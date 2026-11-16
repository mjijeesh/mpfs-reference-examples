# PolarFire SoC Bare-Metal Network Application (`mpfs-netapp`)

`mpfs-netapp` is a bare-metal RISC-V network application for the **Microchip PolarFire SoC Discovery Kit** (`MPFS095T` / `FCSG325`) built on `mpfs_hal` and a polling (`NO_SYS = 1`) **lwIP** TCP/IP stack. It runs natively from on-chip **L2-LIM SRAM** (both standalone via JTAG and as an HSS bootloader payload) or from external **DDR**, providing DHCP/static IPv4 networking, ICMP ping, an HTTP web server with CGI board control, and an in-memory TFTP server.

---

## 1. Architecture & Features

* **Multi-Hart Execution Model:**
  * **E51 (Monitor Hart):** In standalone debug targets (`lim-debug`), E51 executes `mss_nwc_init()` from `_start` to configure MSS PLLs, clocks, DDR, and SGMII before waking the U54 application harts. In bootloader-assisted targets (`lim-release`, `ddr-release`, and `ddr-debug`), Hart Software Services (HSS) remains resident on E51 and dispatches the U54 harts directly.
  * **U54_1 (Application Hart):** Initializes `MMUART4` stdio (`115200 8N1`), brings up GEM0 + VSC8221 PHY, and runs the cooperative lwIP polling loop (`net_port_poll()`), HTTP server, and TFTP server.
  * **U54_2 – U54_4:** Satisfy the `mpfs_hal` startup barrier and enter low-power `wfi` loops.
* **Ethernet Driver (`middleware/net_stack/net_port.c`):**
  * Drives **GEM0** (`APP_MAC`) over TBI/SGMII connected to the onboard **VSC8221** Gigabit Ethernet PHY (`MSS_MAC_VSC8221_*`) with link-speed auto-negotiation (`10/100/1000 Mbps`, Full/Half Duplex).
  * Decouples PLIC interrupt context from lwIP by copying received frames inside `mac_rx_callback()` into a lock-free ring (`g_rx_raw_queue`) and draining them into `PBUF_POOL` buffers inside `net_port_poll()`.
* **IPv4 & Application Services:**
  * **DHCP + Static Fallback:** Starts DHCP discovery on link-up with an `8000 ms` timeout, automatically applying a static IPv4 configuration (`APP_STATIC_IP`) if no DHCP lease is offered.
  * **HTTP Server (`TCP/80`):** Serves embedded web assets (`fsdata_custom.c` included via lwIP `fs.c`) and handles `/ctrl.cgi` board control commands.
  * **TFTP Server (`UDP/69`):** Backs file `put`/`get` transfers in binary (`octet`) mode with a dynamic RAM-disk buffer (`16 KiB` in L2-LIM, `1 MiB` in DDR).

---

## 2. Repository Layout

