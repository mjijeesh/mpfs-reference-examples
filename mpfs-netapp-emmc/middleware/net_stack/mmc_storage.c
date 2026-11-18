#include <stdio.h>
#include <string.h>
#include "board_config.h"
#include "mpfs_hal/common/nwc/mss_io_config.h"
#include "mmc_storage.h"
#include "drivers/mss/mss_mmc/mss_mmc.h"
#include "drivers/mss/mss_gpio/mss_gpio.h"

#define MMC_FILE_MAGIC  0x4D4D4346UL /* "MMCF" */

static mss_mmc_status_t g_last_init_status = MSS_MMC_NOT_INITIALISED;

typedef struct __attribute__((packed, aligned(4))) {
    uint32_t magic;
    uint32_t file_size;
    uint32_t crc32;
    char     filename[64];
    uint8_t  reserved[MMC_SECTOR_SIZE - 76U];
} mmc_file_meta_t;

static bool            g_mmc_ready = false;
static uint8_t         g_sector_buf[MMC_SECTOR_SIZE] __attribute__((aligned(8)));
static uint32_t        g_buf_used = 0U;
static uint32_t        g_write_sector = MMC_DATA_START_SECTOR;
static uint32_t        g_total_written = 0U;
static uint32_t        g_running_crc = 0xFFFFFFFFUL;
static char            g_active_name[64];
static mmc_file_meta_t g_cached_meta;

/**
 * @brief Helper function to log strings out the main application UART.
 */
static void mmc_log(const char *s)
{
    MSS_UART_polled_tx_string(APP_UART, (const uint8_t *)s);
}

/**
 * @brief Computes a running IEEE 802.3 CRC32 checksum.
 * 
 * @param crc The previous CRC32 value (seed with 0xFFFFFFFFUL on first call).
 * @param buf Pointer to the byte array to process.
 * @param len Number of bytes to process.
 * @return uint32_t The updated CRC32 checksum.
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
 * @brief Hard-resets the eMMC/SD host controller.
 * 
 * This pulses the SOFT_RESET_CR_MMC_MASK to clear any dirty state left by 
 * prior boot stages (like HSS) or failed initialization attempts, giving the 
 * internal clock dividers time to stabilize.
 */
static void mmc_reset_block(void)
{
    SYSREG->SUBBLK_CLOCK_CR |= (uint32_t)SUBBLK_CLOCK_CR_MMC_MASK;
    SYSREG->SOFT_RESET_CR   |= (uint32_t)SOFT_RESET_CR_MMC_MASK;
    SYSREG->SOFT_RESET_CR   &= ~(uint32_t)SOFT_RESET_CR_MMC_MASK;

    /* Spin delay to allow hardware clock domain synchronization */
    for (volatile uint32_t d = 0U; d < 250000U; ++d) {
        __asm__ volatile ("nop");
    }
}

/**
 * @brief Main hardware initialization sequence for the storage subsystem.
 * 
 * Implements a fallback chain:
 * 1. Tries eMMC at 200 MHz (HS200)
 * 2. Tries eMMC at 50 MHz (SDR)
 * 3. Tries SD Card at 50 MHz (High Speed)
 * 4. Tries SD Card at 50 MHz (Default Speed)
 * 
 * @return true if media was successfully initialized and identified.
 */
bool mmc_storage_init(void)
{
    char msg[192];
    mss_mmc_cfg_t cfg;
    mss_mmc_status_t st;
    bool xml_ok = mss_does_xml_ver_support_switch();
    bool sw_ok  = false;

    mmc_reset_block();

    /* 1. Configure Bank 4 MSSIO for onboard eMMC (1.8V, 8-bit) */
    if (xml_ok) {
        sw_ok = switch_mssio_config(EMMC_MSSIO_CONFIGURATION);
    }

    memset(&cfg, 0, sizeof(cfg));
    cfg.card_type      = MSS_MMC_CARD_TYPE_MMC;
    cfg.data_bus_width = MSS_MMC_DATA_WIDTH_8BIT;
    cfg.bus_voltage    = MSS_MMC_1_8V_BUS_VOLTAGE;
    cfg.clk_rate       = MSS_MMC_CLOCK_200MHZ;
    cfg.bus_speed_mode = MSS_MMC_MODE_HS200;

    st = MSS_MMC_init(&cfg);
    if (st != MSS_MMC_INIT_SUCCESS) {
        mmc_reset_block();
        cfg.clk_rate       = MSS_MMC_CLOCK_50MHZ;
        cfg.bus_speed_mode = MSS_MMC_MODE_SDR;
        st = MSS_MMC_init(&cfg);
    }

    if (st == MSS_MMC_INIT_SUCCESS) {
        g_last_init_status = st;
        g_mmc_ready = true;
        snprintf(msg, sizeof(msg),
                 "[MMC] eMMC initialized (%s MHz, 8-bit, sector=%u B)\r\n",
                 (cfg.clk_rate == MSS_MMC_CLOCK_200MHZ) ? "200" : "50",
                 (unsigned int)MMC_SECTOR_SIZE);
        mmc_log(msg);
        return true;
    }

    /* 2. Fallback: Configure Bank 4 MSSIO for microSD slot J22 (4-bit) */
    mmc_reset_block();
    if (xml_ok) {
        sw_ok = switch_mssio_config(SD_MSSIO_CONFIGURATION);
    }

    memset(&cfg, 0, sizeof(cfg));
    cfg.card_type      = MSS_MMC_CARD_TYPE_SD;
    cfg.data_bus_width = MSS_MMC_DATA_WIDTH_4BIT;
    cfg.bus_speed_mode = MSS_SDCARD_MODE_HIGH_SPEED;
    cfg.clk_rate       = MSS_MMC_CLOCK_50MHZ;

    st = MSS_MMC_init(&cfg);
    if (st != MSS_MMC_INIT_SUCCESS) {
        mmc_reset_block();
        cfg.bus_speed_mode = MSS_SDCARD_MODE_DEFAULT_SPEED;
        st = MSS_MMC_init(&cfg);
    }

    g_last_init_status = st;

    if (st == MSS_MMC_INIT_SUCCESS) {
        g_mmc_ready = true;
        snprintf(msg, sizeof(msg),
                 "[MMC] SD Card initialized (50 MHz, %s, 4-bit, sector=%u B)\r\n",
                 (cfg.bus_speed_mode == MSS_SDCARD_MODE_HIGH_SPEED) ? "HS" : "DS",
                 (unsigned int)MMC_SECTOR_SIZE);
        mmc_log(msg);
        return true;
    }

    /* Total failure to initialize any media */
    g_mmc_ready = false;
    snprintf(msg, sizeof(msg),
             "[MMC] Init failed (status=%d, xml=%d, sw=%d, IOMUX6=0x%08x)\r\n",
             (int)st, (int)xml_ok, (int)sw_ok,
             (unsigned int)SYSREG->IOMUX6_CR);
    mmc_log(msg);
    return false;
}

