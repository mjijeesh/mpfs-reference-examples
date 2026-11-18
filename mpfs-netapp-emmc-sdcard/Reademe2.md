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

## 3. Streaming TFTP Write & Read Pipelines

TFTP transfers files in `512`-byte data blocks. The streaming API decouples lwIP's packet boundaries from physical flash sector writes.

* **Upload Path (`TFTP PUT` -> eMMC/SD):** Opens the file, resets counters, and writes incoming `pbuf` chunks to `g_sector_buf`. Every time `512` bytes accumulate, it commits to flash. Upon completion, it flushes the remainder, calculates CRC32, and writes `mmc_file_meta_t`.

* **Download Path (eMMC/SD -> `TFTP GET`):** Reads the metadata to authorize the file and size, then fetches the requested 512-byte blocks from flash to satisfy lwIP's read chunks.

# Part 2: lwIP TFTP Server Integration (`tftp_server_app.c`)

`tftp_server_app.c` acts as the "glue" logic between the lwIP network stack (`lwip/apps/tftp_server.h`) and our custom eMMC storage layer.

## 1. Connection State Tracking

A static struct `tftp_mmc_handle_t` tracks the active session, logging whether it is a write (`PUT`) or read (`GET`), as well as the file size and current byte offset during a download.

## 2. The lwIP Callbacks (`g_tftp_ctx`)

* **`tftp_mmc_open()`**: Triggered on WRQ/RRQ. Clears state and calls the underlying `mmc_file_open_*` logic. Returning `NULL` gracefully aborts the connection.

