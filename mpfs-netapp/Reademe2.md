# PolarFire SoC Bare-Metal Network Application (`mpfs-netapp`)

`mpfs-netapp` is a bare-metal RISC-V network application for the Microchip PolarFire SoC built on `mpfs_hal` and a polling (`NO_SYS = 1`) **lwIP** TCP/IP stack. It features a completely **board-agnostic architecture**, allowing it to run natively from on-chip **L2-LIM SRAM** or external **DDR** across different development kits (e.g., Discovery Kit, Icicle Kit) or custom hardware without duplicating application code.

It provides DHCP/static IPv4 networking, ICMP ping, an HTTP web server with CGI board control, and an in-memory TFTP server.

## 1. Architecture & Features

* **Multi-Board Portability (HAL):**
  Application logic and network stacks are strictly decoupled from hardware specifics. Board-specific routing, PHY drivers, and clock initializations are dynamically injected at compile-time via `boards/<board-name>/board_config.h` and `board.mk`.
  
* **Multi-Hart Execution Model:**
  * **E51 (Monitor Hart):** In standalone debug targets (`lim-debug`), E51 executes `mss_nwc_init()` to configure MSS PLLs, clocks, DDR, and SGMII before waking the U54 application harts. In bootloader-assisted targets, Hart Software Services (HSS) remains resident on E51 and dispatches the U54 harts directly.
  * **U54_1 (Application Hart):** Initializes the board-defined stdio UART, brings up the MAC + PHY, and runs the cooperative lwIP polling loop (`net_port_poll()`), HTTP server, and TFTP server.
  * **U54_2 – U54_4:** Satisfy the `mpfs_hal` startup barrier and enter low-power `wfi` loops.

* **Ethernet Driver (`middleware/net_stack/net_port.c`):**
  * Drives the board-defined MAC over TBI/SGMII connected to a dynamic Gigabit Ethernet PHY with link-speed auto-negotiation.
  * Decouples PLIC interrupt context from lwIP using a lock-free ring (`g_rx_raw_queue`).

* **IPv4 & Application Services:**
  * **DHCP + Static Fallback:** Starts DHCP discovery on link-up with an 8-second timeout, falling back to a board-defined static IPv4 configuration.
  * **HTTP Server (`TCP/80`) & TFTP Server (`UDP/69`).**

## 2. Repository Layout

```text
mpfs-netapp/
├── Makefile                        # Top-level multi-target build & debug rules
├── toolchain.mk                    # RISC-V GCC, OpenOCD, GDB paths
├── application/                    # 100% Board-Agnostic Core Logic
│   ├── inc/                        
│   ├── hart1/u54_1.c               # Main network loop & UART diagnostics
│   └── hart2..4/                   # Secondary U54 WFI stubs
├── boards/                         # Hardware Abstraction Layer (HAL)
│   └── mpfs-disco-kit/
│       ├── board.mk                # Board variables (DIE, PACKAGE, PHY driver, CFLAGS)
│       ├── board_config.h          # Hardware macros (APP_UART, APP_MAC, APP_PHY_*)
│       ├── board_init.c            # Implements board_periph_init()
│       ├── fpga_design_config/     # Libero-generated hardware headers
│       └── platform_config/        # Linker scripts & HAL memory configs
├── middleware/                     # 100% Board-Agnostic Network Stack
│   ├── lwip/src/                   # Upstream lwIP core
│   └── net_stack/                  # lwIP ports, MAC driver, HTTP & TFTP servers
└── platform/
    ├── drivers/mss/                # Microchip bare-metal peripheral drivers
    ├── hal/                        # RISC-V HAL & IRQ helpers
    └── mpfs_hal/                   # PolarFire SoC startup & hardware configuration
```

## 3. Memory Architecture & Build Targets

The `Makefile` infers the memory target automatically and generates artifacts in `build/<board>/<target>/`:

| Make Target | Linker Script | Base Address | Boot Dependency |
| ----- | ----- | ----- | ----- |
| **`lim-debug`** | `mpfs-lim.ld` | `0x08000000` (512k) | None (Standalone JTAG) |
| **`lim-release`** | `mpfs-lim-app.ld` | `0x08040000` (256k) | HSS Bootloader |
| **`ddr-debug`** | `mpfs-ddr-loaded-...ld`| `0x80000000` (128M) | HSS (Soft JTAG Attach) |
| **`ddr-release`** | `mpfs-ddr-loaded-...ld`| `0x80000000` (128M) | HSS Bootloader |
| **`envm-lim`** | `mpfs-envm-lma-...ld`| `0x20220100` (eNVM)| None (eNVM -> L2-LIM) |

