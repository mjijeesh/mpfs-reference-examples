# PolarFire SoC Bare-Metal Storage & TFTP Architecture Guide

This document captures the architecture, critical hardware learnings, and step-by-step build, load, and test procedures required to bring up the Cadence SD/eMMC host controller (`mss_mmc`) on Microchip PolarFire SoC boards (**Icicle Kit** and **Discovery Kit**) in a bare-metal environment, and bridge it to an lwIP TFTP server for direct PC-to-flash file transfers.

---

# Part 1: Hardware Differences & Key Learnings

## 1. Board Hardware Comparison (`Icicle Kit` vs. `Discovery Kit`)

While both boards use the PolarFire SoC MSS Cadence SD/eMMC controller, their physical storage routing, Ethernet PHYs, and USB-UART mappings differ:

| Feature | PolarFire SoC Icicle Kit (`mpfs-icicle-kit`) | PolarFire SoC Discovery Kit (`mpfs-disco-kit`) |
| :--- | :--- | :--- |
| **Onboard eMMC** | Yes (8-bit, 1.8V, up to 200 MHz HS200) | **No** (no eMMC chip populated) |
| **microSD Card Slot** | Yes (`J22`, 4-bit, up to 50 MHz High Speed) | Yes (4-bit, up to 50 MHz High Speed) |
| **Bank 4 MSSIO Muxing** | Shared between eMMC and microSD (`J22`); switched at runtime via `switch_mssio_config()` | Dedicated to microSD slot (no runtime mux switch needed) |
| **Ethernet Interfaces** | Dual Gigabit Ethernet (`e0` = GEM1 J2, `e1` = GEM0 J1) | Single Gigabit Ethernet (`e0` = GEM0/GEM1 RJ45) |
| **HSS Console UART** | `MMUART0` (`/dev/ttyUSB0` @ 115200 baud) | `MMUART0` (first USB-UART port, e.g., `/dev/ttyUSB0` or `/dev/ttyACM0`) |
| **App Debug UART** | `MMUART1` (`/dev/ttyUSB1` @ 115200 baud) | `MMUART1` (second USB-UART port, e.g., `/dev/ttyUSB1` or `/dev/ttyACM1`) |

## 2. Critical Bare-Metal Bring-Up Requirements

1. **Mandatory Controller Reset Pulse (`mmc_reset_block`):**
   Enabling the peripheral clock alone is insufficient when taking over from JTAG (`lim-debug`) or HSS (`ddr-release`). Before every initialization attempt, the driver must assert and de-assert `SOFT_RESET_CR_MMC_MASK` while keeping `SUBBLK_CLOCK_CR_MMC_MASK` set, followed by a `250,000`-iteration `nop` spin (~50 ms) so the internal SD/eMMC clock dividers stabilize.
2. **Dynamic Bank 4 MSSIO Pad Switching (`switch_mssio_config`):**
   On the Icicle Kit, Bank 4 MSSIO electrical pads and `SYSREG->IOMUX6_CR` must be switched between `EMMC_MSSIO_CONFIGURATION` (1.8V, 8-bit) and `SD_MSSIO_CONFIGURATION` (4-bit) prior to calling `MSS_MMC_init()`. On boards without a runtime switch (such as the Discovery Kit), `mss_does_xml_ver_support_switch()` returns `false` and the switch call is safely bypassed.
3. **Media-Specific Speed Modes:**
   * **eMMC:** `MSS_MMC_MODE_HS200` (200 MHz) with fallback to `MSS_MMC_MODE_SDR` (50 MHz).
   * **SD Card:** `MSS_SDCARD_MODE_HIGH_SPEED` (50 MHz) with fallback to `MSS_SDCARD_MODE_DEFAULT_SPEED` (50 MHz).

---

# Part 2: Storage & Network Software Architecture

## 1. MMC Storage Abstraction Layer (`middleware/net_stack/mmc_storage.c`)

Because the bare-metal target runs without an OS or filesystem (such as FAT32 or ext4), `mmc_storage.c` bridges byte-stream network chunks to 512-byte Logical Block Address (LBA) sectors starting at a `10 MiB` offset (preserving sector `0` MBR/GPT tables and HSS boot partitions):