```text
mpfs-netapp/
├── Makefile                        # Top-level multi-target build, boot, flash & JTAG rules
├── toolchain.mk                    # RISC-V GCC, OpenOCD, GDB & HSS payload generator paths
├── application/
│   ├── inc/                        # Application-wide headers & static IP/MAC config
│   ├── hart1/u54_1.c               # Main network loop & interactive UART diagnostics
│   └── hart2..4/                   # Secondary U54 WFI stubs
├── boards/mpfs-disco-kit/
│   ├── board.mk                    # Discovery Kit DIE, PACKAGE & board CFLAGS
│   ├── board_config.h              # Peripheral mapping (APP_MAC=GEM0, APP_UART=MMUART4)
│   ├── board_init.c                # Board-level peripheral & GPIO initialization
│   ├── fpga_design_config/         # Libero-generated hardware headers (WAY_ENABLE, clocks, IO)
│   └── platform_config/
│       ├── lim-debug/              # Linker script (mpfs-lim.ld) & HAL/MAC config for JTAG
│       ├── lim-release/            # Linker script (mpfs-lim-app.ld) & HAL/MAC config for HSS
│       ├── ddr-debug/              # Linker script (mpfs-ddr-loaded-by-boot-loader.ld) for JTAG
│       ├── ddr-release/            # Linker script (mpfs-ddr-loaded-by-boot-loader.ld) for HSS
│       ├── envm-release/           # Direct eNVM execution config
│       └── envm-scratchpad-release/# eNVM LMA -> L2 Scratchpad VMA config
├── middleware/
│   ├── middleware.mk               # Modular lwIP (Filelists.mk) + net_stack source definitions
│   ├── lwip/src/                   # Upstream lwIP core, IPv4, netif, HTTP & TFTP modules
│   └── net_stack/
│       ├── lwipopts.h              # Conditional MEM_TARGET_LIM vs MEM_TARGET_DDR pool sizing
│       ├── net_port.c / .h         # GEM0 MAC driver, RX/TX rings, DHCP/link state machine
│       ├── web_server.c / .h       # HTTP server & CGI handlers
│       ├── tftp_server_app.c / .h  # TFTP RAM-disk callbacks & diagnostics
│       └── fsdata_custom.c         # Embedded HTML/assets (included directly by lwIP fs.c)
└── platform/
    ├── drivers/mss/                # MSS Ethernet MAC, PHY & MMUART drivers
    ├── hal/                        # RISC-V HAL & IRQ helpers
    └── mpfs_hal/                   # PolarFire SoC startup, NWC/DDR/SGMII init, PLIC/CLINT
```

---

## 3. L2-LIM Memory Architecture & Footprint Scaling

On the Discovery Kit Libero hardware design (`LIBERO_SETTING_WAY_ENABLE = 0x0000000BUL`), 12 of the 16 L2 SRAM ways (`1.5 MiB`) are configured as L2 Cache, leaving 4 ways (**512 KiB**, `0x0800_0000` – `0x0807_FFFF`) mapped as **L2-LIM SRAM**.

* **Standalone JTAG (`lim-debug`):** Links at `0x0800_0000` with `LENGTH = 512k`, utilizing the full L2-LIM region.
* **HSS Bootloader Payload (`lim-release`):** HSS reserves the lower `256 KiB` (`0x0800_0000` – `0x0803_FFFF`) for E51 runtime data, `.bss`, and IPI queues, so `mpfs-lim-app.ld` links into the upper **256 KiB High-LIM** (`ORIGIN = 0x08040000, LENGTH = 256k`).

To fit `.text`, `.rodata`, `.data`, `.bss`, DMA rings, lwIP pools, heap, and stacks inside `256 KiB` without degrading the `DDR` build, `-DMEM_TARGET_LIM` scales static allocations automatically:

| Resource | Location | `ddr-release` / `ddr-debug` | `lim-release` / `lim-debug` |
| :--- | :--- | :--- | :--- |
| **MAC RX DMA Descriptors** | `mss_ethernet_mac_sw_cfg.h` | `32U` | `16U` *(GEM0 DMA minimum)* |
| **MAC TX DMA Descriptors** | `mss_ethernet_mac_sw_cfg.h` | `1001U` | `16U` *(GEM0 DMA minimum)* |
| **MAC RX DMA Buffer** | `net_port.c` | `32 × 10,240 B` (`320 KiB`) | `16 × 1,536 B` (`24,576 B`) |
| **MAC TX DMA Buffer** | `net_port.c` | `8 × 1,536 B` (`12 KiB`) | `4 × 1,536 B` (`6,144 B`) |
| **ISR-to-Poll RX Queue** | `net_port.c` | `64` frames (`~95 KiB`) | `8` frames (`12,192 B`) |
| **lwIP Heap (`MEM_SIZE`)** | `lwipopts.h` | `128 KiB` | `24 KiB` |
| **lwIP PBUF Pool** | `lwipopts.h` | `32` buffers (`~48 KiB`) | `12` buffers (`~18.3 KiB`) |
| **TFTP RAM-Disk** | `tftp_server_app.c` | `1 MiB` (`1024 KiB`) | `16 KiB` |
| **Hart Stacks & Heap** | `Linker Script` | `168 KiB` total | `U54_1=8k`, `E51/U54=1k`, `Heap=4k` |