> *Note:* L2-LIM builds (`-DMEM_TARGET_LIM`) dynamically shrink DMA descriptors, rx queues, lwIP heaps, and the TFTP RAM-disk to fit safely inside limited on-chip SRAM.

## 4. Step-by-Step Build & Deploy Tutorial

### Workflow A: Build & Audit Memory Usage

Compile a target and verify its memory footprint:
```bash
make BOARD=mpfs-disco-kit lim-debug
make BOARD=mpfs-disco-kit MEM_TARGET=lim-debug memusage
```
*(Expected: Clean compile, zero errors, and a percentage breakdown of memory usage).*

### Workflow B: Boot via HSS YMODEM (`mpfs-terminal`)

Stream a release payload to the pre-flashed HSS bootloader via USB-UART:
1. Open App UART: `python3 -m serial.tools.miniterm /dev/ttyUSBY 115200`
2. Power-cycle the board.
3. Stream Payload (Terminal 2):
   ```bash
   mpfs-terminal /dev/ttyUSBX --kernel build/mpfs-disco-kit/ddr-release/mpfs-net-app_payload.bin --kernel-adr 0x80000000
   ```
*(Expected: HSS receives the payload via YMODEM and immediately boots the application. The banner and IP address appear on the App UART).*

### Workflow C: JTAG Debugging (`openocd` + GDB)

1. Start OpenOCD: `make BOARD=mpfs-disco-kit openocd`
2. Open App UART: `python3 -m serial.tools.miniterm /dev/ttyUSBY 115200`
3. Launch GDB:
   * **Standalone LIM:** `make BOARD=mpfs-disco-kit debug-lim` (Issues hard reset, E51 configures hardware).
   * **DDR with HSS:** *Power-cycle board and wait 2 seconds for HSS to train DDR.* Then run `make BOARD=mpfs-disco-kit debug-ddr` (Issues soft halt, preventing DDR controller crash).

## 5. Porting to a New Board

Thanks to the hardware abstraction layer, adding a custom board requires **zero modifications** to the `application/`, `middleware/`, or root `Makefile`.

To create support for a custom board (e.g., `my-custom-board`):

1. **Create the Directory:**
   ```bash
   mkdir -p boards/my-custom-board/{fpga_design,platform_config}
   ```
2. **Define `board.mk`:** Create `boards/my-custom-board/board.mk` to specify the SoC variant and PHY drivers:
   ```makefile
   DIE              := MPFS250T
   PACKAGE          := FCVG484
   BOARD_SRCS_C     := $(BOARD_DIR)/board_init.c
   BOARD_INC_DIRS   := $(BOARD_DIR)
   
   # Include core MAC drivers and your specific PHY
   BOARD_PHY_DRIVER := platform/drivers/mss/mss_ethernet_mac/mss_ethernet_mac.c \
                       platform/drivers/mss/mss_ethernet_mac/null_phy.c \
                       platform/drivers/mss/mss_ethernet_mac/ti_dp83867_phy.c
   ```
3. **Map Hardware in `board_config.h`:** Create `boards/my-custom-board/board_config.h` to define peripherals:
   ```c
   #ifndef BOARD_CONFIG_H
   #define BOARD_CONFIG_H
   #include "drivers/mss/mss_mmuart/mss_uart.h"
   #include "drivers/mss/mss_ethernet_mac/phy.h"

   #define APP_BOARD_NAME          "My Custom Board"
   #define APP_UART                (&g_mss_uart1_lo)  /* Example: Uses UART1 */
   #define APP_MAC                 (&g_mac1)          /* Example: Uses MAC1 */
   
   /* Map specific PHY Callbacks */
   #define APP_PHY_INIT            MSS_MAC_DP83867_phy_init
   /* ... other PHY mappings ... */
   
   void board_periph_init(void);
   #endif
   ```
4. **Implement Initializer (`board_init.c`):**
   ```c
   #include "board_config.h"
   void board_periph_init(void) {
       /* Turn on clocks for your specific UART/MAC */
       mss_config_clk_rst(MSS_PERIPH_MMUART1, (uint8_t)MPFS_HAL_FIRST_HART, PERIPHERAL_ON);
       mss_config_clk_rst(MSS_PERIPH_MAC1,    (uint8_t)MPFS_HAL_FIRST_HART, PERIPHERAL_ON);
       PLIC_init();
       __enable_irq();
   }
   ```
