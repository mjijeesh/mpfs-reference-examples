#include <stdio.h>
#include <string.h>
#include "board_config.h"
#include "mpfs_hal/common/nwc/mss_io_config.h"
#include "mmc_storage.h"
#include "drivers/mss/mss_mmc/mss_mmc.h"
#include "drivers/mss/mss_gpio/mss_gpio.h"

/* ASCII signature ("MMCF") used to validate persistent metadata on sector 20480 */
#define MMC_FILE_MAGIC  0x4D4D4346UL

/* Preserves the last hardware driver status code for post-boot UART diagnostics */
static mss_mmc_status_t g_last_init_status = MSS_MMC_NOT_INITIALISED;

/**
 * @brief Persistent 512-byte metadata header stored at MMC_META_SECTOR (LBA 20480).
 *
 * Packed and 4-byte aligned to match exactly one hardware flash sector so it can
 * be passed directly to MSS_MMC_single_block_write() and MSS_MMC_single_block_read().
 */
typedef struct __attribute__((packed, aligned(4))) {
    uint32_t magic;                             /* Must equal MMC_FILE_MAGIC if valid */
    uint32_t file_size;                         /* Exact payload byte count (excludes padding) */
    uint32_t crc32;                             /* Finalized IEEE 802.3 CRC32 checksum */
    char     filename[64];                      /* Null-terminated stored filename */
    uint8_t  reserved[MMC_SECTOR_SIZE - 76U];   /* Zero-padding to fill 512-byte sector */
} mmc_file_meta_t;

/* Runtime storage state */
static bool            g_mmc_ready = false;      /* True once eMMC or SD init succeeds */
static bool            g_active_is_emmc = true;  /* True = onboard eMMC, False = microSD (J22) */
static uint8_t         g_sector_buf[MMC_SECTOR_SIZE] __attribute__((aligned(8))); /* 8-byte aligned DMA/FIFO staging buffer */
static uint32_t        g_buf_used = 0U;          /* Valid bytes currently staged in g_sector_buf */
static uint32_t        g_write_sector = MMC_DATA_START_SECTOR; /* Next target LBA sector for writes */
static uint32_t        g_total_written = 0U;     /* Cumulative payload bytes received in active upload */
static uint32_t        g_running_crc = 0xFFFFFFFFUL; /* Incremental CRC32 accumulator */
static char            g_active_name[64];        /* Filename for the in-progress TFTP upload */
static mmc_file_meta_t g_cached_meta;            /* In-memory copy of sector 20480 metadata */

/**
 * @brief Transmits a null-terminated diagnostic string over the application UART.
 *
 * @param s Pointer to the string to print.
 */
static void mmc_log(const char *s)
{
    MSS_UART_polled_tx_string(APP_UART, (const uint8_t *)s);
}

/**
 * @brief Updates a running IEEE 802.3 CRC32 checksum across streaming data chunks.
 *
 * @param crc Previous CRC32 state (seed with 0xFFFFFFFFUL on the first chunk).
 * @param buf Pointer to the incoming byte buffer.
 * @param len Number of bytes to fold into the checksum.
 * @return uint32_t Updated (un-inverted) CRC32 accumulator value.
 */
static uint32_t crc32_update(uint32_t crc, const uint8_t *buf, size_t len)
{
    for (size_t i = 0U; i < len; ++i) {
        crc ^= (uint32_t)buf[i];
        for (uint8_t b = 0U; b < 8U; ++b) {
            crc = (crc & 1U) ? ((crc >> 1) ^ 0xEDB88320UL) : (crc >> 1);
        }
    }
    return crc;
}

/**
 * @brief Hard-resets the Cadence eMMC/SD host controller and waits for clock lock.
 *
 * Pulses SOFT_RESET_CR_MMC_MASK while keeping SUBBLK_CLOCK_CR_MMC_MASK enabled,
 * clearing any stale controller state left over from JTAG/HSS or a prior probe.
 */
static void mmc_reset_block(void)
{
    SYSREG->SUBBLK_CLOCK_CR |= (uint32_t)SUBBLK_CLOCK_CR_MMC_MASK;
    SYSREG->SOFT_RESET_CR   |= (uint32_t)SOFT_RESET_CR_MMC_MASK;
    SYSREG->SOFT_RESET_CR   &= ~(uint32_t)SOFT_RESET_CR_MMC_MASK;

    /* ~50 ms spin delay to allow internal SD/eMMC clock dividers to stabilize */
    for (volatile uint32_t d = 0U; d < 250000U; ++d) {
        __asm__ volatile ("nop");
    }
}