---

## 4. Build Targets

The `Makefile` infers `MEM_TARGET` automatically from convenience goals and outputs artifacts to `build/<board>/<target>/`:

| Make Target | Linker Script | Base Address | Flags & Optimization | Output Artifacts |
| :--- | :--- | :--- | :--- | :--- |
| **`lim-debug`** | `mpfs-lim.ld` | `0x08000000` (`512k`) | `-DMEM_TARGET_LIM -Og -g3` | `.elf`, `.bin`, `.hex`, `.fbi` |
| **`lim-release`** | `mpfs-lim-app.ld` | `0x08040000` (`256k`) | `-DMEM_TARGET_LIM -DIMAGE_LOADED_BY_BOOTLOADER=1 -O2` | `.elf`, `_payload.bin`, etc. |
| **`ddr-debug`** | `mpfs-ddr-loaded-by-boot-loader.ld` | `0x80000000` (`128M`) | `-DMEM_TARGET_DDR -DIMAGE_LOADED_BY_BOOTLOADER=1 -Og` | `.elf`, `.bin`, `.hex`, `.fbi` |
| **`ddr-release`** | `mpfs-ddr-loaded-by-boot-loader.ld` | `0x80000000` (`128M`) | `-DMEM_TARGET_DDR -DIMAGE_LOADED_BY_BOOTLOADER=1 -O2` | `.elf`, `_payload.bin`, etc. |

---

## 5. Step-by-Step Build, Boot, Flash & JTAG Debug Tutorial

### Hardware & Software Setup

1. **Software Tools:**
   * RISC-V GNU Embedded Toolchain (`riscv-none-elf-gcc`, `riscv-none-elf-gdb`) configured in `toolchain.mk`.
   * Microchip OpenOCD (`board/microsemi-riscv.cfg`) for FlashPro6 JTAG debugging.
   * Python 3 with `pyserial` (`python3 -m serial.tools.miniterm`).
2. **Board Connections:**
   * Connect the **Micro-USB cable** (`J5`) from the Discovery Kit to your PC. Linux enumerates four UART ports (`/dev/ttyUSB0` – `/dev/ttyUSB3`):
     * **`/dev/ttyUSB0` (`MMUART0`):** HSS bootloader console (`115200 8N1`).
     * **`/dev/ttyUSB2` (`MMUART4`):** NetApp application console (`115200 8N1`).
   * Connect an **Ethernet cable** from the Discovery Kit RJ45 port (`J11`) to your router, network switch, or host PC.

---

### Workflow A: Build & Audit Memory Usage

Use these commands to compile a target and verify that its code and data fit inside the selected memory region.

1. **Build the L2-LIM debug binary or DDR release payload:**
   ```bash
   make BOARD=mpfs-disco-kit lim-debug
   make BOARD=mpfs-disco-kit ddr-release
   ```
   * **Expected Result:** Each source file compiles with `[CC]` / `[AS]`, links with `[LD]`, and finishes with a size summary table and zero errors:
     ```text
      [LD]  build/mpfs-disco-kit/ddr-release/mpfs-net-app.elf
      [HSS] Generating boot payload: build/mpfs-disco-kit/ddr-release/mpfs-net-app_payload.bin

        text       data        bss        dec        hex    filename
       62480       3728    2006656    2072864     1fa120    build/mpfs-disco-kit/ddr-release/mpfs-net-app.elf
     ```

2. **Audit exact memory region utilization:**
   ```bash
   make BOARD=mpfs-disco-kit MEM_TARGET=lim-debug memusage
   ```
   * **Expected Result:** Prints a per-section breakdown confirming that `l2lim` usage is well below `100%` (typically ~35% of `512 KiB` for `lim-debug`, and ~68% of `256 KiB` for `lim-release`).

---

### Workflow B: Boot via HSS YMODEM (`lim-boot` / `ddr-boot`)

Use this workflow to stream a release payload over USB-UART to the pre-flashed Hart Software Services (HSS) bootloader without writing to SPI Flash.

