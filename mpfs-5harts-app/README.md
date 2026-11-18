# PolarFire SoC Bare-Metal eMMC & SD Card Bring-Up Guide

This document captures the architecture, critical hardware learnings, and step-by-step file modifications required to bring up the Cadence SD/eMMC host controller on the Microchip PolarFire SoC bare-metal environment, completely independent of the Hart Software Services (HSS) bootloader.

It also details how to bridge this block storage driver to an lwIP TFTP server for direct PC-to-eMMC file transfers.

## 1. Key Learnings & Hardware Quirks

During the bring-up process, we uncovered several critical requirements that must be met to avoid the dreaded `status=1` (`MSS_MMC_NOT_INITIALISED`) error. 

### A. The Hardware Reset Pulse is Mandatory
Simply enabling the peripheral clock via `mss_config_clk_rst(MSS_PERIPH_EMMC, ..., PERIPHERAL_ON)` is insufficient, especially when debugging via JTAG (`lim-debug`) or taking over from a previous boot stage. The Cadence host controller and the physical eMMC chip must be forcefully reset to a clean state before `MSS_MMC_init()` is called. 
* **Fix:** Assert `SOFT_RESET_CR_MMC_MASK`, de-assert it, and wait for ~250,000 clock cycles (`NOP` loops) before attempting initialization.

### B. MSSIO Pin Muxing (`switch_mssio_config`)
The eMMC and SD card pins (CLK, CMD, DAT0-7) are located on **Bank 4 MSSIO**. On boards like the Icicle Kit, you must dynamically configure the electrical pads (1.8V vs 3.3V) and route the signals to the controller.
* **Fix:** You must call `switch_mssio_config(EMMC_MSSIO_CONFIGURATION)` from `mpfs_hal/common/nwc/mss_io_config.h` before initialization. This relies on the Libero XML-generated `hw_mssio_mux.h` being correct for your specific board.

### C. Speed Modes and Voltages
Microchip's driver uses distinct enums for SD vs. eMMC, and crossing them causes initialization failures during the PHY delay calibration:
* **eMMC (8-bit, 1.8V):** Use `MSS_MMC_MODE_HS200` (200 MHz) or `MSS_MMC_MODE_HSD` (High-Speed 50 MHz). *Never use `MSS_MMC_MODE_SDR` for eMMC.*
* **SD Card (4-bit, 1.8V/3.3V):** Use `MSS_SDCARD_MODE_HIGH_SPEED` or `MSS_MMC_MODE_SDR` (50 MHz).
* **Fallback:** Always implement a fallback to `MSS_MMC_CLOCK_25MHZ` with `MSS_MMC_MODE_LEGACY` if the high-speed initialization fails.

---

## 2. Required File Modifications

To replicate this implementation on a new board or project, the following files must be updated.

### A. Build System (`boards/<board>/board.mk`)
Ensure the MMC driver is compiled into the project. Depending on your `platform/` snapshot, you only need `mss_mmc.c`.
```makefile
BOARD_SRCS_C += platform/drivers/mss/mss_mmc/mss_mmc.c
```

### B. Hardware Initialization (`boards/<board>/board_init.c`)
Release the reset for the EMMC peripheral before handing off to the application.
```c
/* In board_periph_init() */
(void)mss_config_clk_rst(MSS_PERIPH_EMMC, (uint8_t)MPFS_HAL_FIRST_HART, PERIPHERAL_ON);
```

### C. The Storage Wrapper (`middleware/net_stack/mmc_storage.c` & `.h`)
Create a dedicated storage module to handle the hardware initialization sequence, 512-byte sector alignment, and metadata tracking.

**Key functions to implement:**
1. `mmc_reset_block()`: Manually toggles `SYSREG->SOFT_RESET_CR`.
2. `mmc_storage_init()`: Attempts eMMC initialization (with `switch_mssio_config`), falls back to lower speeds, and finally falls back to SD Card mode.
3. `mmc_file_write_chunk()`: Buffers arbitrary incoming byte lengths (e.g., from network packets) and calls `MSS_MMC_single_block_write()` strictly on 512-byte boundaries.

### D. TFTP Server Hook (`middleware/net_stack/tftp_server_app.c`)
Modify the lwIP TFTP context to route network data directly to the eMMC rather than a RAM-disk.
```c
static const struct tftp_context g_tftp_ctx = {
    .open  = tftp_mmc_open,   // Calls mmc_file_open_write()
    .close = tftp_mmc_close,  // Calls mmc_file_close_write()
    .read  = tftp_mmc_read,   // Calls mmc_file_read_chunk()
    .write = tftp_mmc_write,  // Calls mmc_file_write_chunk()
};

void tftp_server_app_init(void) {
    tftp_init_server(&g_tftp_ctx);
}
```

### E. Application Main Loop (`application/hart1/u54_1.c`)
Initialize the storage subsystem before the network stack, and add interactive UART diagnostics.
```c
/* In u54_1() main loop setup */
(void)mmc_storage_init();
net_port_init();
tftp_server_app_init();
```

---

## 3. Troubleshooting `MSS_MMC_NOT_INITIALISED` (Status = 1)

If `mmc_storage_init()` returns `1`, check the following in order:

1. **Check your Libero XML generation:** Ensure `boards/<board>/fpga_design_config/io/hw_mssio_mux.h` has `0x00000000UL` for `IOMUX1_CR`, `IOMUX2_CR`, and `IOMUX6_CR`. If they contain GPIO mappings, the eMMC pins are physically disconnected inside the silicon.
2. **Check your Reset Sequence:** Ensure `mmc_reset_block()` is executing before *every* call to `MSS_MMC_init()`.
3. **Check the Fabric Demux (Icicle Kit Specific):** Older Icicle Kit FPGA designs routed the SD/eMMC mux through the fabric. If `CONFIG_SERVICE_MMC_FABRIC_SD_EEMC_DEMUX_SELECTED_PRESENT` is defined in your hardware design, you must write `0U` (eMMC) or `1U` (SD) to the demux address (usually `0x4F000000UL`).



# MMC Storage Abstraction Layer (`mmc_storage.c`)

`mmc_storage.c` acts as a bare-metal storage abstraction layer that bridges byte-oriented network packets from lwIP's TFTP server to 512-byte Logical Block Address (LBA) sectors on the PolarFire SoC's eMMC/SD host controller (`mss_mmc`). 

Because the target runs without an operating system or filesystem (such as FAT32 or ext4), this module implements raw sector management, unaligned-to-aligned buffer accumulation, persistent file metadata, and incremental CRC32 integrity verification.

## 1. Disk Layout, Data Structures & State

Rather than starting at sector `0`—which would overwrite partition tables (MBR/GPT) or boot payloads stored for Hart Software Services (HSS)—the module places all storage operations at a `10 MiB` offset (sector `20480`, assuming `512` bytes per sector):

| LBA Sector | Byte Offset | Purpose |
| :--- | :--- | :--- |
| **`20479`** (`MMC_META_SECTOR - 1`) | `10,485,248` (`~10 MiB - 512 B`) | Dedicated scratch sector for the standalone UART write/read-back self-test, isolated from stored files. |
| **`20480`** (`MMC_META_SECTOR`) | `10,485,760` (`10.0 MiB`) | Persistent `512`-byte file metadata header (`mmc_file_meta_t`). |
| **`20481+`** (`MMC_DATA_START_SECTOR`) | `10,486,272+` | Contiguous raw file payload sectors (up to `MMC_MAX_FILE_BYTES = 16 MiB`). |

To persist file attributes across power cycles without a filesystem, `mmc_file_meta_t` defines an exact `512`-byte packed, 4-byte-aligned structure matching one hardware sector:

* **`magic` (`0x4D4D4346UL`):** ASCII `"MMCF"` signature used to verify whether sector `20480` holds valid file metadata or uninitialized flash data.
* **`file_size` & `crc32`:** Exact byte length of the uploaded file (excluding trailing sector padding) and its IEEE 802.3 CRC32 checksum.
* **`filename[64]` & `reserved[436]`:** Null-terminated filename string plus zeroed padding so `sizeof(mmc_file_meta_t)` equals `512` bytes, preventing out-of-bounds DMA/FIFO reads when passed to `MSS_MMC_single_block_write()`.

Several static state variables coordinate asynchronous network chunks and hardware alignment requirements:

* **`g_sector_buf[512]` (`__attribute__((aligned(8)))`):** Acts as a staging window for accumulating partial incoming network chunks into complete `512`-byte sectors and holding sectors read from flash. The 8-byte alignment is required because `MSS_MMC_single_block_write()` and `MSS_MMC_single_block_read()` cast the buffer pointer to `uint32_t *` for 32-bit FIFO/DMA transfers.
* **`g_mmc_ready` & `g_last_init_status`:** Tracks whether hardware initialization succeeded and preserves the exact `mss_mmc_status_t` enum code (`MSS_MMC_NOT_INITIALISED` by default) so diagnostics remain visible even if a serial terminal connects after boot.
* **Streaming Counters (`g_buf_used`, `g_write_sector`, `g_total_written`, `g_running_crc`):** Track the byte offset inside `g_sector_buf`, the current target LBA sector, total payload bytes received, and the running CRC32 state (`0xFFFFFFFFUL` seed) across packets.

---

## 2. Hardware Initialization & Multi-Stage Fallback

Bringing up eMMC or SD storage in a standalone bare-metal environment requires resetting the Cadence SD/eMMC host controller and configuring Bank 4 MSSIO electrical pads before running card identification.

### Controller Reset (`mmc_reset_block`)
Before every initialization attempt, `mmc_reset_block()` ensures `SUBBLK_CLOCK_CR_MMC_MASK` is enabled in `SYSREG->SUBBLK_CLOCK_CR`, pulses `SOFT_RESET_CR_MMC_MASK` high and then low in `SYSREG->SOFT_RESET_CR`, and executes a `250,000`-iteration `nop` loop. This clears any dirty host-controller state left over from a prior failed probe or bootloader handoff and gives the internal clock dividers time to stabilize.

### Dynamic Bank 4 Switching & Fallback Chain (`mmc_storage_init`)
On the PolarFire SoC Icicle Kit, eMMC and the microSD card slot (`J22`) share MSSIO Bank 4. `mmc_storage_init()` first queries `mss_does_xml_ver_support_switch()` to confirm the Libero hardware configuration supports runtime MSSIO switching, then executes a four-tier initialization sequence:

1. **eMMC @ 200 MHz (`HS200`):** Calls `switch_mssio_config(EMMC_MSSIO_CONFIGURATION)` to configure Bank 4 pads and `SYSREG->IOMUX6_CR` for `1.8V` 8-bit eMMC, then invokes `MSS_MMC_init()` in `MSS_MMC_MODE_HS200`.
2. **eMMC @ 50 MHz (`SDR`):** If HS200 tuning fails (for instance, due to cold JTAG clock settings), resets the controller via `mmc_reset_block()` and retries eMMC at `50 MHz` in `MSS_MMC_MODE_SDR`.
3. **microSD @ 50 MHz (`High Speed`):** If no eMMC responds, resets the block, calls `switch_mssio_config(SD_MSSIO_CONFIGURATION)` to route Bank 4 to the microSD slot (`J22`), and attempts 4-bit `MSS_MMC_CARD_TYPE_SD` initialization in `MSS_SDCARD_MODE_HIGH_SPEED`.
4. **microSD @ 50 MHz (`Default Speed`):** Resets the controller one final time and retries 4-bit SD initialization in `MSS_SDCARD_MODE_DEFAULT_SPEED`.

If all four stages fail, `g_mmc_ready` stays `false` and the function logs `status`, `xml_ok`, `sw_ok`, and `SYSREG->IOMUX6_CR` over UART for debugging.

---

## 3. Streaming TFTP Write & Read Pipelines

TFTP transfers files in `512`-byte data blocks (terminated by a short block of `0`–`511` bytes), and lwIP may deliver a single UDP datagram split across multiple chained `pbuf` descriptors. The streaming API decouples lwIP's packet boundaries from physical flash sector writes.

### Upload Path (`TFTP PUT` -> eMMC/SD)
1. **`mmc_file_open_write(filename)`:** Triggered on a TFTP Write Request (`WRQ`). Resets `g_buf_used` to `0`, points `g_write_sector` to `20481` (`MMC_DATA_START_SECTOR`), seeds `g_running_crc` to `0xFFFFFFFFUL`, and stores `filename` in `g_active_name`.
2. **`mmc_file_write_chunk(data, len)`:** Called for each `pbuf` payload segment. It enforces the `16 MiB` limit (`MMC_MAX_FILE_BYTES`), copies incoming bytes into `g_sector_buf` up to `512` bytes at a time, and whenever `g_buf_used == 512`, commits the full sector to flash via `MSS_MMC_single_block_write()` and increments `g_write_sector`. Simultaneously, `crc32_update()` folds the raw bytes into `g_running_crc` using the standard reflected polynomial `0xEDB88320UL`.
3. **`mmc_file_close_write()`:** Triggered when the final TFTP packet is acknowledged. If trailing bytes remain in `g_sector_buf` (`0 < g_buf_used < 512`), it zero-pads the rest of the sector and writes that final data block. It then finalizes the CRC32 (`~g_running_crc`), populates `g_cached_meta` with `MMC_FILE_MAGIC`, `g_total_written`, and `g_active_name`, and commits the metadata header to sector `20480` (`MMC_META_SECTOR`).

### Download Path (eMMC/SD -> `TFTP GET`)
1. **`mmc_file_open_read(filename, &out_size)`:** Triggered on a TFTP Read Request (`RRQ`). Reads sector `20480` into `g_cached_meta`, verifies `magic == MMC_FILE_MAGIC` and `strcmp` against `filename`, and passes `file_size` back to the TFTP server.
2. **`mmc_file_read_chunk(buf, max_len, offset)`:** Converts any byte-level `offset` and `max_len` request from lwIP into the corresponding LBA sector (`MMC_DATA_START_SECTOR + (cur_off / 512)`) and intra-sector byte offset (`cur_off % 512`), reads each `512`-byte sector into `g_sector_buf` via `MSS_MMC_single_block_read()`, and copies only the requested byte slice into `buf`.

---

## 4. Diagnostics & Standalone Self-Test

* **`mmc_storage_print_info()` (UART CLI `[m]` / `[t]`):** Reads sector `20480` directly from the media and checks for `MMC_FILE_MAGIC`, printing the active media type (`eMMC` or `SD Card`), stored filename, exact byte size, and CRC32—or reporting `g_last_init_status` if the controller is offline.
* **`mmc_storage_run_selftest()` (UART CLI `[w]`):** Fills a 512-byte buffer (`tx_blk`) with a deterministic 32-bit pattern (`0xA55A0000UL | i`), writes it to scratch sector `20479`, reads it back into `rx_blk`, and verifies bit-for-bit equality using `memcmp()` without disturbing any stored TFTP file.


# PolarFire SoC Bare-Metal Storage & TFTP Architecture Guide

This document captures the architecture, critical hardware learnings, and step-by-step file modifications required to bring up the Cadence SD/eMMC host controller on the Microchip PolarFire SoC bare-metal environment, and how to bridge it to an lwIP TFTP server for direct PC-to-eMMC file transfers.

---

# Part 1: MMC Storage Abstraction Layer (`mmc_storage.c`)

