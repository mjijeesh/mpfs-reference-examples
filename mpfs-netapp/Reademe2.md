# PolarFire SoC Bare-Metal Network Application (`mpfs-netapp`)

`mpfs-netapp` is a bare-metal RISC-V network application for the **Microchip PolarFire SoC Discovery Kit** (`MPFS095T` / `FCSG325`) built on `mpfs_hal` and a polling (`NO_SYS = 1`) **lwIP** TCP/IP stack. It runs natively from on-chip **L2-LIM SRAM** (both standalone via JTAG and as an HSS bootloader payload) or from external **DDR**, providing DHCP/static IPv4 networking, ICMP ping, an HTTP web server with CGI board control, and an in-memory TFTP server.

---

## 1. Architecture & Features

* **Multi-Hart Execution Model:**
  * **E51 (Monitor Hart):** In standalone debug targets (`lim-debug`, `ddr-debug`), E51 executes `mss_nwc_init()` from `_start` to configure MSS PLLs, clocks, DDR, and SGMII before waking the U54 application harts. In bootloader targets (`lim-release`, `ddr-release`), Hart Software Services (HSS) remains resident on E51 and dispatches the U54 harts directly.
  * **U54_1 (Application Hart):** Initializes `MMUART4` stdio (`115200 8N1`), brings up GEM0 + VSC8221 PHY, and runs the cooperative lwIP polling loop (`net_port_poll()`), HTTP server, and TFTP server.
  * **U54_2 – U54_4:** Satisfy the `mpfs_hal` startup barrier and enter low-power `wfi` loops.
* **Ethernet Driver (`middleware/net_stack/net_port.c`):**
  * Drives **GEM0** (`APP_MAC`) over TBI/SGMII connected to the onboard **VSC8221** Gigabit Ethernet PHY (`MSS_MAC_VSC8221_*`) with link-speed auto-negotiation (`10/100/1000 Mbps`, Full/Half Duplex).
  * Decouples PLIC interrupt context from lwIP by copying received frames inside `mac_rx_callback()` into a lock-free ring (`g_rx_raw_queue`) and draining them into `PBUF_POOL` buffers inside `net_port_poll()`.
* **IPv4 & Application Services:**
  * **DHCP + Static Fallback:** Starts DHCP discovery on link-up with an `8000 ms` timeout, automatically applying a static IPv4 configuration (`APP_STATIC_IP`) if no DHCP lease is offered.
  * **HTTP Server (`TCP/80`):** Serves embedded web assets (`fsdata_custom.c` included via lwIP `fs.c`) and handles `/ctrl.cgi` board control commands.
  * **TFTP Server (`UDP/69`):** Backs file `put`/`get` transfers in binary (`octet`) mode with a static RAM-disk buffer (`16 KiB` in L2-LIM, `1 MiB` in DDR).

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
│       ├── ddr-debug/              # Linker script (mpfs-ddr.ld) & HAL/MAC config for JTAG
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
| **Hart Stacks & Heap** | `mpfs-lim-app.ld` | `52 KiB` total | `U54_1=8k`, `E51/U54_2..4=1k`, `Heap=4k`, `Common=2k` |

---

## 4. Build Targets

The `Makefile` infers `MEM_TARGET` automatically from convenience goals (`lim-debug`, `lim-release`, `ddr-debug`, `ddr-release`, `envm-release`, `envm-lim`) and outputs artifacts to `build/<board>/<target>/`:

| Make Target | Linker Script | Base Address | Flags & Optimization | Output Artifacts |
| :--- | :--- | :--- | :--- | :--- |
| **`lim-debug`** | `mpfs-lim.ld` | `0x08000000` (`512k`) | `-DMEM_TARGET_LIM -Og -g3` | `.elf`, `.bin`, `.hex`, `.fbi` |
| **`lim-release`** | `mpfs-lim-app.ld` | `0x08040000` (`256k`) | `-DMEM_TARGET_LIM -DIMAGE_LOADED_BY_BOOTLOADER=1 -O2` | `.elf`, `.bin`, `.hex`, `.fbi`, `_payload.bin` |
| **`ddr-debug`** | `mpfs-ddr.ld` | `0x80000000` (`128M`) | `-DMEM_TARGET_DDR -Og -g3` | `.elf`, `.bin`, `.hex`, `.fbi` |
| **`ddr-release`** | `mpfs-ddr-loaded-by-boot-loader.ld` | `0x80000000` (`128M`) | `-DMEM_TARGET_DDR -DIMAGE_LOADED_BY_BOOTLOADER=1 -O2` | `.elf`, `.bin`, `.hex`, `.fbi`, `_payload.bin` |
| **`envm-release`** | `mpfs-envm.ld` | `0x20220100` (`128k`) | `-DMEM_TARGET_LIM -O2` | `.elf`, `.bin`, `.hex`, `.fbi` |
| **`envm-lim`** | `mpfs-envm-lma-scratchpad-vma.ld` | `0x20220100` -> `0x0A000000` | `-DMEM_TARGET_LIM -O2` | `.elf`, `.bin`, `.hex`, `.fbi` |

---

## 5. Build, Boot, Flash & JTAG Debug Workflows