1. **Terminal 1 — Open the Application UART Console (`MMUART4`):**
   ```bash
   python3 -m serial.tools.miniterm /dev/ttyUSB2 115200
   ```
   * **Expected Result:** The terminal opens and displays a connection header, waiting quietly for application output:
     ```text
     --- Miniterm on /dev/ttyUSB2 115200,8,N,1 ---
     --- Quit: Ctrl+] | Menu: Ctrl+T | Help: Ctrl+T followed by Ctrl+H ---
     ```

2. **Power-cycle or press the hardware Reset button on the board, then stream the payload in Terminal 2:**
   ```bash
   # Stream High-LIM release image (0x08040000)
   make BOARD=mpfs-disco-kit lim-boot

   # Or stream DDR release image (0x80000000)
   make BOARD=mpfs-disco-kit ddr-boot
   ```
   * **Expected Result:**
     * **In Terminal 2:** `mpfs-terminal` builds `_payload.bin`, interrupts HSS on `/dev/ttyUSB0`, initiates a YMODEM transfer (`Sending payload... 100%`), and triggers HSS `boot`.
     * **In Terminal 1 (`/dev/ttyUSB2`):** Within ~2 seconds of the transfer finishing, the NetApp banner, MAC initialization, and IPv4 address appear.

---

### Workflow C: Program SPI Flash for Standalone Power-On Boot (`lim-flash` / `ddr-flash`)

Use this workflow when you want the Discovery Kit to boot `mpfs-netapp` automatically on power-up without a host PC connected.

1. **Flash the HSS payload to onboard SPI Flash (`0x00020000`):**
   ```bash
   make BOARD=mpfs-disco-kit ddr-flash
   ```
   * **Expected Result:** `mpfs-terminal` transfers the payload to HSS, erases the target SPI Flash sectors, programs the image, and exits cleanly with `Flash programming complete`.

2. **Test Autonomous Cold-Boot:**
   * Open `python3 -m serial.tools.miniterm /dev/ttyUSB2 115200` and press the board's hardware **Reset** button (or unplug and reconnect USB power).
   * **Expected Result:** HSS automatically loads the payload from SPI Flash into DDR (`0x80000000`) and starts `u54_1`–`u54_4`. Within ~3–5 seconds, the NetApp startup banner appears on `/dev/ttyUSB2`.

---

### Workflow D: Interactive JTAG Debugging (`openocd` + GDB)

The project supports two interactive JTAG debugging modes. Understanding the difference prevents bus hangs during `load`:

| Feature | L2-LIM Standalone Debug (`debug-lim`) | DDR HSS-Assisted Debug (`debug-ddr`) |
| :--- | :--- | :--- |
| **Target Memory** | `0x08000000` (512 KiB on-chip SRAM) | `0x80000000` (128 MiB external DDR4) |
| **GDB Halt Command** | `monitor reset halt` (hard reset) | `monitor halt` (soft halt, preserves DDR) |
| **Who Trains DDR/Clocks?** | `E51` runs `mss_nwc_init()` from `_start` | Pre-flashed **HSS** trains DDR4 before attach |
| **Threads Resumed** | `thread apply all` (`E51` + `U54_1..4`) | `thread apply 2-5` (`U54_1..4` only; `E51` stays in HSS) |
| **TFTP RAM-Disk / Rings** | `16 KiB` TFTP / `16` RX & TX descriptors | `1024 KiB` TFTP / `32` RX & `1001` TX descriptors |

#### Option 1: L2-LIM Standalone Debug (`debug-lim`) — Zero Bootloader Dependency

1. **Terminal 1 — Open the Application UART Console (`MMUART4`):**
   ```bash
   python3 -m serial.tools.miniterm /dev/ttyUSB2 115200
   ```
   * **Expected Result:** Miniterm opens and waits for serial output.

2. **Terminal 2 — Start OpenOCD JTAG Server:**
   ```bash
   make BOARD=mpfs-disco-kit openocd
   ```
   * **Expected Result:** OpenOCD connects to the onboard FlashPro6 JTAG programmer, discovers all 5 RISC-V harts, and waits for GDB connections (keep this terminal running):
     ```text
     Info : [mpfs.hart0_e51] Examination succeed
     Info : [mpfs.hart1_u54_1] Examination succeed
     ...
     Info : Listening on port 3333 for gdb connections
     ```