/**
 * @brief Attempts to initialize the onboard 8-bit 1.8V eMMC device.
 *
 * Switches Bank 4 MSSIO electrical pads and IOMUX6_CR to eMMC mode, then probes
 * the device at 200 MHz (HS200) with an automatic fallback to 50 MHz (SDR).
 *
 * @param xml_ok True if the Libero XML configuration supports runtime MSSIO switching.
 * @param sw_ok  Output flag updated with the return status of switch_mssio_config().
 * @param out_st Output pointer receiving the final mss_mmc_status_t code.
 * @return true  If eMMC initialized successfully.
 * @return false If eMMC initialization failed at both speeds.
 */
static bool try_init_emmc(bool xml_ok, bool *sw_ok, mss_mmc_status_t *out_st)
{
    char msg[128];
    mss_mmc_cfg_t cfg;

    mmc_reset_block();
    if (xml_ok) {
        *sw_ok = switch_mssio_config(EMMC_MSSIO_CONFIGURATION);
    }

    /* Primary attempt: 8-bit eMMC @ 200 MHz (HS200) */
    memset(&cfg, 0, sizeof(cfg));
    cfg.card_type      = MSS_MMC_CARD_TYPE_MMC;
    cfg.data_bus_width = MSS_MMC_DATA_WIDTH_8BIT;
    cfg.bus_voltage    = MSS_MMC_1_8V_BUS_VOLTAGE;
    cfg.clk_rate       = MSS_MMC_CLOCK_200MHZ;
    cfg.bus_speed_mode = MSS_MMC_MODE_HS200;

    mss_mmc_status_t st = MSS_MMC_init(&cfg);
    if (st != MSS_MMC_INIT_SUCCESS) {
        /* Fallback attempt: reset controller and retry eMMC @ 50 MHz (SDR) */
        mmc_reset_block();
        cfg.clk_rate       = MSS_MMC_CLOCK_50MHZ;
        cfg.bus_speed_mode = MSS_MMC_MODE_SDR;
        st = MSS_MMC_init(&cfg);
    }

    *out_st = st;
    if (st == MSS_MMC_INIT_SUCCESS) {
        g_last_init_status = st;
        g_mmc_ready        = true;
        g_active_is_emmc   = true;
        snprintf(msg, sizeof(msg),
                 "[MMC] eMMC initialized (%s MHz, 8-bit, sector=%u B)\r\n",
                 (cfg.clk_rate == MSS_MMC_CLOCK_200MHZ) ? "200" : "50",
                 (unsigned int)MMC_SECTOR_SIZE);
        mmc_log(msg);
        return true;
    }
    return false;
}

/**
 * @brief Attempts to initialize a 4-bit microSD card in slot J22.
 *
 * Switches Bank 4 MSSIO pads and IOMUX6_CR to SD mode, then probes the card
 * at 50 MHz High-Speed (HS) with an automatic fallback to Default Speed (DS).
 *
 * @param xml_ok True if the Libero XML configuration supports runtime MSSIO switching.
 * @param sw_ok  Output flag updated with the return status of switch_mssio_config().
 * @param out_st Output pointer receiving the final mss_mmc_status_t code.
 * @return true  If the SD card initialized successfully.
 * @return false If SD card initialization failed or no card is inserted in J22.
 */
static bool try_init_sdcard(bool xml_ok, bool *sw_ok, mss_mmc_status_t *out_st)
{
    char msg[128];
    mss_mmc_cfg_t cfg;

    mmc_reset_block();
    if (xml_ok) {
        *sw_ok = switch_mssio_config(SD_MSSIO_CONFIGURATION);
    }

    /* Primary attempt: 4-bit microSD @ 50 MHz (High Speed) */
    memset(&cfg, 0, sizeof(cfg));
    cfg.card_type      = MSS_MMC_CARD_TYPE_SD;
    cfg.data_bus_width = MSS_MMC_DATA_WIDTH_4BIT;
    cfg.bus_speed_mode = MSS_SDCARD_MODE_HIGH_SPEED;
    cfg.clk_rate       = MSS_MMC_CLOCK_50MHZ;

    mss_mmc_status_t st = MSS_MMC_init(&cfg);
    if (st != MSS_MMC_INIT_SUCCESS) {
        /* Fallback attempt: reset controller and retry SD @ 50 MHz (Default Speed) */
        mmc_reset_block();
        cfg.bus_speed_mode = MSS_SDCARD_MODE_DEFAULT_SPEED;
        st = MSS_MMC_init(&cfg);
    }

    *out_st = st;
    g_last_init_status = st;
    if (st == MSS_MMC_INIT_SUCCESS) {
        g_mmc_ready      = true;
        g_active_is_emmc = false;
        snprintf(msg, sizeof(msg),
                 "[MMC] SD Card initialized (50 MHz, %s, 4-bit, sector=%u B)\r\n",
                 (cfg.bus_speed_mode == MSS_SDCARD_MODE_HIGH_SPEED) ? "HS" : "DS",
                 (unsigned int)MMC_SECTOR_SIZE);
        mmc_log(msg);
        return true;
    }
    return false;
}