/**
 * @brief Returns the readiness state of the block storage.
 */
bool mmc_storage_is_ready(void)
{
    return g_mmc_ready;
}

/**
 * @brief Reads the metadata sector and prints file attributes to UART.
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
                 APP_MMC_USE_EMMC ? "eMMC" : "SD Card",
                 g_cached_meta.filename,
                 (unsigned int)g_cached_meta.file_size,
                 (unsigned int)g_cached_meta.crc32,
                 (unsigned int)MMC_DATA_START_SECTOR);
    } else {
        snprintf(msg, sizeof(msg),
                 "\r\n[MMC] Media : %s | No TFTP file stored yet (meta sector %u empty)\r\n",
                 APP_MMC_USE_EMMC ? "eMMC" : "SD Card",
                 (unsigned int)MMC_META_SECTOR);
    }
    mmc_log(msg);
}

/**
 * @brief Executes a non-destructive hardware sector write and read-back test.
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

    if ((wst == MSS_MMC_TRANSFER_SUCCESS) && (rst == MSS_MMC_TRANSFER_SUCCESS) &&
        (memcmp(tx_blk, rx_blk, MMC_SECTOR_SIZE) == 0)) {
        snprintf(msg, sizeof(msg),
                 "\r\n[MMC-TEST] PASS: Wrote & verified 512 B at sector %u\r\n",
                 (unsigned int)test_sector);
    } else {
        snprintf(msg, sizeof(msg),
                 "\r\n[MMC-TEST] FAIL: write=%d read=%d at sector %u\r\n",
                 (int)wst, (int)rst, (unsigned int)test_sector);
    }
    mmc_log(msg);
}

/**
 * @brief Prepares the storage layer for an incoming file upload stream.
 * 
 * Resets internal byte counters, points the sector write pointer back to the 
 * start of the data partition, and seeds the CRC calculation.
 * 
 * @param filename Name of the file being written.
 * @return true if storage is ready, false otherwise.
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
 * @brief Streams raw bytes into the eMMC/SD data partition.
 * 
 * Accumulates arbitrarily sized byte arrays (e.g. from network packets) into 
 * a 512-byte staging buffer. Whenever the buffer fills, it writes the sector 
 * to flash and advances the sector pointer.
 * 
 * @param data Pointer to the payload bytes.
 * @param len Number of bytes in this chunk.
 * @return int Number of bytes successfully buffered/written, or -1 on error.
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
 * @brief Finalizes an active file upload.
 * 
 * Flushes any remaining partial data in the staging buffer to flash, computes
 * the final CRC32, and writes the persistent metadata block to sector 20480.
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
             "\r\n[MMC] Saved \"%s\" (%u bytes, CRC32=0x%08x)\r\n",
             g_cached_meta.filename,
             (unsigned int)g_cached_meta.file_size,
             (unsigned int)g_cached_meta.crc32);
    mmc_log(msg);
}

/**
 * @brief Prepares to stream a file out of flash storage.
 * 
 * Validates the magic signature on the metadata sector and checks if the 
 * requested filename matches the one on disk.
 * 
 * @param filename Name of the file requested by the network client.
 * @param out_size Pointer to return the total byte size of the file.
 * @return true if file exists and is ready to read, false otherwise.
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
 * @brief Reads an arbitrary slice of bytes from a stored file.
 * 
 * Computes the correct flash sector and sector-offset for the requested 
 * logical byte offset, reads the sector(s), and copies the exact requested 
 * slice into the provided buffer.
 * 
 * @param buf Output buffer to hold the requested bytes.
 * @param max_len Maximum number of bytes requested.
 * @param offset Logical byte offset inside the file.
 * @return int Number of bytes successfully read.
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