| LBA Sector | Byte Offset | Purpose |
| :--- | :--- | :--- |
| **`20479`** (`MMC_META_SECTOR - 1`) | `10,485,248` (`~10 MiB - 512 B`) | Dedicated scratch sector for the non-destructive UART `[w]` self-test. |
| **`20480`** (`MMC_META_SECTOR`) | `10,485,760` (`10.0 MiB`) | Persistent `512`-byte file metadata header (`mmc_file_meta_t` with `"MMCF"` magic, byte size, IEEE 802.3 CRC32, and filename). |
| **`20481+`** (`MMC_DATA_START_SECTOR`) | `10,486,272+` | Contiguous raw file payload sectors (up to `MMC_MAX_FILE_BYTES = 16 MiB`). |

* **Dual-Media Initialization (`mmc_storage_init_media(bool use_emmc)`):**
  * When `use_emmc == true`, probes eMMC (`200 MHz HS200` $\rightarrow$ `50 MHz SDR`) and automatically falls back to microSD (`50 MHz HS` $\rightarrow$ `50 MHz DS`) if no eMMC is present (allowing Discovery Kit to initialize SD automatically even if `APP_MMC_USE_EMMC=1`).
  * When `use_emmc == false`, probes the microSD slot directly.
  * Tracks the active medium dynamically in `g_active_is_emmc` so all CLI diagnostics report the true active hardware (`eMMC` vs. `SD Card`).
* **Sector Staging & Streaming:** Accumulates arbitrary incoming network byte slices into an 8-byte aligned `512`-byte buffer (`g_sector_buf`), commits full sectors via `MSS_MMC_single_block_write()`, zero-pads any trailing partial sector on close, and writes `mmc_file_meta_t` to sector `20480`.

## 2. lwIP TFTP Server Glue (`middleware/net_stack/tftp_server_app.c`)

Binds lwIP's UDP port `69` TFTP daemon (`tftp_init_server`) to the storage layer via `g_tftp_ctx`:
* **`tftp_mmc_open()`:** Dispatches `WRQ` (upload) to `mmc_file_open_write()` and `RRQ` (download) to `mmc_file_open_read()`.
* **`tftp_mmc_write()`:** Walks lwIP's linked `pbuf` chain and streams each segment into `mmc_file_write_chunk()`.
* **`tftp_mmc_read()`:** Fetches requested byte slices from flash via `mmc_file_read_chunk()`.
* **`tftp_mmc_close()`:** Flushes trailing bytes and commits the persistent metadata sector via `mmc_file_close_write()`.

## 3. Ethernet MAC Driver (`middleware/net_stack/net_port.c`)

Bridges `mss_mac` Gigabit Ethernet controllers to lwIP `netif` interfaces:
* **Lock-Free ISR-to-Main-Loop RX Queue:** `mac_rx_callback()` copies incoming frames into `rx_raw_queue` inside the MAC ISR without calling `pbuf_alloc()`; `net_port_poll()` drains the queue safely in the main loop.
* **Aligned DMA TX Ring:** `low_level_output()` flattens chained `pbuf`s into 16-byte aligned TX slots and pads frames to the `60`-byte Ethernet minimum.
* **DHCP with Static Fallback:** Starts DHCP discovery on link-up and automatically applies the board's static IPv4 configuration after `8000 ms` (`DHCP_FALLBACK_MS`) if no DHCP server responds.

## 4. HTTP Web Server (`middleware/net_stack/web_server.c`)

Starts lwIP's `httpd` on TCP port `80` and registers `/ctrl.cgi` (`cgi_ctrl_handler`) to log URL query parameters over `MMUART1` and redirect back to `/index.html`.

---

# Part 3: Building the Firmware

Run all build commands from the repository root directory.

## 1. PolarFire SoC Icicle Kit (`BOARD=mpfs-icicle-kit`)

* **JTAG Debug Build (runs from L2 LIM):**
  ```bash
  make BOARD=mpfs-icicle-kit clean lim-debug
  ```
  *Output ELF:* `build/mpfs-icicle-kit/lim-debug/mpfs-netapp.elf`

* **HSS Bootloader Release Build (runs from DDR):**
  ```bash
  make BOARD=mpfs-icicle-kit clean ddr-release
  ```
  *Output Payload:* `build/mpfs-icicle-kit/ddr-release/mpfs-net-app_payload.bin`

## 2. PolarFire SoC Discovery Kit (`BOARD=mpfs-disco-kit`)

*(Optional)* In `boards/mpfs-disco-kit/board_config.h`, ensure `#define APP_MMC_USE_EMMC 0` is set so boot skips eMMC probing and initializes the microSD card directly (if left at `1`, it will probe eMMC first and automatically fall back to the microSD card).