/**
 * @brief Initializes or hot-switches the shared Bank 4 storage interface at runtime.
 *
 * Used both at boot and by the UART CLI ('e' / 'c' keys) to switch between the
 * soldered eMMC and the microSD slot (J22). When use_emmc is true, eMMC is tried
 * first and falls back to SD card on failure; when false, SD card is probed directly.
 *
 * @param use_emmc True to select onboard eMMC (with SD fallback), false to select SD card directly.
 * @return true    If the selected storage medium is initialized and ready for I/O.
 * @return false   If initialization failed (diagnostic registers are logged to UART).
 */
bool mmc_storage_init_media(bool use_emmc)
{
    char msg[192];
    mss_mmc_status_t st = MSS_MMC_NOT_INITIALISED;
    bool xml_ok = mss_does_xml_ver_support_switch();
    bool sw_ok  = false;

    g_mmc_ready = false;

    if (use_emmc) {
        if (try_init_emmc(xml_ok, &sw_ok, &st)) {
            return true;
        }
    }

    if (try_init_sdcard(xml_ok, &sw_ok, &st)) {
        return true;
    }

    /* Log controller and mux diagnostics if initialization failed */
    snprintf(msg, sizeof(msg),
             "[MMC] %s init failed (status=%d, xml=%d, sw=%d, IOMUX6=0x%08x)\r\n",
             use_emmc ? "eMMC/SD" : "SD Card",
             (int)st, (int)xml_ok, (int)sw_ok,
             (unsigned int)SYSREG->IOMUX6_CR);
    mmc_log(msg);
    return false;
}

/**
 * @brief Default boot-time storage initialization entry point.
 *
 * Delegates to mmc_storage_init_media() using the compile-time default
 * APP_MMC_USE_EMMC defined in board_config.h.
 *
 * @return true if storage initialized successfully, false otherwise.
 */
bool mmc_storage_init(void)
{
    return mmc_storage_init_media(APP_MMC_USE_EMMC != 0);
}

/**
 * @brief Queries whether the storage subsystem is initialized and ready for block I/O.
 *
 * @return true if either eMMC or SD Card is active and ready.
 */
bool mmc_storage_is_ready(void)
{
    return g_mmc_ready;
}

/**
 * @brief Reads sector 20480 and prints the active interface and stored file metadata to UART.
 *
 * Triggered via the UART CLI ('m' or 't' keys). Uses g_active_is_emmc so the output
 * accurately reflects runtime switching between eMMC and SD Card.
 */
void mmc_storage_print_info(void)
{
    char msg[192];

    if (!g_mmc_ready) {
        snprintf(msg, sizeof(msg), "\r\n[MMC] Device not initialized (last status=%d).\r\n",
                 (int)g_last_init_status);
        mmc_log(msg);
        return;
    }

    memset(&g_cached_meta, 0, sizeof(g_cached_meta));
    (void)MSS_MMC_single_block_read(MMC_META_SECTOR, (uint32_t *)&g_cached_meta);

    if (g_cached_meta.magic == MMC_FILE_MAGIC) {
        snprintf(msg, sizeof(msg),
                 "\r\n[MMC] Media : %s | Stored File: \"%s\" (%u bytes, CRC32=0x%08x @ sector %u)\r\n",
                 g_active_is_emmc ? "eMMC" : "SD Card",
                 g_cached_meta.filename,
                 (unsigned int)g_cached_meta.file_size,
                 (unsigned int)g_cached_meta.crc32,
                 (unsigned int)MMC_DATA_START_SECTOR);
    } else {
        snprintf(msg, sizeof(msg),
                 "\r\n[MMC] Media : %s | No TFTP file stored yet (meta sector %u empty)\r\n",
                 g_active_is_emmc ? "eMMC" : "SD Card",
                 (unsigned int)MMC_META_SECTOR);
    }
    mmc_log(msg);
}

/**
 * @brief Runs a non-destructive 512-byte sector write/read-back self-test on the active medium.
 *
 * Writes a deterministic test pattern to scratch sector 20479 (MMC_META_SECTOR - 1),
 * reads it back, verifies byte-for-byte equality, and prints PASS/FAIL along with
 * the currently selected interface ("eMMC" or "SD Card").
 */