5. **Build for the New Board:**
   ```bash
   make BOARD=my-custom-board ddr-debug
   ```

## 6. Runtime Verification & Network Testing

Once booted, the application UART will display:
```text
====================================================================
  PolarFire SoC Bare-Metal NetApp (PolarFire SoC Discovery Kit)
  Entry @ 0x80000000 | Build: 2026-11-16 12:00:00
====================================================================
[MAC] GEM0 TBI/SGMII + VSC8221 ready (RX ring=32, avail_rx=32, avail_tx=1001)
[DHCP] Started DHCP discovery (8s timeout -> static fallback)...
[NET @ T+8012 ms] Link : UP (1000 Mbps, Full-Duplex)
[NET] IP   : 192.168.20.50
```

* **Ping Test:** `ping -c 4 <board-ip>`
* **Web Server Test:** `curl -i http://<board-ip>/`
* **TFTP Test:** `tftp -m octet <board-ip> -c put testfile.bin` (Press `t` in the serial console to view dynamic RAM-disk capacity).


# PolarFire SoC Bare-Metal Network Application (`mpfs-netapp`)

`mpfs-netapp` is a bare-metal RISC-V network application for the **Microchip PolarFire SoC Discovery Kit** (`MPFS095T` / `FCSG325`) built on `mpfs_hal` and a polling (`NO_SYS = 1`) **lwIP** TCP/IP stack. It runs natively from on-chip **L2-LIM SRAM** (both standalone via JTAG and as an HSS bootloader payload) or from external **DDR**, providing DHCP/static IPv4 networking, ICMP ping, an HTTP web server with CGI board control, and an in-memory TFTP server.

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

## 2. Repository Layout