* **JTAG Debug Build (runs from L2 LIM):**
  ```bash
  make BOARD=mpfs-disco-kit clean lim-debug
  ```
  *Output ELF:* `build/mpfs-disco-kit/lim-debug/mpfs-netapp.elf`

* **HSS Bootloader Release Build (runs from DDR):**
  ```bash
  make BOARD=mpfs-disco-kit clean ddr-release
  ```
  *Output Payload:* `build/mpfs-disco-kit/ddr-release/mpfs-net-app_payload.bin`

---

# Part 4: Loading Firmware onto the Board

Choose **Method A** (for JTAG source-level debugging without HSS) or **Method B** (for deploying release builds via the HSS bootloader).

## Method A: JTAG Debugging (`OpenOCD` + `GDB` + `UART`)

Open **three separate terminals** on your host PC:

1. **Terminal 1 — Application UART Console (`MMUART1`):**
   Start the terminal listener *before* launching GDB so you capture early boot logs:
   ```bash
   # Icicle Kit (MMUART1 is typically /dev/ttyUSB1)
   python3 mpfs-terminal.py /dev/ttyUSB1

   # Discovery Kit (MMUART1 is the second enumerated UART port, e.g., /dev/ttyUSB1 or /dev/ttyACM1)
   python3 mpfs-terminal.py /dev/ttyUSB1
   ```

2. **Terminal 2 — OpenOCD JTAG Server:**
   * **For Icicle Kit:**
     ```bash
     openocd -f board/microsemi-icicle-kit-es.cfg
     ```
   * **For Discovery Kit:**
     ```bash
     openocd -f board/microchip-mpfs-disco-kit.cfg
     ```
   *(Verify OpenOCD reports `Listening on port 3333 for gdb connections`.)*

3. **Terminal 3 — RISC-V GDB Client:**
   * **For Icicle Kit:**
     ```bash
     riscv64-unknown-elf-gdb build/mpfs-icicle-kit/lim-debug/mpfs-netapp.elf
     ```
   * **For Discovery Kit:**
     ```bash
     riscv64-unknown-elf-gdb build/mpfs-disco-kit/lim-debug/mpfs-netapp.elf
     ```
   At the `(gdb)` prompt, connect, load into LIM, and start execution:
   ```gdb
   (gdb) target extended-remote localhost:3333
   (gdb) load
   (gdb) continue
   ```

## Method B: Loading via HSS Bootloader (`ddr-release`)

1. **Open two terminals** (one for `MMUART0` HSS console, one for `MMUART1` Application console):
   * **Terminal 1 (`MMUART0` — HSS Bootloader):**
     ```bash
     python3 mpfs-terminal.py /dev/ttyUSB0
     ```
   * **Terminal 2 (`MMUART1` — Application Output):**
     ```bash
     python3 mpfs-terminal.py /dev/ttyUSB1
     ```
2. **Reset the board.** In **Terminal 1 (`MMUART0`)**, press any key when prompted to interrupt autoboot and reach the HSS `>>` prompt.
3. **Trigger `serialboot` (YMODEM transfer):**
   * Copy or point `mpfs-terminal.py` to the generated payload (`mpfs-net-app_payload.bin`), then type at the HSS prompt:
     ```text
     >> serialboot
     ```
   * `mpfs-terminal.py` automatically transfers the payload over YMODEM into DDR memory, and HSS boots Hart 1 (`u54_1`). All application output immediately appears in **Terminal 2 (`MMUART1`)**.

---

# Part 5: Step-by-Step Testing & Expected Outputs

## Interactive UART CLI Reference (`MMUART1`)

Once the application reaches `[APP] Ready`, the following single-key commands are active on `MMUART1`:

| Key | Action |
| :--- | :--- |
| **`s`** | Print Ethernet link state, negotiated speed/duplex, MAC, IPv4 address, and RX/TX packet counts |
| **`w`** | Run a 512-byte write + read-back verification self-test at scratch sector `20479` |
| **`m`** | Read metadata sector `20480` and display active storage medium, filename, size, and CRC32 |
| **`e`** | Switch Bank 4 MSSIO mux to onboard **eMMC** and initialize (Icicle Kit) |
| **`c`** | Switch Bank 4 MSSIO mux to **microSD Card** and initialize |
| **`h`** | Print the CLI help banner |

---

## 5.1 Icicle Kit Step-by-Step Verification (`eMMC` + `microSD J22`)