void mmc_storage_run_selftest(void)
{
    char msg[160];
    static uint32_t tx_blk[MMC_SECTOR_SIZE / 4U] __attribute__((aligned(8)));
    static uint32_t rx_blk[MMC_SECTOR_SIZE / 4U] __attribute__((aligned(8)));
    const uint32_t test_sector = MMC_META_SECTOR - 1U;

    if (!g_mmc_ready) {
        mmc_log("\r\n[MMC-TEST] Aborted: MMC not ready.\r\n");
        return;
    }

    for (uint32_t i = 0U; i < (MMC_SECTOR_SIZE / 4U); ++i) {
        tx_blk[i] = 0xA55A0000UL | i;
        rx_blk[i] = 0U;
    }

    mss_mmc_status_t wst = MSS_MMC_single_block_write(tx_blk, test_sector);
    mss_mmc_status_t rst = MSS_MMC_single_block_read(test_sector, rx_blk);

    const char *media = g_active_is_emmc ? "eMMC" : "SD Card";

    if ((wst == MSS_MMC_TRANSFER_SUCCESS) && (rst == MSS_MMC_TRANSFER_SUCCESS) &&
        (memcmp(tx_blk, rx_blk, MMC_SECTOR_SIZE) == 0)) {
        snprintf(msg, sizeof(msg),
                 "\r\n[MMC-TEST] PASS (%s): Wrote & verified 512 B at sector %u\r\n",
                 media, (unsigned int)test_sector);
    } else {
        snprintf(msg, sizeof(msg),
                 "\r\n[MMC-TEST] FAIL (%s): write=%d read=%d at sector %u\r\n",
                 media, (int)wst, (int)rst, (unsigned int)test_sector);
    }
    mmc_log(msg);
}

/**
 * @brief Prepares the storage layer for a new incoming file upload stream (TFTP WRQ).
 *
 * Resets staging counters, points the LBA write cursor to MMC_DATA_START_SECTOR (20481),
 * seeds the incremental CRC32 accumulator, and records the target filename.
 *
 * @param filename Null-terminated name of the incoming file.
 * @return true if storage is ready and filename is valid, false otherwise.
 */
bool mmc_file_open_write(const char *filename)
{
    if (!g_mmc_ready || (filename == NULL)) {
        return false;
    }
    g_buf_used      = 0U;
    g_write_sector  = MMC_DATA_START_SECTOR;
    g_total_written = 0U;
    g_running_crc   = 0xFFFFFFFFUL;
    strncpy(g_active_name, filename, sizeof(g_active_name) - 1U);
    g_active_name[sizeof(g_active_name) - 1U] = '\0';
    return true;
}

/**
 * @brief Appends an arbitrary byte slice to the active file upload stream.
 *
 * Accumulates incoming network bytes into the 8-byte aligned 512-byte staging buffer
 * (g_sector_buf). Whenever a full sector accumulates, it flushes the block to flash
 * via MSS_MMC_single_block_write() and updates the running CRC32 checksum.
 *
 * @param data Pointer to the incoming payload bytes.
 * @param len  Number of bytes in this chunk.
 * @return int Number of bytes accepted, or -1 on size overflow or flash write error.
 */
int mmc_file_write_chunk(const uint8_t *data, uint16_t len)
{
    if (!g_mmc_ready || ((g_total_written + len) > MMC_MAX_FILE_BYTES)) {
        return -1;
    }

    uint16_t offset = 0U;
    while (offset < len) {
        uint32_t space = MMC_SECTOR_SIZE - g_buf_used;
        uint32_t chunk = ((uint32_t)(len - offset) < space) ? (uint32_t)(len - offset) : space;

        memcpy(&g_sector_buf[g_buf_used], &data[offset], chunk);
        g_buf_used      += chunk;
        offset          += (uint16_t)chunk;
        g_total_written += chunk;

        if (g_buf_used == MMC_SECTOR_SIZE) {
            if (MSS_MMC_single_block_write((uint32_t *)g_sector_buf, g_write_sector) != MSS_MMC_TRANSFER_SUCCESS) {
                return -1;
            }
            g_write_sector++;
            g_buf_used = 0U;
        }
    }

    g_running_crc = crc32_update(g_running_crc, data, len);
    return (int)len;
}

/**
 * @brief Finalizes the active file upload and commits persistent metadata to flash.
 *
 * Zero-pads and writes any trailing partial sector (< 512 bytes), finalizes the
 * CRC32 checksum, writes the mmc_file_meta_t header to MMC_META_SECTOR (20480),
 * and logs the saved filename, target medium ("eMMC" or "SD Card"), size, and CRC32.
 */