### Prerequisites
* RISC-V GNU Embedded Toolchain (`riscv-none-elf-gcc`, `riscv-none-elf-gdb`) configured in `toolchain.mk`.
* Microchip OpenOCD (`board/microsemi-riscv.cfg`) for FlashPro6 JTAG debugging.
* Python 3 (`pyserial` for UART console and `mpfs-terminal` / `hss-payload-generator` for HSS boot/flashing).
* USB connections to the Discovery Kit:
  * **`/dev/ttyUSB0` (`MMUART0`):** HSS bootloader console (`115200 8N1`).
  * **`/dev/ttyUSB2` (`MMUART4`):** Application stdio console (`115200 8N1`).

### A. Build & Audit Section Memory Usage
```bash
# Build L2-LIM release payload (0x08040000) or debug ELF (0x08000000)
make BOARD=mpfs-disco-kit lim-release
make BOARD=mpfs-disco-kit lim-debug

# Inspect exact per-region L2-LIM / DDR utilization
make BOARD=mpfs-disco-kit MEM_TARGET=lim-release memusage
make BOARD=mpfs-disco-kit MEM_TARGET=lim-debug memusage
```

### B. Boot via HSS YMODEM (`lim-boot` / `ddr-boot`)
1. Open the application UART console in Terminal 1:
   ```bash
   python3 -m serial.tools.miniterm /dev/ttyUSB2 115200
   ```
2. Power-cycle or reset the board, then stream the HSS payload in Terminal 2:
   ```bash
   # Boot High-LIM release image (0x08040000)
   make BOARD=mpfs-disco-kit lim-boot

   # Or boot DDR release image (0x80000000)
   make BOARD=mpfs-disco-kit ddr-boot
   ```

### C. Program SPI Flash for Autonomous Cold-Boot (`lim-flash` / `ddr-flash`)
Writes the formatted HSS payload to onboard SPI Flash (`FLASH_BOOT_ADDR = 0x00020000`) so HSS boots the application automatically on power-up:
```bash
make BOARD=mpfs-disco-kit lim-flash
# Or for DDR:
make BOARD=mpfs-disco-kit ddr-flash
```

### D. Interactive JTAG Debugging (`openocd` + `debug-lim` / `debug-ddr`)
1. **Terminal 1 — Application UART (`MMUART4`):**
   ```bash
   python3 -m serial.tools.miniterm /dev/ttyUSB2 115200
   ```
2. **Terminal 2 — Start OpenOCD over FlashPro6 JTAG:**
   ```bash
   make BOARD=mpfs-disco-kit openocd
   ```
3. **Terminal 3 — Build, Halt, Load & Attach GDB:**
   ```bash
   # Debug in L2-LIM (0x08000000, -Og)
   make BOARD=mpfs-disco-kit debug-lim

   # Or debug in DDR (0x80000000, -Og)
   make BOARD=mpfs-disco-kit debug-ddr
   ```

---

## 6. Runtime Verification & Network Testing

### Serial Console (`MMUART4` @ `115200` baud)
On startup, verify the execution address (`0x0008000...` for `lim-debug`, `0x0008040...` for `lim-release`, or `0x8000000...` for `ddr-*`), MAC ring initialization, and assigned IPv4 address:
```text
[MAC] GEM0 TBI/SGMII + VSC8221 ready (RX ring=16, avail_rx=16, avail_tx=16)
[DHCP] Started DHCP discovery (8s timeout -> static fallback)...
[NET @ T+8012 ms] Link : UP (1000 Mbps, Full-Duplex)
[NET] IP   : 192.168.20.50
```
Press **`t`** in the UART terminal at any time to inspect the TFTP RAM-disk base address and capacity (`16 KiB` in LIM, `1024 KiB` in DDR).

### Host PC Network Tests
Replace `<board-ip>` with the IPv4 address reported on `MMUART4`:

```bash
# 1. ICMP Echo (Ping)
ping -c 4 <board-ip>

# 2. HTTP Web Server & CGI Control (TCP/80)
curl -i http://<board-ip>/
curl -i "http://<board-ip>/ctrl.cgi?cmd=led_toggle"

# 3. TFTP Binary File Upload & Round-Trip Verification (UDP/69)
# Note: Keep file size <= 16 KiB when running lim-debug or lim-release
tftp -m octet <board-ip> -c put Makefile
tftp -m octet <board-ip> -c get Makefile /tmp/Makefile.rx
diff -s Makefile /tmp/Makefile.rx
```

---

## 7. Key Implementation Notes

* **Minimum GEM0 DMA Ring Size (`16U`):** The PolarFire SoC GEM0 DMA controller caches descriptors in bursts; setting `MSS_MAC_RX_RING_SIZE` or `MSS_MAC_TX_RING_SIZE` below `16U` in `mss_ethernet_mac_sw_cfg.h` stalls packet reception. Always keep `mss_ethernet_mac_sw_cfg.h` synchronized across `platform/drivers/mss/mss_ethernet_mac/` and all `boards/mpfs-disco-kit/platform_config/*/drivers/mss/mss_ethernet_mac/` directories.
* **Single Inclusion of `fsdata_custom.c`:** `middleware/lwip/src/apps/http/fs.c` `#include`s `fsdata_custom.c` directly. `middleware/middleware.mk` intentionally omits `fsdata_custom.c` from `NET_STACK_SRCS_C` to prevent duplicate `file_index_html` linker symbols.
* **HSS L2-LIM Boundary (`0x08040000`):** Never link an HSS-booted image (`lim-release`) below `0x08040000`, and never stream `lim-debug` (`0x08000000`) via `lim-boot`, as zeroing `.bss` at `0x08000000` overwrites HSS's live E51 runtime memory.