`mmc_storage.c` acts as a bare-metal storage abstraction layer that bridges byte-oriented network packets from lwIP's TFTP server to 512-byte Logical Block Address (LBA) sectors on the PolarFire SoC's eMMC/SD host controller (`mss_mmc`). 

Because the target runs without an operating system or filesystem (such as FAT32 or ext4), this module implements raw sector management, unaligned-to-aligned buffer accumulation, persistent file metadata, and incremental CRC32 integrity verification.

## 1. Disk Layout, Data Structures & State

Rather than starting at sector `0`—which would overwrite partition tables (MBR/GPT) or boot payloads stored for Hart Software Services (HSS)—the module places all storage operations at a `10 MiB` offset (sector `20480`, assuming `512` bytes per sector):

| LBA Sector | Byte Offset | Purpose |
| :--- | :--- | :--- |
| **`20479`** (`MMC_META_SECTOR - 1`) | `10,485,248` (`~10 MiB - 512 B`) | Dedicated scratch sector for the standalone UART write/read-back self-test, isolated from stored files. |
| **`20480`** (`MMC_META_SECTOR`) | `10,485,760` (`10.0 MiB`) | Persistent `512`-byte file metadata header (`mmc_file_meta_t`). |
| **`20481+`** (`MMC_DATA_START_SECTOR`) | `10,486,272+` | Contiguous raw file payload sectors (up to `MMC_MAX_FILE_BYTES = 16 MiB`). |

To persist file attributes across power cycles without a filesystem, `mmc_file_meta_t` defines an exact `512`-byte packed, 4-byte-aligned structure matching one hardware sector:

* **`magic` (`0x4D4D4346UL`):** ASCII `"MMCF"` signature used to verify whether sector `20480` holds valid file metadata or uninitialized flash data.
* **`file_size` & `crc32`:** Exact byte length of the uploaded file (excluding trailing sector padding) and its IEEE 802.3 CRC32 checksum.
* **`filename[64]` & `reserved[436]`:** Null-terminated filename string plus zeroed padding so `sizeof(mmc_file_meta_t)` equals `512` bytes, preventing out-of-bounds DMA/FIFO reads when passed to `MSS_MMC_single_block_write()`.

Several static state variables coordinate asynchronous network chunks and hardware alignment requirements:

* **`g_sector_buf[512]` (`__attribute__((aligned(8)))`):** Acts as a staging window for accumulating partial incoming network chunks into complete `512`-byte sectors and holding sectors read from flash. The 8-byte alignment is required because `MSS_MMC_single_block_write()` and `MSS_MMC_single_block_read()` cast the buffer pointer to `uint32_t *` for 32-bit FIFO/DMA transfers.
* **`g_mmc_ready` & `g_last_init_status`:** Tracks whether hardware initialization succeeded and preserves the exact `mss_mmc_status_t` enum code (`MSS_MMC_NOT_INITIALISED` by default) so diagnostics remain visible even if a serial terminal connects after boot.
* **Streaming Counters (`g_buf_used`, `g_write_sector`, `g_total_written`, `g_running_crc`):** Track the byte offset inside `g_sector_buf`, the current target LBA sector, total payload bytes received, and the running CRC32 state (`0xFFFFFFFFUL` seed) across packets.

## 2. Hardware Initialization & Multi-Stage Fallback

Bringing up eMMC or SD storage in a standalone bare-metal environment requires resetting the Cadence SD/eMMC host controller and configuring Bank 4 MSSIO electrical pads before running card identification.

### Controller Reset (`mmc_reset_block`)
Before every initialization attempt, `mmc_reset_block()` ensures `SUBBLK_CLOCK_CR_MMC_MASK` is enabled in `SYSREG->SUBBLK_CLOCK_CR`, pulses `SOFT_RESET_CR_MMC_MASK` high and then low in `SYSREG->SOFT_RESET_CR`, and executes a `250,000`-iteration `nop` loop. This clears any dirty host-controller state left over from a prior failed probe or bootloader handoff and gives the internal clock dividers time to stabilize.

### Dynamic Bank 4 Switching & Fallback Chain (`mmc_storage_init`)
On the PolarFire SoC Icicle Kit, eMMC and the microSD card slot (`J22`) share MSSIO Bank 4. `mmc_storage_init()` first queries `mss_does_xml_ver_support_switch()` to confirm the Libero hardware configuration supports runtime MSSIO switching, then executes a four-tier initialization sequence:

1. **eMMC @ 200 MHz (`HS200`):** Calls `switch_mssio_config(EMMC_MSSIO_CONFIGURATION)` to configure Bank 4 pads and `SYSREG->IOMUX6_CR` for `1.8V` 8-bit eMMC, then invokes `MSS_MMC_init()` in `MSS_MMC_MODE_HS200`.
2. **eMMC @ 50 MHz (`SDR`):** If HS200 tuning fails (for instance, due to cold JTAG clock settings), resets the controller via `mmc_reset_block()` and retries eMMC at `50 MHz` in `MSS_MMC_MODE_SDR`.
3. **microSD @ 50 MHz (`High Speed`):** If no eMMC responds, resets the block, calls `switch_mssio_config(SD_MSSIO_CONFIGURATION)` to route Bank 4 to the microSD slot (`J22`), and attempts 4-bit `MSS_MMC_CARD_TYPE_SD` initialization in `MSS_SDCARD_MODE_HIGH_SPEED`.
4. **microSD @ 50 MHz (`Default Speed`):** Resets the controller one final time and retries 4-bit SD initialization in `MSS_SDCARD_MODE_DEFAULT_SPEED`.