void mmc_file_close_write(void)
{
    char msg[160];
    if (!g_mmc_ready) {
        return;
    }

    /* Flush any remaining partial sector (< 512 bytes) */
    if (g_buf_used > 0U) {
        memset(&g_sector_buf[g_buf_used], 0, MMC_SECTOR_SIZE - g_buf_used);
        (void)MSS_MMC_single_block_write((uint32_t *)g_sector_buf, g_write_sector);
        g_buf_used = 0U;
    }

    /* Commit file metadata header to MMC_META_SECTOR */
    memset(&g_cached_meta, 0, sizeof(g_cached_meta));
    g_cached_meta.magic     = MMC_FILE_MAGIC;
    g_cached_meta.file_size = g_total_written;
    g_cached_meta.crc32     = ~g_running_crc;
    memcpy(g_cached_meta.filename, g_active_name, sizeof(g_cached_meta.filename) - 1U);

    (void)MSS_MMC_single_block_write((uint32_t *)&g_cached_meta, MMC_META_SECTOR);

    snprintf(msg, sizeof(msg),
             "\r\n[MMC] Saved \"%s\" to %s (%u bytes, CRC32=0x%08x)\r\n",
             g_cached_meta.filename,
             g_active_is_emmc ? "eMMC" : "SD Card",
             (unsigned int)g_cached_meta.file_size,
             (unsigned int)g_cached_meta.crc32);
    mmc_log(msg);
}

/**
 * @brief Opens a stored file for reading (TFTP RRQ) by verifying sector 20480 metadata.
 *
 * Reads MMC_META_SECTOR from the active storage medium, verifies the MMC_FILE_MAGIC
 * signature and exact filename match, and outputs the stored byte length.
 *
 * @param filename Null-terminated filename requested by the client.
 * @param out_size Output pointer receiving the exact stored file size in bytes.
 * @return true    If the file exists on the active medium and is ready to read.
 * @return false   If the medium is offline, no file is stored, or the name mismatches.
 */
bool mmc_file_open_read(const char *filename, uint32_t *out_size)
{
    if (!g_mmc_ready || (filename == NULL) || (out_size == NULL)) {
        return false;
    }

    memset(&g_cached_meta, 0, sizeof(g_cached_meta));
    if (MSS_MMC_single_block_read(MMC_META_SECTOR, (uint32_t *)&g_cached_meta) != MSS_MMC_TRANSFER_SUCCESS) {
        return false;
    }

    if ((g_cached_meta.magic != MMC_FILE_MAGIC) ||
        (strncmp(g_cached_meta.filename, filename, sizeof(g_cached_meta.filename)) != 0)) {
        return false;
    }

    *out_size = g_cached_meta.file_size;
    return true;
}

/**
 * @brief Reads a byte slice from the stored file at an arbitrary logical offset.
 *
 * Maps the requested byte range to underlying 512-byte LBA sectors starting at
 * MMC_DATA_START_SECTOR (20481), reads each spanned sector into g_sector_buf,
 * and copies the requested slice into the caller's buffer.
 *
 * @param buf     Destination buffer to receive the file bytes.
 * @param max_len Maximum number of bytes to read.
 * @param offset  Logical byte offset from the start of the stored file.
 * @return int    Number of bytes copied (0 at EOF), or -1 on flash read failure.
 */
int mmc_file_read_chunk(uint8_t *buf, uint32_t max_len, uint32_t offset)
{
    if (!g_mmc_ready || (offset >= g_cached_meta.file_size)) {
        return 0;
    }

    uint32_t remaining = g_cached_meta.file_size - offset;
    uint32_t to_read   = (remaining < max_len) ? remaining : max_len;
    uint32_t copied    = 0U;

    while (copied < to_read) {
        uint32_t cur_off  = offset + copied;
        uint32_t sector   = MMC_DATA_START_SECTOR + (cur_off / MMC_SECTOR_SIZE);
        uint32_t sec_off  = cur_off % MMC_SECTOR_SIZE;
        uint32_t sec_copy = MMC_SECTOR_SIZE - sec_off;
        if (sec_copy > (to_read - copied)) {
            sec_copy = to_read - copied;
        }

        if (MSS_MMC_single_block_read(sector, (uint32_t *)g_sector_buf) != MSS_MMC_TRANSFER_SUCCESS) {
            return -1;
        }

        memcpy(&buf[copied], &g_sector_buf[sec_off], sec_copy);
        copied += sec_copy;
    }

    return (int)copied;
}