### Prerequisites
* Connect an Ethernet cable from your host PC (or switch) to **J2 (`GEM1` / `e0`)** on the Icicle Kit.
* Insert a formatted/scratch **microSD card into slot `J22`** before booting so both eMMC and SD can be tested back-to-back.

### Step 1: Verify Boot & eMMC Initialization
Observe **Terminal 2 (`MMUART1`)** on boot:
```text
========================================================
 PolarFire SoC Icicle Kit - Bare-Metal lwIP + eMMC/SD Demo
 Entry @ 0x0080000000 | Boot T+142 ms | Press 'h' for help
========================================================
[INIT 1/4] SYSREG CLOCK_CR=0x0003ffff  SOFT_RESET_CR=0x00000000
[INIT 2/4] Initializing eMMC / SD Storage...
[MMC] eMMC initialized (200 MHz, 8-bit, sector=512 B)
[INIT 3/4] Initializing Ethernet & lwIP Stack...
[MAC e0] GEM1 TBI/SGMII + VSC8662 (J2) ready (RX ring=8, avail_rx=8, avail_tx=4)
[DHCP e0] Started DHCP discovery (8s timeout -> static fallback)...
[MAC e1] GEM0 TBI/SGMII + VSC8662 (J1) ready (RX ring=8, avail_rx=8, avail_tx=4)
[DHCP e1] Started DHCP discovery (8s timeout -> static fallback)...
[INIT 4/4] Starting HTTP (TCP/80) & TFTP-to-MMC (UDP/69) @ T+185 ms...
[APP] Ready. Press 'w' for local eMMC test or send a file via TFTP.

[PHY e0] Link UP -> 1000 Mbps Full-Duplex
[DHCP e0] Lease acquired!
[NET e0 @ T+2410 ms] GEM1 TBI/SGMII + VSC8662 (J2)
[NET e0] Link : UP (1000 Mbps, Full-Duplex)
[NET e0] MAC  : 00:04:a3:11:22:32 (PHY addr 9)
[NET e0] IP   : 192.168.20.52 | Mask: 255.255.255.0 | GW: 192.168.20.1
[NET e0] Pkts : RX=4  TX=2
```
*(If no DHCP server is on the network, after 8 seconds `e0` prints `[DHCP e0] Timed out -> applied static IPv4.` and assigns its configured static IP.)*

### Step 2: Run Local eMMC Sector Self-Test
In **Terminal 2 (`MMUART1`)**, press **`w`**:
```text
[MMC-TEST] PASS (eMMC): Wrote & verified 512 B at sector 20479
```

### Step 3: Transfer & Verify a File over TFTP to eMMC
From a terminal on your host PC (replace `192.168.20.52` with the IP shown for `e0`):
```bash
# 1. Upload a binary/text file to the board's eMMC
tftp -m octet 192.168.20.52 -c put Makefile

# 2. Download the file back from the board's eMMC
tftp -m octet 192.168.20.52 -c get Makefile /tmp/Makefile.emmc.rx

# 3. Verify byte-for-byte integrity
diff -s Makefile /tmp/Makefile.emmc.rx
```
*Expected PC Output:*
```text
Files Makefile and /tmp/Makefile.emmc.rx are identical
```
*Expected `MMUART1` Output (also press **`m`** to inspect metadata):*
```text
[MMC] Saved "Makefile" to eMMC (8412 bytes, CRC32=0x9a4f12c8)

[MMC] Media : eMMC | Stored File: "Makefile" (8412 bytes, CRC32=0x9a4f12c8 @ sector 20481)
```

### Step 4: Hot-Switch to microSD Card (`J22`) and Test
1. In **Terminal 2 (`MMUART1`)**, press **`c`** to switch Bank 4 MSSIO to the microSD slot:
   ```text
   [CLI] Switching Bank 4 mux to microSD slot J22...
   [MMC] SD Card initialized (50 MHz, HS, 4-bit, sector=512 B)
   ```
2. Press **`w`** to verify block write/read on the SD card:
   ```text
   [MMC-TEST] PASS (SD Card): Wrote & verified 512 B at sector 20479
   ```
3. Run a TFTP round-trip to the microSD card from your host PC:
   ```bash
   tftp -m octet 192.168.20.52 -c put README.md
   tftp -m octet 192.168.20.52 -c get README.md /tmp/README.sd.rx
   diff -s README.md /tmp/README.sd.rx
   ```
   *Expected `MMUART1` Output:*
   ```text
   [MMC] Saved "README.md" to SD Card (12640 bytes, CRC32=0x4b7e3011)
   ```