3. **Terminal 3 — Build, Reset, Load into L2-LIM & Run GDB:**
   ```bash
   make BOARD=mpfs-disco-kit debug-lim
   ```
   * **Expected Result:**
     * **In Terminal 3 (GDB):** Connects to `localhost:3333`, performs `monitor reset halt`, loads sections into `0x08000000`, sets `$pc = _start` on all 5 threads, and prints `Continuing.`:
       ```text
       Loading section .text, size 0xf4a0 lma 0x8000000
       Loading section .sdata, size 0x20 lma 0x800f4a0
       Loading section .data, size 0xe70 lma 0x800f4c0
       Start address 0x0000000008000000, load size 66352
       ...
       Continuing.
       ```
     * **In Terminal 1 (`/dev/ttyUSB2`):** Immediately prints the startup banner showing **`Entry @ 0x0800...`** and **`RX ring=16, avail_rx=16, avail_tx=16`**.

#### Option 2: DDR HSS-Assisted Debug (`debug-ddr`) — Full 128 MiB Memory & Large Buffers

1. **Reset / Power-Cycle the Board & Wait ~2 Seconds:**
   * Press the hardware **Reset** button (or cycle power) and wait ~2 seconds before launching GDB.
   * **Expected Result:** The pre-flashed HSS bootloader runs on `E51`, initializes the PLLs, trains the DDR4 memory controller at `0x80000000`, and configures SGMII. *(If you watch `/dev/ttyUSB0`, you will see HSS finish booting.)*

2. **Terminal 1 — Open the Application UART Console (`MMUART4`):**
   ```bash
   python3 -m serial.tools.miniterm /dev/ttyUSB2 115200
   ```
   * **Expected Result:** Miniterm opens on `/dev/ttyUSB2`.

3. **Terminal 2 — Start OpenOCD JTAG Server:**
   ```bash
   make BOARD=mpfs-disco-kit openocd
   ```
   * **Expected Result:** OpenOCD prints `Info : Listening on port 3333 for gdb connections` (keep this terminal running).

4. **Terminal 3 — Build, Soft-Halt, Load into DDR & Run GDB:**
   ```bash
   make BOARD=mpfs-disco-kit debug-ddr
   ```
   * **Expected Result:**
     * **In Terminal 3 (GDB):** Connects to `localhost:3333`, performs a soft `monitor halt` (keeping DDR4 trained), loads sections directly into `0x80000000`, points `threads 2-5` (`u54_1`–`u54_4`) to `_start` while leaving `thread 1` (`e51`) parked in HSS, and prints `Continuing.`:
       ```text
       Loading section .text, size 0xf8c0 lma 0x80000000
       Loading section .sdata, size 0x20 lma 0x8000f8c0
       Loading section .data, size 0xe70 lma 0x8000f8e0
       Start address 0x0000000080000000, load size 67408
       ...
       Continuing.
       ```
     * **In Terminal 1 (`/dev/ttyUSB2`):** Immediately prints the startup banner showing **`Entry @ 0x8000...`** and **`RX ring=32, avail_rx=32, avail_tx=1001`**.

> **Tip:** To pause execution, set breakpoints (`b main`), or inspect variables in GDB at any time during either debug session, press **`Ctrl+C`** in Terminal 3, inspect state, and type **`c`** (`continue`) to resume.

---

## 6. Runtime Verification & Network Testing

### Step 1: Verify Serial Console Output (`MMUART4` @ `115200` baud)

Once the application starts (via JTAG, YMODEM boot, or SPI Flash), inspect `/dev/ttyUSB2`:

* **Expected Result:**
  ```text
  ====================================================================
    PolarFire SoC Bare-Metal NetApp (mpfs-disco-kit)
    Entry @ 0x80000000 | Build: 2026-11-16 11:00:00
  ====================================================================
  [MAC] GEM0 TBI/SGMII + VSC8221 ready (RX ring=32, avail_rx=32, avail_tx=1001)
  [DHCP] Started DHCP discovery (8s timeout -> static fallback)...
  [NET @ T+8012 ms] Link : UP (1000 Mbps, Full-Duplex)
  [NET] IP   : 192.168.20.50
  [NET] Mask : 255.255.255.0
  [NET] GW   : 192.168.20.1
  ```
  *(Note: If connected to a DHCP-enabled router, the IP address will reflect your router's DHCP lease; if connected directly to a PC without a DHCP server, it falls back to `192.168.20.50` after 8 seconds.)*

* **Check TFTP RAM-Disk Status:** Press the **`t`** key inside the `/dev/ttyUSB2` terminal.
  * **Expected Result:** Prints the TFTP RAM-disk base address and capacity:
    ```text
    [TFTP] RAM-disk @ 0x80054000 | Capacity: 1024 KiB | Last file: none
    ```
    *(Capacity reads `16 KiB` in `lim-debug` / `lim-release`, and `1024 KiB` in `ddr-debug` / `ddr-release`.)*

### Step 2: Run Host PC Network Tests

Open a terminal on your host PC and replace `<board-ip>` with the IPv4 address printed on `/dev/ttyUSB2` (e.g., `192.168.20.50`):

1. **Test ICMP Echo (Ping):**
   ```bash
   ping -c 4 <board-ip>
   ```
   * **Expected Result:** All 4 packets return with sub-millisecond latency and `0% packet loss`:
     ```text
     4 packets transmitted, 4 received, 0% packet loss, time 3054ms
     rtt min/avg/max/mdev = 0.281/0.312/0.365/0.032 ms
     ```

2. **Test HTTP Web Server & CGI Board Control (`TCP/80`):**
   ```bash
   curl -i http://<board-ip>/
   curl -i "http://<board-ip>/ctrl.cgi?cmd=led_toggle"
   ```
   * **Expected Result:**
     * The first command returns `HTTP/1.1 200 OK` followed by the HTML source of the embedded web control dashboard (which you can also open in any web browser at `http://<board-ip>/`).
     * The second command returns `HTTP/1.1 200 OK` and physically toggles a user LED on the Discovery Kit.

3. **Test TFTP Binary File Upload & Download (`UDP/69`):**
   *(Keep file size $\le 16\text{ KiB}$ when testing `lim-debug` or `lim-release`; files up to $1\text{ MiB}$ are supported in `ddr-debug` and `ddr-release`.)*
   ```bash
   tftp -m octet <board-ip> -c put Makefile
   tftp -m octet <board-ip> -c get Makefile /tmp/Makefile.rx
   diff -s Makefile /tmp/Makefile.rx
   ```
   * **Expected Result:** Both `tftp` commands complete without timeout errors, and `diff -s` confirms byte-for-byte integrity:
     ```text
     Files Makefile and /tmp/Makefile.rx are identical
     ```

---

## 7. Key Implementation Notes

* **Minimum GEM0 DMA Ring Size (`16U`):** The PolarFire SoC GEM0 DMA controller caches descriptors in bursts; setting `MSS_MAC_RX_RING_SIZE` or `MSS_MAC_TX_RING_SIZE` below `16U` stalls packet reception. Always keep `mss_ethernet_mac_sw_cfg.h` synchronized across `platform/drivers/mss/mss_ethernet_mac/` and all `boards/mpfs-disco-kit/platform_config/*/drivers/mss/mss_ethernet_mac/` directories.
* **Single Inclusion of `fsdata_custom.c`:** `middleware/lwip/src/apps/http/fs.c` `#include`s `fsdata_custom.c` directly. `middleware/middleware.mk` intentionally omits `fsdata_custom.c` from `NET_STACK_SRCS_C` to prevent duplicate `file_index_html` linker symbols.
* **HSS L2-LIM Boundary (`0x08040000`):** Never link an HSS-booted image (`lim-release`) below `0x08040000`, and never stream `lim-debug` (`0x08000000`) via `lim-boot`, as zeroing `.bss` at `0x08000000` overwrites HSS's live E51 runtime memory.