If all four stages fail, `g_mmc_ready` stays `false` and the function logs `status`, `xml_ok`, `sw_ok`, and `SYSREG->IOMUX6_CR` over UART for debugging.

## 3. Streaming TFTP Write & Read Pipelines

TFTP transfers files in `512`-byte data blocks (terminated by a short block of `0`–`511` bytes), and lwIP may deliver a single UDP datagram split across multiple chained `pbuf` descriptors. The streaming API decouples lwIP's packet boundaries from physical flash sector writes.

### Upload Path (`TFTP PUT` -> eMMC/SD)
1. **`mmc_file_open_write(filename)`:** Triggered on a TFTP Write Request (`WRQ`). Resets `g_buf_used` to `0`, points `g_write_sector` to `20481` (`MMC_DATA_START_SECTOR`), seeds `g_running_crc` to `0xFFFFFFFFUL`, and stores `filename` in `g_active_name`.
2. **`mmc_file_write_chunk(data, len)`:** Called for each `pbuf` payload segment. It enforces the `16 MiB` limit (`MMC_MAX_FILE_BYTES`), copies incoming bytes into `g_sector_buf` up to `512` bytes at a time, and whenever `g_buf_used == 512`, commits the full sector to flash via `MSS_MMC_single_block_write()` and increments `g_write_sector`. Simultaneously, `crc32_update()` folds the raw bytes into `g_running_crc` using the standard reflected polynomial `0xEDB88320UL`.
3. **`mmc_file_close_write()`:** Triggered when the final TFTP packet is acknowledged. If trailing bytes remain in `g_sector_buf` (`0 < g_buf_used < 512`), it zero-pads the rest of the sector and writes that final data block. It then finalizes the CRC32 (`~g_running_crc`), populates `g_cached_meta` with `MMC_FILE_MAGIC`, `g_total_written`, and `g_active_name`, and commits the metadata header to sector `20480` (`MMC_META_SECTOR`).

### Download Path (eMMC/SD -> `TFTP GET`)
1. **`mmc_file_open_read(filename, &out_size)`:** Triggered on a TFTP Read Request (`RRQ`). Reads sector `20480` into `g_cached_meta`, verifies `magic == MMC_FILE_MAGIC` and `strcmp` against `filename`, and passes `file_size` back to the TFTP server.
2. **`mmc_file_read_chunk(buf, max_len, offset)`:** Converts any byte-level `offset` and `max_len` request from lwIP into the corresponding LBA sector (`MMC_DATA_START_SECTOR + (cur_off / 512)`) and intra-sector byte offset (`cur_off % 512`), reads each `512`-byte sector into `g_sector_buf` via `MSS_MMC_single_block_read()`, and copies only the requested byte slice into `buf`.

---

# Part 2: lwIP TFTP Server Integration (`tftp_server_app.c`)

`tftp_server_app.c` acts as the "glue" logic between the lwIP network stack (specifically the built-in `lwip/apps/tftp_server.h` module) and our custom eMMC storage layer. It implements the callbacks required by lwIP to handle TFTP network events.

## 1. Connection State Tracking

Because we are implementing a bare-metal TFTP server that supports one active file transfer at a time, we use a single static struct to track the session:
```c
typedef struct {
    uint8_t  is_write;
    uint32_t file_size;
    uint32_t offset;
} tftp_mmc_handle_t;
static tftp_mmc_handle_t g_tftp_handle;
```
* **`is_write`**: Identifies if the current transfer is a `PUT` (client uploading to the board) or a `GET` (client downloading from the board).
* **`file_size` & `offset`**: Used during a `GET` request to keep track of how many bytes have been successfully transmitted back to the client over the network.

## 2. The lwIP Callbacks (`g_tftp_ctx`)

lwIP manages the UDP network sockets, timeouts, and TFTP protocol state machines (like waiting for ACKs and handling block numbers). When it needs to access the actual file data, it fires the callbacks defined in `g_tftp_ctx`.

### `tftp_mmc_open()`
Triggered when the board receives a TFTP **WRQ** (Write Request) or **RRQ** (Read Request) packet.
* It clears the `g_tftp_handle` state.
* If it's a write, it calls `mmc_file_open_write()`. If the eMMC is uninitialized, this returns `false`, causing `tftp_mmc_open` to return `NULL`. lwIP sees this `NULL` and automatically sends a TFTP Error packet back to the client, aborting the connection gracefully.
* If it's a read, it calls `mmc_file_open_read()`, fetching the target file size from the metadata sector.

### `tftp_mmc_write()`
Triggered during an active `PUT` when a valid TFTP data packet arrives.
* **The `pbuf` Chain:** In lwIP, network packets are not always stored in a single continuous block of memory. They can be split across a linked list of buffer structures called `pbuf`s. 
* The `for (struct pbuf *q = p; q != NULL; q = q->next)` loop iterates through this chain, extracting the raw payload data from each segment and streaming it into `mmc_file_write_chunk()`.
* Returning `< 0` tells lwIP that a storage error occurred, which will abort the TFTP session.