* **`tftp_mmc_write()`**: Iterates through the chained `pbuf` structures (lwIP's network buffer linked lists) and passes the raw payloads into `mmc_file_write_chunk()`.

* **`tftp_mmc_read()`**: Pulls the exact byte slice requested by lwIP out of the eMMC via `mmc_file_read_chunk()`.

* **`tftp_mmc_close()`**: Flushes partial staging buffers and commits the metadata header.

# Part 3: Ethernet MAC & lwIP Network Interface (`net_port.c`)

`net_port.c` is the core Hardware Abstraction Layer (HAL) that bridges the PolarFire SoC's Gigabit Ethernet MAC blocks to lwIP's generic `netif` structure.

## 1. Safe Interrupt Bridging (The RX Path)

In a bare-metal environment, allocating memory (`pbuf_alloc`) inside an ISR can lead to heap corruption.

1. **The ISR (`mac_rx_callback`)**: Copies the raw packet into a fast, circular lock-free queue (`rx_raw_queue`) and returns the DMA buffer back to the MAC controller.

2. **The Main Loop (`net_port_poll`)**: Drains the `rx_raw_queue`, safely allocates an lwIP `pbuf`, and passes it up to lwIP via `ctx->netif.input()`.

## 2. Transmission (The TX Path)

When lwIP needs to send a packet, `low_level_output()` copies the payload into a strictly 16-byte aligned slot in `mac_tx_buffer`, enforces the minimum 60-byte Ethernet frame size, and triggers the hardware transmission via `MSS_MAC_send_pkt()`.

## 3. DHCP & Link State Machine

`net_port_poll()` monitors PHY link status. When the link comes up, it starts DHCP. If no DHCP lease is acquired within `DHCP_FALLBACK_MS` (e.g., 8 seconds), the port gracefully stops the DHCP client and applies a static IP.

# Part 4: HTTP Web Server & CGI Handling (`web_server.c`)

`web_server.c` initializes lwIP's lightweight, built-in HTTP daemon (`httpd`). While standard files are served using lwIP's ROM filesystem, dynamic interaction is handled through CGI (Common Gateway Interface).

When a user accesses `http://<board-ip>/ctrl.cgi?param=value`, lwIP parses the URL and triggers `cgi_ctrl_handler`, which logs the key-value pairs to the UART console and redirects back to `/index.html`.

# Part 5: Build, Load, and Test Procedures

This section details exactly how to build the application and run it on the board using two distinct methodologies: **JTAG debugging** (bypassing HSS) and **HSS Bootloader execution** (release payloads).

## 5.1 Compiling the Project

You will need to build the project based on your execution target.

* **For JTAG Debugging (LIM Memory):**

  ```
  make BOARD=mpfs-icicle-kit clean lim-debug
  
  ```

* **For HSS Bootloader (DDR Memory):**

  ```
  make BOARD=mpfs-icicle-kit clean ddr-release
  
  ```

## 5.2 Method A: JTAG Debugging (OpenOCD + GDB + UART)

This method is used when developing and actively stepping through code. It requires three separate terminal windows.

**Terminal 1: UART Console**
Open your serial connection to view application logs and diagnostics.

```
python3 mpfs-terminal.py /dev/ttyUSB1
# (Leave this running to capture early boot logs)

```

**Terminal 2: OpenOCD Server**
Launch the OpenOCD daemon to connect to the PolarFire SoC via the onboard FlashPro6 (JTAG).

```
openocd -f board/microsemi-icicle-kit-es.cfg

```

*(Note: If your environment provides a specific wrapper script for OpenOCD, use that instead. The terminal should print that it is listening on port 3333).*

**Terminal 3: GDB Client**
Launch the RISC-V GDB client, connect to OpenOCD, load the ELF, and run it.

```
riscv64-unknown-elf-gdb build/mpfs-icicle-kit/lim-debug/mpfs-netapp.elf

```

Inside the GDB prompt, type:

```
(gdb) target extended-remote localhost:3333
(gdb) load
(gdb) continue

```

## 5.3 Method B: Loading via HSS Bootloader

This method is used for deploying the final `ddr-release` payload without JTAG intervention. The `mpfs-terminal.py` script makes this highly automated by intercepting the HSS prompt.

**Terminal 1: HSS Console & YMODEM Transfer**
Connect the script to the main HSS console port (typically `/dev/ttyUSB1` or `/dev/ttyUSB0` depending on the board).

```
python3 mpfs-terminal.py /dev/ttyUSB1

```

Reset the board. As the HSS boots, wait for the `>>` prompt and type:

```
>> serialboot

```

`mpfs-terminal.py` will detect the `serialboot` command, automatically initiate a YMODEM transfer of your built `mpfs-net-app_payload.bin`, and the HSS will launch the application from DDR memory.

## 5.4 Expected Outputs & TFTP Testing

Regardless of how you booted the application, you should observe the following in your UART terminal:

```
[INIT 2/4] Initializing eMMC / SD Storage...
[MMC] eMMC initialized (200 MHz, 8-bit, sector=512 B)
...
[MAC e0] GEM1 TBI/SGMII + VSC8662 (J2) ready
[DHCP e0] Started DHCP discovery (8s timeout -> static fallback)...
...
[DHCP e0] Lease acquired!
[NET e0] IP   : 192.168.20.52 | Mask: 255.255.255.0 | GW: 192.168.20.1

```

**1. Local Storage Verification:**
In the UART terminal, press the `[w]` key. You should see:

```
[MMC-TEST] PASS: Wrote & verified 512 B at sector 20479

```

**2. Network TFTP Verification:**
From your host PC, open a terminal and transfer a file to the board's IP address:

```
# Push a file to the board
tftp -m octet 192.168.20.52 -c put Makefile

# Pull the file back from the board
tftp -m octet 192.168.20.52 -c get Makefile /tmp/Makefile.rx

# Verify bit-for-bit accuracy
diff -s Makefile /tmp/Makefile.rx

```

**3. Metadata Inspection:**
In the UART terminal, press the `[m]` key to read the stored file header:

```
[MMC] Media : eMMC | Stored File: "Makefile" (1024 bytes, CRC32=0x1234ABCD @ sector 20481)

```