```
mpfs-netapp/
├── Makefile                        # Top-level multi-target build, flash & JTAG rules
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

## 3. L2-LIM Memory Architecture & Footprint Scaling

On the Discovery Kit Libero hardware design (`LIBERO_SETTING_WAY_ENABLE = 0x0000000BUL`), 12 of the 16 L2 SRAM ways (`1.5 MiB`) are configured as L2 Cache, leaving 4 ways (**512 KiB**, `0x0800_0000` – `0x0807_FFFF`) mapped as **L2-LIM SRAM**.

* **Standalone JTAG (`lim-debug`):** Links at `0x0800_0000` with `LENGTH = 512k`, utilizing the full L2-LIM region.

* **HSS Bootloader Payload (`lim-release`):** HSS reserves the lower `256 KiB` (`0x0800_0000` – `0x0803_FFFF`) for E51 runtime data, `.bss`, and IPI queues, so `mpfs-lim-app.ld` links into the upper **256 KiB High-LIM** (`ORIGIN = 0x08040000, LENGTH = 256k`).

To fit `.text`, `.rodata`, `.data`, `.bss`, DMA rings, lwIP pools, heap, and stacks inside `256 KiB` without degrading the `DDR` build, `-DMEM_TARGET_LIM` scales static allocations automatically:

| Resource | Location | `ddr-release` / `ddr-debug` | `lim-release` / `lim-debug` | 
| ----- | ----- | ----- | ----- | 
| **MAC RX DMA Descriptors** | `mss_ethernet_mac_sw_cfg.h` | `32U` | `16U` *(GEM0 DMA minimum)* | 
| **MAC TX DMA Descriptors** | `mss_ethernet_mac_sw_cfg.h` | `1001U` | `16U` *(GEM0 DMA minimum)* | 
| **MAC RX DMA Buffer** | `net_port.c` | `32 × 10,240 B` (`320 KiB`) | `16 × 1,536 B` (`24,576 B`) | 
| **MAC TX DMA Buffer** | `net_port.c` | `8 × 1,536 B` (`12 KiB`) | `4 × 1,536 B` (`6,144 B`) | 
| **ISR-to-Poll RX Queue** | `net_port.c` | `64` frames (`~95 KiB`) | `8` frames (`12,192 B`) | 
| **lwIP Heap (`MEM_SIZE`)** | `lwipopts.h` | `128 KiB` | `24 KiB` | 
| **lwIP PBUF Pool** | `lwipopts.h` | `32` buffers (`~48 KiB`) | `12` buffers (`~18.3 KiB`) | 
| **TFTP RAM-Disk** | `tftp_server_app.c` | `1 MiB` (`1024 KiB`) | `16 KiB` | 
| **Hart Stacks & Heap** | `Linker Script` | `168 KiB` total | `U54_1=8k`, `E51/U54=1k`, `Heap=4k` | 

## 4. Build Targets

The `Makefile` infers `MEM_TARGET` automatically from convenience goals and outputs artifacts to `build/<board>/<target>/`:

| Make Target | Linker Script | Base Address | Flags & Optimization | Output Artifacts | 
| ----- | ----- | ----- | ----- | ----- | 
| **`lim-debug`** | `mpfs-lim.ld` | `0x08000000` (`512k`) | `-DMEM_TARGET_LIM -Og -g3` | `.elf`, `.bin`, `.hex`, `.fbi` | 
| **`lim-release`** | `mpfs-lim-app.ld` | `0x08040000` (`256k`) | `-DMEM_TARGET_LIM -DIMAGE_LOADED_BY_BOOTLOADER=1 -O2` | `.elf`, `_payload.bin`, etc. | 
| **`ddr-debug`** | `mpfs-ddr-loaded-by-boot-loader.ld` | `0x80000000` (`128M`) | `-DMEM_TARGET_DDR -DIMAGE_LOADED_BY_BOOTLOADER=1 -Og` | `.elf`, `.bin`, `.hex`, `.fbi` | 
| **`ddr-release`** | `mpfs-ddr-loaded-by-boot-loader.ld` | `0x80000000` (`128M`) | `-DMEM_TARGET_DDR -DIMAGE_LOADED_BY_BOOTLOADER=1 -O2` | `.elf`, `_payload.bin`, etc. | 

## 5. Step-by-Step Build, Boot, Flash & JTAG Debug Tutorial

### Hardware & Software Setup

1. **Software Tools:**

   * RISC-V GNU Embedded Toolchain (`riscv-none-elf-gcc`, `riscv-none-elf-gdb`) configured in `toolchain.mk`.
   * Microchip OpenOCD (`board/microsemi-riscv.cfg`) for FlashPro6 JTAG debugging.
   * Python 3 with `pyserial` (`python3 -m serial.tools.miniterm`) and `mpfs-terminal`.

2. **Board Connections:**

   * Connect the **Micro-USB cable** (`J5`) from the Discovery Kit to your PC. Linux enumerates four UART ports (e.g., `/dev/ttyUSB0` – `/dev/ttyUSB3`), which may shift if unplugged. Generally:
     * **`/dev/ttyUSB<0>` (`MMUART0`):** HSS bootloader console (`115200 8N1`).
     * **`/dev/ttyUSB<2>` (`MMUART4`):** NetApp application console (`115200 8N1`).
   * Substitute your system's exact HSS and App ports for `/dev/ttyUSBX` and `/dev/ttyUSBY` in the commands below.
   * Connect an **Ethernet cable** from the Discovery Kit RJ45 port (`J11`) to your router, network switch, or host PC.

### Workflow A: Build & Audit Memory Usage

Use these commands to compile a target and verify that its code and data fit inside the selected memory region.

1. **Build the L2-LIM debug binary or DDR release payload:**

   ```bash
   make BOARD=mpfs-disco-kit lim-debug
   make BOARD=mpfs-disco-kit ddr-release
   ```

   * **Expected Result:** Each source file compiles, links with `[LD]`, and finishes with a size summary table and zero errors:

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

   * **Expected Result:** Prints a per-section breakdown confirming that `l2lim` usage is well below `100%` (typically ~35% of `512 KiB` for `lim-debug`).

### Workflow B: Boot via HSS YMODEM (`mpfs-terminal`)

Use this workflow to stream a release payload directly over USB-UART to the pre-flashed Hart Software Services (HSS) bootloader without writing to SPI Flash. 

1. **Terminal 1 — Open the Application UART Console (`MMUART4`):**

   ```bash
   python3 -m serial.tools.miniterm /dev/ttyUSBY 115200
   ```

   * **Expected Result:** The terminal opens and displays a connection header, waiting quietly for application output.

2. **Terminal 2 — Stream the Payload via `mpfs-terminal`:**
   Power-cycle or press the hardware Reset button on the board, then immediately run the appropriate command for your build:

   **For High-LIM (`lim-release`):**
   ```bash
   mpfs-terminal /dev/ttyUSBX \
       --kernel build/mpfs-disco-kit/lim-release/mpfs-net-app_payload.bin \
       --kernel-adr 0x08040000
   ```

   **For DDR (`ddr-release`):**
   ```bash
   mpfs-terminal /dev/ttyUSBX \
       --kernel build/mpfs-disco-kit/ddr-release/mpfs-net-app_payload.bin \
       --kernel-adr 0x80000000
   ```

   * **Expected Result:**
     * **In Terminal 2:** `mpfs-terminal` interrupts HSS on `/dev/ttyUSBX`, initiates a YMODEM transfer (`Sending payload... 100%`), and triggers HSS to `boot`.
     * **In Terminal 1 (`/dev/ttyUSBY`):** Within ~2 seconds of the transfer finishing, the NetApp banner, MAC initialization, and IPv4 address appear.

### Workflow C: Program SPI Flash for Standalone Power-On Boot

Use this workflow when you want the Discovery Kit to boot `mpfs-netapp` automatically on power-up without a host PC connected.

1. **Flash the HSS payload to onboard SPI Flash (`0x00020000`):**
   Ensure HSS is running, then invoke `mpfs-terminal` with the `--flash` flag:

   **For High-LIM (`lim-release`):**
   ```bash
   mpfs-terminal /dev/ttyUSBX \
       --kernel build/mpfs-disco-kit/lim-release/mpfs-net-app_payload.bin \
       --flash --kernel-adr 0x00020000
   ```

   **For DDR (`ddr-release`):**
   ```bash
   mpfs-terminal /dev/ttyUSBX \
       --kernel build/mpfs-disco-kit/ddr-release/mpfs-net-app_payload.bin \
       --flash --kernel-adr 0x00020000
   ```

   * **Expected Result:** `mpfs-terminal` transfers the payload to HSS, erases the target SPI Flash sectors, programs the image, and exits cleanly with `Flash programming complete`.

2. **Test Autonomous Cold-Boot:**

   * Open `python3 -m serial.tools.miniterm /dev/ttyUSBY 115200` and press the board's hardware **Reset** button (or unplug and reconnect USB power).
   * **Expected Result:** HSS automatically loads the payload from SPI Flash into memory and starts the application. Within ~3–5 seconds, the NetApp startup banner appears on your serial terminal.

### Workflow D: Interactive JTAG Debugging (`openocd` + GDB)

The project supports two interactive JTAG debugging modes. Understanding the difference prevents bus hangs during `load`:

| Feature | L2-LIM Standalone Debug (`debug-lim`) | DDR HSS-Assisted Debug (`debug-ddr`) | 
| ----- | ----- | ----- | 
| **Target Memory** | `0x08000000` (512 KiB on-chip SRAM) | `0x80000000` (128 MiB external DDR4) | 
| **GDB Halt Command** | `monitor reset halt` (hard reset) | `monitor halt` (soft halt, preserves DDR) | 
| **Who Trains DDR/Clocks?** | `E51` runs `mss_nwc_init()` from `_start` | Pre-flashed **HSS** trains DDR4 before attach | 
| **Threads Resumed** | `thread apply all` (`E51` + `U54_1..4`) | `thread apply 2-5` (`U54_1..4` only; `E51` stays in HSS) | 
| **TFTP RAM-Disk / Rings** | `16 KiB` TFTP / `16` RX & TX descriptors | `1024 KiB` TFTP / `32` RX & `1001` TX descriptors | 

#### Option 1: L2-LIM Standalone Debug (`debug-lim`) — Zero Bootloader Dependency

1. **Terminal 1 — Open the Application UART Console (`MMUART4`):**

   ```bash
   python3 -m serial.tools.miniterm /dev/ttyUSBY 115200
   ```

   * **Expected Result:** Miniterm opens and waits for serial output.

2. **Terminal 2 — Start OpenOCD JTAG Server:**

   ```bash
   make BOARD=mpfs-disco-kit openocd
   ```

   * **Expected Result:** OpenOCD connects to the onboard FlashPro6 JTAG programmer, discovers all 5 RISC-V harts, and waits for GDB connections (keep this terminal running).

3. **Terminal 3 — Build, Reset, Load into L2-LIM & Run GDB:**

   ```bash
   make BOARD=mpfs-disco-kit debug-lim
   ```

   * **Expected Result:**
     * **In Terminal 3 (GDB):** Connects to `localhost:3333`, performs `monitor reset halt`, loads sections into `0x08000000`, sets `$pc = _start` on all 5 threads, and prints `Continuing.`.
     * **In Terminal 1 (`/dev/ttyUSBY`):** Immediately prints the startup banner showing **`Entry @ 0x0800...`** and **`RX ring=16, avail_rx=16, avail_tx=16`**.

#### Option 2: DDR HSS-Assisted Debug (`debug-ddr`) — Full 128 MiB Memory & Large Buffers

1. **Reset / Power-Cycle the Board & Wait ~2 Seconds:**

   * Press the hardware **Reset** button (or cycle power) and wait ~2 seconds before launching GDB.
   * **Expected Result:** The pre-flashed HSS bootloader runs on `E51`, initializes the PLLs, trains the DDR4 memory controller at `0x80000000`, and configures SGMII.

2. **Terminal 1 — Open the Application UART Console (`MMUART4`):**

   ```bash
   python3 -m serial.tools.miniterm /dev/ttyUSBY 115200
   ```

   * **Expected Result:** Miniterm opens.

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
     * **In Terminal 3 (GDB):** Connects to `localhost:3333`, performs a soft `monitor halt` (keeping DDR4 trained), loads sections directly into `0x80000000`, points `threads 2-5` (`u54_1`–`u54_4`) to `_start` while leaving `thread 1` (`e51`) parked in HSS, and prints `Continuing.`.
     * **In Terminal 1 (`/dev/ttyUSBY`):** Immediately prints the startup banner showing **`Entry @ 0x8000...`** and **`RX ring=32, avail_rx=32, avail_tx=1001`**.

> **Tip:** To pause execution, set breakpoints (`b main`), or inspect variables in GDB at any time during either debug session, press **`Ctrl+C`** in Terminal 3, inspect state, and type **`c`** (`continue`) to resume.

## 6. Runtime Verification & Network Testing

### Step 1: Verify Serial Console Output (`MMUART4` @ `115200` baud)

Once the application starts (via JTAG, YMODEM boot, or SPI Flash), inspect `/dev/ttyUSBY`:

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

* **Check TFTP RAM-Disk Status:** Press the **`t`** key inside the terminal.
  * **Expected Result:** Prints the TFTP RAM-disk base address and capacity (e.g., `1024 KiB` for DDR).

### Step 2: Run Host PC Network Tests

Open a terminal on your host PC and replace `<board-ip>` with the IPv4 address printed on `/dev/ttyUSBY` (e.g., `192.168.20.50`):

1. **Test ICMP Echo (Ping):**

   ```bash
   ping -c 4 <board-ip>
   ```

   * **Expected Result:** All 4 packets return with sub-millisecond latency and `0% packet loss`.

2. **Test HTTP Web Server & CGI Board Control (`TCP/80`):**

   ```bash
   curl -i http://<board-ip>/
   curl -i "http://<board-ip>/ctrl.cgi?cmd=led_toggle"
   ```

   * **Expected Result:** Both commands return `HTTP/1.1 200 OK`. The second physically toggles a user LED on the Discovery Kit.

3. **Test TFTP Binary File Upload & Download (`UDP/69`):**
   *(Keep file size* $\le 16\text{ KiB}$ *when testing `lim-debug` or `lim-release`; files up to* $1\text{ MiB}$ *are supported in `ddr-debug` and `ddr-release`.)*

   ```bash
   tftp -m octet <board-ip> -c put Makefile
   tftp -m octet <board-ip> -c get Makefile /tmp/Makefile.rx
   diff -s Makefile /tmp/Makefile.rx
   ```

   * **Expected Result:** Both `tftp` commands complete without timeout errors, and `diff -s` confirms `Files Makefile and /tmp/Makefile.rx are identical`.

## 7. Key Implementation Notes

* **Minimum GEM0 DMA Ring Size (`16U`):** The PolarFire SoC GEM0 DMA controller caches descriptors in bursts; setting `MSS_MAC_RX_RING_SIZE` or `MSS_MAC_TX_RING_SIZE` below `16U` stalls packet reception. Always keep `mss_ethernet_mac_sw_cfg.h` synchronized across `platform/drivers/mss/mss_ethernet_mac/` and all `boards/mpfs-disco-kit/platform_config/*/drivers/mss/mss_ethernet_mac/` directories.

* **Single Inclusion of `fsdata_custom.c`:** `middleware/lwip/src/apps/http/fs.c` `#include`s `fsdata_custom.c` directly. `middleware/middleware.mk` intentionally omits `fsdata_custom.c` from `NET_STACK_SRCS_C` to prevent duplicate `file_index_html` linker symbols.

* **HSS L2-LIM Boundary (`0x08040000`):** Never link an HSS-booted image (`lim-release`) below `0x08040000`, and never stream `lim-debug` (`0x08000000`) via YMODEM, as zeroing `.bss` at `0x08000000` overwrites HSS's live E51 runtime memory.