### `tftp_mmc_read()`
Triggered during an active `GET` when lwIP needs the next block of data to send to the client.
* It passes the requested size and current `h->offset` down to `mmc_file_read_chunk()`, which handles fetching the correct 512-byte hardware sectors from the eMMC and pulling the exact requested byte slice.
* Once the data is copied to lwIP's network buffer, `h->offset` is incremented.

### `tftp_mmc_close()`
Triggered when the TFTP session finishes, either successfully (the final block was ACK'd) or unsuccessfully (a timeout occurred).
* If the transfer was a write operation, it calls `mmc_file_close_write()`. This is a critical step because it flushes any remaining partial data in the `512`-byte staging buffer to flash, computes the final CRC32, and commits the `mmc_file_meta_t` header to sector `20480`.

## 3. Server Initialization

`tftp_server_app_init()` simply calls `tftp_init_server(&g_tftp_ctx)`. This instructs the lwIP stack to bind a UDP socket to Port 69 and begin listening for incoming TFTP packets, dispatching them to the hardware storage functions we defined.



# PolarFire SoC Bare-Metal Storage & TFTP Architecture Guide

This document captures the architecture, critical hardware learnings, and step-by-step file modifications required to bring up the Cadence SD/eMMC host controller on the Microchip PolarFire SoC bare-metal environment, and how to bridge it to an lwIP TFTP server for direct PC-to-eMMC file transfers.

# Part 1: MMC Storage Abstraction Layer (`mmc_storage.c`)

`mmc_storage.c` acts as a bare-metal storage abstraction layer that bridges byte-oriented network packets from lwIP's TFTP server to 512-byte Logical Block Address (LBA) sectors on the PolarFire SoC's eMMC/SD host controller (`mss_mmc`).

Because the target runs without an operating system or filesystem (such as FAT32 or ext4), this module implements raw sector management, unaligned-to-aligned buffer accumulation, persistent file metadata, and incremental CRC32 integrity verification.

## 1. Disk Layout, Data Structures & State

Rather than starting at sector `0`—which would overwrite partition tables (MBR/GPT) or boot payloads stored for Hart Software Services (HSS)—the module places all storage operations at a `10 MiB` offset (sector `20480`, assuming `512` bytes per sector):

| LBA Sector | Byte Offset | Purpose | 
| ----- | ----- | ----- | 
| **`20479`** (`MMC_META_SECTOR - 1`) | `10,485,248` (`~10 MiB - 512 B`) | Dedicated scratch sector for the standalone UART write/read-back self-test, isolated from stored files. | 
| **`20480`** (`MMC_META_SECTOR`) | `10,485,760` (`10.0 MiB`) | Persistent `512`-byte file metadata header (`mmc_file_meta_t`). | 
| **`20481+`** (`MMC_DATA_START_SECTOR`) | `10,486,272+` | Contiguous raw file payload sectors (up to `MMC_MAX_FILE_BYTES = 16 MiB`). | 

To persist file attributes across power cycles without a filesystem, `mmc_file_meta_t` defines an exact `512`-byte packed, 4-byte-aligned structure matching one hardware sector:

* **`magic` (`0x4D4D4346UL`):** ASCII `"MMCF"` signature used to verify whether sector `20480` holds valid file metadata or uninitialized flash data.

* **`file_size` & `crc32`:** Exact byte length of the uploaded file (excluding trailing sector padding) and its IEEE 802.3 CRC32 checksum.

* **`filename[64]` & `reserved[436]`:** Null-terminated filename string plus zeroed padding so `sizeof(mmc_file_meta_t)` equals `512` bytes, preventing out-of-bounds DMA/FIFO reads when passed to `MSS_MMC_single_block_write()`.

Several static state variables coordinate asynchronous network chunks and hardware alignment requirements:

* **`g_sector_buf[512]` (`__attribute__((aligned(8)))`):** Acts as a staging window for accumulating partial incoming network chunks into complete `512`-byte sectors and holding sectors read from flash. The 8-byte alignment is required because `MSS_MMC_single_block_write()` and `MSS_MMC_single_block_read()` cast the buffer pointer to `uint32_t *` for 32-bit FIFO/DMA transfers.

* **`g_mmc_ready` & `g_last_init_status`:** Tracks whether hardware initialization succeeded and preserves the exact `mss_mmc_status_t` enum code (`MSS_MMC_NOT_INITIALISED` by default) so diagnostics remain visible even if a serial terminal connects after boot.

* **Streaming Counters (`g_buf_used`, `g_write_sector`, `g_total_written`, `g_running_crc`):** Track the byte offset inside `g_sector_buf`, the current target LBA sector, total payload bytes received, and the running CRC32 state (`0xFFFFFFFFUL` seed) across packets.

## 2. Hardware Initialization & Multi-Stage Fallback

Bringing up eMMC or SD storage in a standalone bare-metal environment requires resetting the Cadence SD/eMMC host controller and configuring Bank 4 MSSIO electrical pads before running card identification.

### Controller Reset (`mmc_reset_block`)

Before every initialization attempt, `mmc_reset_block()` ensures `SUBBLK_CLOCK_CR_MMC_MASK` is enabled in `SYSREG->SUBBLK_CLOCK_CR`, pulses `SOFT_RESET_CR_MMC_MASK` high and then low in `SYSREG->SOFT_RESET_CR`, and executes a `250,000`-iteration `nop` loop. This clears any dirty host-controller state left over from a prior failed probe or bootloader handoff and gives the internal clock dividers time to stabilize.

### Dynamic Bank 4 Switching & Fallback Chain (`mmc_storage_init`)

On the PolarFire SoC Icicle Kit, eMMC and the microSD card slot (`J22`) share MSSIO Bank 4. `mmc_storage_init()` first queries `mss_does_xml_ver_support_switch()` to confirm the Libero hardware configuration supports runtime MSSIO switching, then executes a four-tier initialization sequence:

1. **eMMC @ 200 MHz (`HS200`):** Calls `switch_mssio_config(EMMC_MSSIO_CONFIGURATION)` to configure Bank 4 pads and `SYSREG->IOMUX6_CR` for `1.8V` 8-bit eMMC, then invokes `MSS_MMC_init()` in `MSS_MMC_MODE_HS200`.

2. **eMMC @ 50 MHz (`SDR`):** If HS200 tuning fails (for instance, due to cold JTAG clock settings), resets the controller via `mmc_reset_block()` and retries eMMC at `50 MHz` in `MSS_MMC_MODE_SDR`.

3. **microSD @ 50 MHz (`High Speed`):** If no eMMC responds, resets the block, calls `switch_mssio_config(SD_MSSIO_CONFIGURATION)` to route Bank 4 to the microSD slot (`J22`), and attempts 4-bit `MSS_MMC_CARD_TYPE_SD` initialization in `MSS_SDCARD_MODE_HIGH_SPEED`.

4. **microSD @ 50 MHz (`Default Speed`):** Resets the controller one final time and retries 4-bit SD initialization in `MSS_SDCARD_MODE_DEFAULT_SPEED`.

If all four stages fail, `g_mmc_ready` stays `false` and the function logs `status`, `xml_ok`, `sw_ok`, and `SYSREG->IOMUX6_CR` over UART for debugging.

## 3. Streaming TFTP Write & Read Pipelines

TFTP transfers files in `512`-byte data blocks (terminated by a short block of `0`–`511` bytes), and lwIP may deliver a single UDP datagram split across multiple chained `pbuf` descriptors. The streaming API decouples lwIP's packet boundaries from physical flash sector writes.

### Upload Path (`TFTP PUT` -> eMMC/SD)

1. **`mmc_file_open_write(filename)`:** Triggered on a TFTP Write Request (`WRQ`). Resets `g_buf_used` to `0`, points `g_write_sector` to `20481` (`MMC_DATA_START_SECTOR`), seeds `g_running_crc` to `0xFFFFFFFFUL`, and stores `filename` in `g_active_name`.

2. **`mmc_file_write_chunk(data, len)`:** Called for each `pbuf` payload segment. It enforces the `16 MiB` limit (`MMC_MAX_FILE_BYTES`), copies incoming bytes into `g_sector_buf` up to `512` bytes at a time, and whenever `g_buf_used == 512`, commits the full sector to flash via `MSS_MMC_single_block_write()` and increments `g_write_sector`. Simultaneously, `crc32_update()` folds the raw bytes into `g_running_crc` using the standard reflected polynomial `0xEDB88320UL`.

3. **`mmc_file_close_write()`:** Triggered when the final TFTP packet is acknowledged. If trailing bytes remain in `g_sector_buf` (`0 < g_buf_used < 512`), it zero-pads the rest of the sector and writes that final data block. It then finalizes the CRC32 (`~g_running_crc`), populates `g_cached_meta` with `MMC_FILE_MAGIC`, `g_total_written`, and `g_active_name`, and commits the metadata header to sector `20480` (`MMC_META_SECTOR`).

### Download Path (eMMC/SD -> `TFTP GET`)

1. **`mmc_file_open_read(filename, &out_size)`:** Triggered on a TFTP Read Request (`RRQ`). Reads sector `20480` into `g_cached_meta`, verifies `magic == MMC_FILE_MAGIC` and `strcmp` against `filename`, and passes `file_size` back to the TFTP server.

2. **`mmc_file_read_chunk(buf, max_len, offset)`:** Converts any byte-level `offset` and `max_len` request from lwIP into the corresponding LBA sector (`MMC_DATA_START_SECTOR + (cur_off / 512)`) and intra-sector byte offset (`cur_off % 512`), reads each `512`-byte sector into `g_sector_buf` via `MSS_MMC_single_block_read()`, and copies only the requested byte slice into `buf`.

# Part 2: lwIP TFTP Server Integration (`tftp_server_app.c`)

`tftp_server_app.c` acts as the "glue" logic between the lwIP network stack (specifically the built-in `lwip/apps/tftp_server.h` module) and our custom eMMC storage layer. It implements the callbacks required by lwIP to handle TFTP network events.

## 1. Connection State Tracking

Because we are implementing a bare-metal TFTP server that supports one active file transfer at a time, we use a single static struct to track the session:

```c
typedef struct {
    uint8_t  is_write;
    uint32_t file_size;
    uint32_t offset;
} tftp_mmc_handle_t;
static tftp_mmc_handle_t g_tftp_handle;
```

* **`is_write`**: Identifies if the current transfer is a `PUT` (client uploading to the board) or a `GET` (client downloading from the board).
* **`file_size` & `offset`**: Used during a `GET` request to keep track of how many bytes have been successfully transmitted back to the client over the network.

## 2. The lwIP Callbacks (`g_tftp_ctx`)

lwIP manages the UDP network sockets, timeouts, and TFTP protocol state machines (like waiting for ACKs and handling block numbers). When it needs to access the actual file data, it fires the callbacks defined in `g_tftp_ctx`.

* **`tftp_mmc_open()`**: Triggered on WRQ/RRQ. Clears state and calls the underlying `mmc_file_open_*` logic. Returning `NULL` gracefully aborts the connection.
* **`tftp_mmc_write()`**: Iterates through the chained `pbuf` structures (lwIP's network buffer linked lists) and passes the raw payloads into `mmc_file_write_chunk()`.
* **`tftp_mmc_read()`**: Pulls the exact byte slice requested by lwIP out of the eMMC via `mmc_file_read_chunk()`.
* **`tftp_mmc_close()`**: Flushes partial staging buffers and commits the `mmc_file_meta_t` header to the metadata sector.

# Part 3: Ethernet MAC & lwIP Network Interface (`net_port.c`)

`net_port.c` is the core Hardware Abstraction Layer (HAL) that bridges the PolarFire SoC's Gigabit Ethernet MAC blocks (`mss_mac`) to lwIP's generic `netif` (Network Interface) structure. 

## 1. Data Structures & Configuration

The networking subsystem is highly configurable to support multiple active Ethernet ports (e.g., GEM0 and GEM1 on the Icicle Kit). 
* **`net_port_hw_t`**: Holds static hardware configuration injected from `board_config.h` (e.g., PHY addresses, MAC addresses, interrupts, and static IP fallbacks). 
* **`net_port_ctx_t`**: Tracks the dynamic runtime state for each port. Crucially, it manages the interrupt-driven RX raw queue, the DMA-aligned TX/RX ring buffers (`mac_rx_buffer` and `mac_tx_buffer`), and lwIP's `struct netif`.

## 2. Safe Interrupt Bridging (The RX Path)

In a bare-metal environment, allocating memory (like lwIP's `pbuf_alloc`) inside a fast-firing hardware interrupt service routine (ISR) can lead to heap corruption or stack overflows. `net_port.c` safely bridges this gap:

1. **The ISR (`mac_rx_callback`)**: When a packet arrives, the MAC triggers this interrupt. The ISR does *not* touch lwIP. Instead, it copies the raw packet into a fast, circular lock-free queue (`rx_raw_queue`), advances the `rx_q_head` pointer, and immediately hands the DMA buffer back to the MAC controller (`MSS_MAC_receive_pkt`).
2. **The Main Loop (`net_port_poll`)**: Running safely in the main application loop (`while(1)`), this function drains the `rx_raw_queue`. For each pending packet, it safely allocates an lwIP `pbuf`, copies the data from the queue into the `pbuf`, and passes it up to lwIP via `ctx->netif.input()`.

## 3. Transmission (The TX Path)

When lwIP needs to send a packet (TCP, UDP, TFTP, Ping), it calls `low_level_output()`. 
* Because lwIP `pbuf` payloads might not be perfectly aligned for the PolarFire SoC's DMA engine, the function copies the payload into a strictly 16-byte aligned slot in `mac_tx_buffer`.
* It enforces the Ethernet standard minimum frame size (padding payloads smaller than 60 bytes with zeros).
* It temporarily disables MAC interrupts (`PLIC_DisableIRQ`), kicks the hardware transmission (`MSS_MAC_send_pkt`), and re-enables interrupts.

## 4. DHCP & Link State Machine

`net_port_poll()` acts as the heartbeat of the network stack. Every cycle, it checks the physical PHY link status (Up/Down) and negotiates speed (e.g., 1000 Mbps Full-Duplex). 
When a link comes UP:
1. It informs lwIP (`netif_set_link_up`).
2. It starts a DHCP discovery broadcast to acquire an IP address automatically.
3. If no DHCP lease is acquired within `DHCP_FALLBACK_MS` (8000 milliseconds), the port gracefully stops the DHCP client and falls back to the hardcoded static IP defined in `board_config.h`. 

# Part 4: HTTP Web Server & CGI Handling (`web_server.c`)

`web_server.c` initializes lwIP's lightweight, built-in HTTP daemon (`httpd`). While standard files (like `index.html`) can be served using lwIP's ROM filesystem (`fsdata.c`), dynamic interaction is handled through CGI (Common Gateway Interface).

## CGI Routing (`cgi_ctrl_handler`)

When the web server is initialized (`http_set_cgi_handlers`), we register a route mapping `/ctrl.cgi` to the `cgi_ctrl_handler` function.

If a user submits a form or makes a GET request to the board (e.g., `http://192.168.20.51/ctrl.cgi?led=1&speed=100`), lwIP parses the URL and triggers this callback. 
* The handler iterates through the `iNumParams` provided.
* It extracts the keys (`pcParam`) and values (`pcValue`) and logs them to the UART console.
* Finally, it returns `"/index.html"`, instructing the HTTP server to redirect the client back to the main webpage. 