4. Press **`e`** at any time to switch back to onboard eMMC:
   ```text
   [CLI] Switching Bank 4 mux to onboard eMMC...
   [MMC] eMMC initialized (200 MHz, 8-bit, sector=512 B)
   ```

---

## 5.2 Discovery Kit Step-by-Step Verification (`microSD Slot`)

### Prerequisites
* Insert a microSD card into the Discovery Kit's microSD card slot **before powering on or booting** (the hardware card-detect switch prevents initialization if the slot is empty).
* Connect an Ethernet cable between your host PC and the Discovery Kit's RJ45 Gigabit Ethernet jack (`e0`).

### Step 1: Verify Boot & SD Card Initialization
Observe **Terminal 2 (`MMUART1`)** on boot:
```text
========================================================
 PolarFire SoC Discovery Kit - Bare-Metal lwIP + eMMC/SD Demo
 Entry @ 0x0080000000 | Boot T+138 ms | Press 'h' for help
========================================================
[INIT 1/4] SYSREG CLOCK_CR=0x0003ffff  SOFT_RESET_CR=0x00000000
[INIT 2/4] Initializing eMMC / SD Storage...
[MMC] SD Card initialized (50 MHz, HS, 4-bit, sector=512 B)
[INIT 3/4] Initializing Ethernet & lwIP Stack...
[MAC e0] GEM0/GEM1 + RTL8211F ready (RX ring=8, avail_rx=8, avail_tx=4)
[DHCP e0] Started DHCP discovery (8s timeout -> static fallback)...
[INIT 4/4] Starting HTTP (TCP/80) & TFTP-to-MMC (UDP/69) @ T+179 ms...
[APP] Ready. Press 'w' for local eMMC test or send a file via TFTP.

[PHY e0] Link UP -> 1000 Mbps Full-Duplex
[DHCP e0] Lease acquired!
[NET e0 @ T+2350 ms] GEM0/GEM1 + RTL8211F
[NET e0] Link : UP (1000 Mbps, Full-Duplex)
[NET e0] MAC  : 00:04:a3:11:22:30 (PHY addr 1)
[NET e0] IP   : 192.168.20.50 | Mask: 255.255.255.0 | GW: 192.168.20.1
[NET e0] Pkts : RX=3  TX=2
```

### Step 2: Run Local SD Card Sector Self-Test
In **Terminal 2 (`MMUART1`)**, press **`w`**:
```text
[MMC-TEST] PASS (SD Card): Wrote & verified 512 B at sector 20479
```

### Step 3: Transfer & Verify a File over TFTP to SD Card
From your host PC terminal (replace `192.168.20.50` with the IP printed for `e0`):
```bash
# 1. Upload a file to the Discovery Kit's microSD card
tftp -m octet 192.168.20.50 -c put Makefile

# 2. Download the file back from the microSD card
tftp -m octet 192.168.20.50 -c get Makefile /tmp/Makefile.disco.rx

# 3. Verify bit-for-bit integrity
diff -s Makefile /tmp/Makefile.disco.rx
```
*Expected PC Output:*
```text
Files Makefile and /tmp/Makefile.disco.rx are identical
```
*Expected `MMUART1` Output (and press **`m`** to verify persistent metadata):*
```text
[MMC] Saved "Makefile" to SD Card (8412 bytes, CRC32=0x9a4f12c8)

[MMC] Media : SD Card | Stored File: "Makefile" (8412 bytes, CRC32=0x9a4f12c8 @ sector 20481)
```

---

# Part 6: Troubleshooting

* **`[MMC] SD Card init failed (status=1, ...)`:**
  * Confirm a microSD card is fully seated in the slot *before* booting or pressing **`c`**. `status=1` (`MSS_MMC_NOT_INITIALISED`) occurs immediately if the card-detect pin (`SRS9_CARD_INSERTED`) reads empty.
* **`tftp: Transfer timed out` on PC:**
  * Always pass **`-m octet`** (binary mode) to the `tftp` client; default `netascii` mode alters newline bytes and corrupts binary files/CRC32 checks.
  * Ensure your host PC's Ethernet adapter is on the same subnet as `e0` (e.g., `192.168.20.x / 24`) and UDP port `69` is not blocked by `ufw` or `iptables`.
* **Second TFTP `put` overwrites the first file:**
  * By design, this bare-metal storage layer writes raw contiguous blocks starting at sector `20481` with a single metadata block at sector `20480` (no FAT32/exFAT filesystem). Each new `tftp put` replaces the previously stored file.