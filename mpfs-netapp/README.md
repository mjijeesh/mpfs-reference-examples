# **PolarFire SoC Bare-Metal Network Application (mpfs-netapp) — Technical Reference & Porting Guide**

mpfs-netapp is a bare-metal RISC-V Ethernet reference application for the Microchip PolarFire SoC family. Built on the mpfs\_hal hardware abstraction layer and a cooperative polling (NO\_SYS \= 1\) **lwIP** TCP/IP stack, it decouples board-specific hardware routing from core networking logic so the same codebase executes seamlessly across the **PolarFire SoC Discovery Kit**, **PolarFire SoC Icicle Kit**, or custom hardware—either from on-chip **L2-LIM SRAM** or external **DDR4 memory**.  
Out of the box, the application provides DHCPv4 discovery with automatic static IPv4 fallback, ICMP echo (ping), an embedded HTTP web server (TCP port 80\) with CGI board control, and an in-memory TFTP server (UDP port 69).

## **1\. Glossary of Terms**

If you are new to the PolarFire SoC platform, these terms are essential to understand the architecture:

> * **Hart:** "Hardware Thread." The PolarFire SoC has 5 RISC-V harts (one E51 monitor core, and four U54 application cores).  
> * **HSS (Hart Software Services):** The standard Microchip bootloader. It typically lives in non-volatile memory, runs on the E51 core, initializes DDR memory, and boots your application payload.  
> * **L2-LIM (Loosely Integrated Memory):** A portion of the L2 Cache reconfigured as fast, deterministic on-chip SRAM. Great for debugging without needing DDR memory.  
> * **eNVM:** Embedded Non-Volatile Memory. Where bootloaders like HSS usually reside on the chip.  
> * **GEM (Gigabit Ethernet MAC):** The hardware block inside the PolarFire SoC that handles Ethernet communication.  
> * **PHY:** The external physical layer transceiver chip on the board (e.g., VSC8221 or VSC8662) that connects the GEM to the physical RJ-45 Ethernet jack.  
> * **PLIC:** Platform-Level Interrupt Controller. Routes hardware interrupts (like an incoming network packet) to the RISC-V cores.

## **2\. Prerequisites & Hardware Setup**

### **Software Prerequisites**

> * **SoftConsole:** Microchip's Eclipse-based IDE. It includes the RISC-V GCC toolchain (riscv-none-elf-gcc), GDB, and OpenOCD.  
> * **Libero SoC:** Microchip's FPGA design suite (used for generating hardware configuration headers).  
> * **Serial Terminal:** A program like PuTTY, TeraTerm, or minicom / pyserial (Python) to view the board's UART output.

### **Hardware Connections (Example: Discovery Kit)**

> * **USB Debug Cable:** Connect a Micro-USB cable to J5 on the Discovery Kit. This single cable provides JTAG debugging and enumerates four separate COM ports (UARTs).  
  * **UART 0:** Used by the HSS Bootloader (115200 baud).  
  * **UART 4:** Used by this NetApp application for logs and the CLI (115200 baud).  
> * **Ethernet Cable:** Connect an RJ45 cable from J11 on the Discovery Kit to your router, switch, or directly to your host PC.

## **3\. System Architecture & Multi-Hart Execution Model**

### **Core Architectural Layers**

> * **Board-Agnostic Hardware Abstraction:** Application code (application/) and network stack modules (middleware/) contain zero hardcoded board peripherals. All UART instances, MAC controllers, MDIO buses, PHY drivers, clock enables, and memory maps are injected at compile time from boards/\<board\>/.  
> * **Multi-Hart Asymmetric Execution:**  
  * **E51 (Monitor Hart 0):** In standalone JTAG debug builds (lim-debug), E51 boots from \_start and executes mss\_nwc\_init() to configure System PLLs, clock trees, DDR4, and SGMII before releasing the U54 application harts. In bootloader-assisted flows (lim-release, ddr-debug, ddr-release), Hart Software Services (HSS) remains resident on E51 and dispatches the U54 payload directly.  
  * **U54\_1 (Application Hart 1):** Initializes the board stdio MMUART (115200 8N1), configures the Gigabit Ethernet MAC(s) and PHY(s), starts DHCP discovery, and runs the cooperative superloop (net\_port\_poll() \+ poll\_uart\_cli()).  
  * **U54\_2 – U54\_4 (Secondary Harts 2–4):** Satisfy the mpfs\_hal startup synchronization barrier and enter low-power wfi (Wait For Interrupt) idle loops.  
> * **Lock-Free Interrupt-to-Polling Ethernet Driver (middleware/net\_stack/net\_port.c):**  
  * Drives one or more MSS Gigabit Ethernet MAC instances (GEM0 / GEM1) over TBI/SGMII with dynamic link-speed and duplex auto-negotiation (10/100/1000 Mbps).  
  * Inside mac\_rx\_callback() (PLIC interrupt context), incoming frames are copied into a lock-free ring buffer (rx\_raw\_queue) and the DMA descriptor is immediately recycled to hardware. net\_port\_poll() drains rx\_raw\_queue in main-loop context into lwIP PBUF\_POOL buffers, preventing re-entrancy hazards without needing an RTOS.  
> * **IPv4 & Embedded Services:**  
  * **DHCP \+ Static IPv4 Fallback:** Starts DHCP discovery upon PHY link-up. If no lease is acquired within 8000 ms (DHCP\_FALLBACK\_MS), the stack stops DHCP and applies the board's static IPv4 configuration.  
  * **HTTP Server (TCP/80):** Serves static web pages compiled into fsdata\_custom.c and routes /ctrl.cgi requests to cgi\_ctrl\_handler() in web\_server.c.  
  * **TFTP Server (UDP/69):** Implements binary (octet) file upload (put) and download (get) backed by an aligned static RAM-disk buffer (s\_ramdisk in tftp\_server\_app.c).

### **Repository Layout**

```

mpfs-netapp/`  
├── Makefile                        # Top-level multi-board, multi-target build, flash & debug rules  
├── toolchain.mk                    # RISC-V GCC, OpenOCD, GDB, Python & HSS tool paths  
├── application/                    # 100% Board-Agnostic Core Application  
│   ├── hart1/u54_1.c               # Main network poll loop, boot diagnostics & UART CLI  
│   └── hart2..4/u54_*.c            # Secondary U54 low-power WFI stubs  
├── boards/                         # Board Support Packages (BSP / HAL)  
│   ├── mpfs-disco-kit/             # Microchip PolarFire SoC Discovery Kit (MPFS095T)  
│   │   ├── board.mk                # Silicon metadata, PHY driver sources & board CFLAGS  
│   │   ├── board_config.h          # UART, MAC, PHY, MDIO & static IPv4 definitions  
│   │   ├── board_init.c            # Peripheral clock/reset gating & PLIC setup  
│   │   ├── fpga_design/            # Libero MSS XML configuration & FlashPro .job file  
│   │   ├── fpga_design_config/     # Auto-generated Libero hardware headers (clocks, IO, cache)  
│   │   └── platform_config/        # Target-specific linker scripts & MAC/HAL configs  
   └── mpfs-icicle-kit/            # Microchip PolarFire SoC Icicle Kit (MPFS250T, dual-NIC)  
├── middleware/                     # 100% Board-Agnostic Middleware & Network Stack  
│   ├── middleware.mk               # lwIP core + net_stack source list  
│   ├── lwip/src/                   # Upstream lwIP 2.x core, IPv4, HTTP & TFTP modules  
│   └── net_stack/`  
│       ├── lwipopts.h              # Conditional L2-LIM vs. DDR lwIP memory/protocol configuration 
│       ├── net_port.c / .h         # Multi-port GEM MAC driver, DMA rings & DHCP state machine
│       ├── web_server.c / .h       # HTTP server & CGI endpoint registration  
│       ├── tftp_server_app.c / .h  # TFTP RAM-disk callbacks & capacity reporting 
│       └── fsdata_custom.c         # Embedded HTML assets (included directly by lwIP fs.c) 
├── platform/                       # Microchip PolarFire SoC Platform Drivers & HAL  
│   ├── drivers/mss/                # Bare-metal MSS Ethernet MAC, PHY & MMUART drivers 
│   ├── hal/                        # RISC-V CSR, atomics & IRQ helpers  
│   ├── mpfs_hal/                   # SoC startup, NWC/PLL/DDR/SGMII init & PLIC/CLINT  
│   └── soc_config_generator/       # Python generator converting Libero XML to C headers  
└── tools/`  
    ├── mpfs-terminal.py            # Interactive HSS UART terminal + SFL binary/flash uploader  
    ├── memusage.py                 # ELF-vs-linker-script memory utilization analyzer  
    ├── crcfbigen.py                # Generates .fbi boot images with CRC metadata  
    └── hss-payload-generator/      # Packages ELF binaries into HSS boot payloads (_payload.bin)
```
## **4\. Memory Architecture & Build Targets**

On the Discovery Kit Libero hardware design (LIBERO\_SETTING\_WAY\_ENABLE \= 0x0000000BUL), 12 of the 16 L2 SRAM ways (1.5 MiB) are allocated to L2 Cache, leaving 4 ways (**512 KiB**, 0x0800\_0000 – 0x0807\_FFFF) mapped as on-chip **L2 Loosely Integrated Memory (L2-LIM)**.

> * **Standalone JTAG (lim-debug):** Links at 0x0800\_0000 (LENGTH \= 512k), using the entire L2-LIM region while E51 executes the application startup code.  
> * **HSS High-LIM Payload (lim-release):** Because HSS executes from eNVM (0x2022\_0000) and remains resident on E51 after booting U54, HSS reserves the lower 256 KiB (0x0800\_0000 – 0x0803\_FFFF) for E51 stacks, .bss, and IPI queues. mpfs-lim-app.ld therefore links into the upper **256 KiB High-LIM** (ORIGIN \= 0x08040000, LENGTH \= 256k).  
> * **DDR Targets (ddr-debug / ddr-release):** Link into cached 32-bit DDR4 (ORIGIN \= 0x80000000, LENGTH \= 128M), relying on HSS to train the DDR4 controller prior to loading.

### **Makefile Targets Summary**

The root Makefile infers MEM\_TARGET automatically from convenience goals and places all outputs in build/\<board\>/\<target\>/:

| Make Target | Linker Script | Execution Base | Compile Defines & Opt | Generated Artifacts & Boot Method |
| :---- | :---- | :---- | :---- | :---- |
| lim-debug | mpfs-lim.ld | 0x08000000 (512k) | \-DMEM\_TARGET\_LIM \-Og \-g3 | .elf, .bin, .hex, .fbi (Standalone JTAG via debug-lim) |
| lim-release | mpfs-lim-app.ld | 0x08040000 (256k) | \-DMEM\_TARGET\_LIM \-DIMAGE\_LOADED\_BY\_BOOTLOADER=1 \-O2 | .elf, \_payload.bin (HSS Serialboot / SPI Flash / SD) |
| ddr-debug | mpfs-ddr-loaded-by-boot-loader.ld | 0x80000000 (128M) | \-DMEM\_TARGET\_DDR \-DIMAGE\_LOADED\_BY\_BOOTLOADER=1 \-Og \-g3 | .elf, .bin, .hex, .fbi (HSS-assisted JTAG via debug-ddr) |
| ddr-release | mpfs-ddr-loaded-by-boot-loader.ld | 0x80000000 (128M) | \-DMEM\_TARGET\_DDR \-DIMAGE\_LOADED\_BY\_BOOTLOADER=1 \-O2 | .elf, \_payload.bin (HSS Serialboot / SPI Flash / SD) |
| envm-lim | mpfs-envm-lma-scratchpad-vma.ld | 0x20220100 (eNVM) | \-DMEM\_TARGET\_LIM \-O2 | .elf, .hex (Direct eNVM flash via program-envm-lim) |

## **5\. Detailed Peripheral, MAC & lwIP Configuration**

### **A. Board Build Variables (boards/\<board\>/board.mk)**

```
# boards/mpfs-disco-kit/board.mk 
DIE             := MPFS095T 
PACKAGE         := FCSG325 STDIO_UART      := 4
XML_CONFIG_DIR  ?= $(BOARD_DIR)/fpga_design/design_description
JOB_FILE        ?= $(BOARD_DIR)/fpga_design/mpfs-disco-kit.job
BOARD_SRCS_C     := $(BOARD_DIR)/board_init.c
BOARD_INC_DIRS   := $(BOARD_DIR)
BOARD_PHY_DRIVER := platform/drivers/mss/mss_ethernet_mac/vsc8221_phy.c \
                    platform/drivers/mss/mss_ethernet_mac/mss_ethernet_mac.c \
                    platform/drivers/mss/mss_ethernet_mac/null_phy.c

BOARD_CFLAGS     := -DBOARD_MPFS_DISCO_KIT \
                    -DMPFS_DISCOVERY_KIT \
                    -DTARGET_DISCOVERY_KIT \                  
                    -DTARGET_G5_SOC \                   
                    -DMSS_MAC_SIMPLE_TX_QUEUE \                   
                    -DGEM0_MSS_MDIO
```

### **B. Peripheral & Network Port Mapping (boards/\<board\>/board\_config.h)**

#### **Discovery Kit**

```c
#define APP_BOARD_NAME              "PolarFire SoC Discovery Kit"
/* HSS owns MMUART0; U54 application stdio uses MMUART4 */
#define APP_UART                    (&g_mss_uart4_lo)
/* Discovery Kit: GEM0 + VSC8221 SGMII PHY (TBI, MDIO addr 11) */
#define APP_MAC                     (&g_mac0)
#define APP_MDIO_MAC                (&g_mac0)
#define APP_MAC_IRQn                MAC0_INT_U54_INT
#define APP_MAC_POLL_ISR()          ((void)PLIC_mac0_int_IRQHandler())
#define APP_PHY_NAME                "GEM0 TBI/SGMII + VSC8221"
#define APP_PHY_ADDR                PHY_VSC8221_MDIO_ADDR /* 11U */
#define APP_PCS_PHY_ADDR            SGMII_MDIO_ADDR
#define APP_PHY_INTERFACE           TBI
#define APP_PHY_TYPE                MSS_MAC_DEV_PHY_VSC8221
#define APP_PHY_FLAGS               PHY_VSC8221_EEPROM_INIT
#define APP_PHY_AUTONEGOTIATE       MSS_MAC_VSC8221_phy_autonegotiate
#define APP_PHY_MAC_AUTONEGOTIATE   MSS_MAC_VSC8221_mac_autonegotiate
#define APP_PHY_GET_LINK_STATUS     MSS_MAC_VSC8221_phy_get_link_status
#define APP_PHY_INIT                MSS_MAC_VSC8221_phy_init
#define APP_PHY_SET_LINK_SPEED      MSS_MAC_VSC8221_phy_set_link_speed

/* Default MAC & static fallback IPv4 (used if DHCP times out after 8s) */
#define APP_MAC_ADDR                { 0x00, 0xFC, 0x00, 0x12, 0x34, 0x60 }
#define APP_STATIC_IP               { 192, 168, 20, 50 }
#define APP_STATIC_NETMASK          { 255, 255, 255, 0 }
#define APP_STATIC_GW               { 192, 168, 20, 1 }
```

#### **Icicle Kit Dual-NIC Configuration**

On the Icicle Kit, a dual-port VSC8662 PHY serves both GEM1 (MDIO address 9\) and GEM0 (MDIO address 8), with the shared MDIO bus wired to GEM1. Defining NET\_PORT\_LIST brings up both interfaces.

### **C. Clock, Reset & PLIC Initialization (boards/\<board\>/board\_init.c)**

```c
#include "board_config.h"void board_periph_init(void)
{    SYSREG->SOFT_RESET_CR   = 0U;
   SYSREG->SUBBLK_CLOCK_CR = 0xFFFFFFFFUL;
   (void)mss_config_clk_rst(MSS_PERIPH_MMUART4, (uint8_t)MPFS_HAL_FIRST_HART, PERIPHERAL_ON);
   (void)mss_config_clk_rst(MSS_PERIPH_MAC0,    (uint8_t)MPFS_HAL_FIRST_HART, PERIPHERAL_ON);
   
   PLIC_init();    __enable_irq();
}
```

### **D. Ethernet MAC Driver Configuration (mss\_ethernet\_mac\_sw\_cfg.h)**

```c
#if defined(MEM_TARGET_LIM)
#define MSS_MAC_RX_RING_SIZE (16U)
#define MSS_MAC_TX_RING_SIZE (16U)
#else#define MSS_MAC_RX_RING_SIZE (32U)
#if defined(MSS_MAC_SPEED_TEST)
#define MSS_MAC_TX_RING_SIZE (4001U)
#else#define MSS_MAC_TX_RING_SIZE (1001U)
#endif#endif#define MSS_MAC_64_BIT_ADDRESS_MODE     1
#define MSS_MAC_SIMPLE_TX_QUEUE         1
#define MSS_MAC_TIME_STAMPED_MODE       0
#define MSS_MAC_QUEUE_COUNT             (4)

```

> * **MSS\_MAC\_RX\_RING\_SIZE / MSS\_MAC\_TX\_RING\_SIZE:** Must be at least **16U**. The PolarFire SoC GEM0 DMA controller fetches descriptors in bursts; setting ring sizes below 16U stalls packet reception.

### **E. lwIP Stack Configuration (middleware/net\_stack/lwipopts.h) & Memory Scaling**

| Option / Resource | Purpose | DDR Build | L2-LIM Build (-DMEM\_TARGET\_LIM) |
| :---- | :---- | :---- | :---- |
| NO\_SYS \= 1 | Disables OS threads/mailboxes; stack runs via sys\_check\_timeouts() \+ netif.input(). | Enabled | Enabled |
| MEM\_SIZE | Dynamic heap (mem\_malloc) used by HTTP server state and TCP/DHCP control structures. | 128 KiB | 20 KiB |
| PBUF\_POOL\_SIZE | Pre-allocated 1536 B RX packet buffers drained from rx\_raw\_queue. | 32 (48 KiB) | 8 (12 KiB) |
| MEMP\_NUM\_TCP\_PCB | Concurrent TCP connections supported by the HTTP web server (TCP/80). | 16 | 8 |
| MEMP\_NUM\_SYS\_TIMEOUT | Software timer slots for ARP, TCP, DHCP, and TFTP retransmission timers. | 16 | 12 |
| TFTP RAM-Disk | Static in-memory storage buffer s\_ramdisk in tftp\_server\_app.c. | 1024 KiB (1 MiB) | 16 KiB |

## **6\. Debugging on L2-LIM (lim-debug) — Standalone JTAG**

Use lim-debug when you want standalone JTAG debugging without depending on an external bootloader or pre-trained DDR. debug-lim issues a **hard reset** (monitor reset halt), loads the ELF directly into L2-LIM (0x08000000), and points **all 5 harts** (E51 \+ U54\_1..4) to \_start so E51 configures system clocks and SGMII before waking U54\_1.

> 1. **Terminal 1 — Start the OpenOCD JTAG Server:** make BOARD=mpfs-disco-kit openocd  
> 2. **Terminal 2 — Open the Application Serial Console (MMUART4):** minicom \-b 115200 \-D /dev/ttyUSB2  
> 3. **Terminal 3 — Build, Load into L2-LIM & Run GDB:** make BOARD=mpfs-disco-kit debug-lim

If you prefer to debug straight out of L2-LIM SRAM using OpenOCD and GDB:  
**Terminal 1: OpenOCD**

```sh
make BOARD=mpfs-disco-kit openocd
```

```
xPack OpenOCD (Microchip SoftConsole build), x86_64 Open On-Chip Debugger
Info : only one transport option; autoselect 'jtag'
Info : Hardware thread awareness created
Info : Listening on port 6666 for tcl connections
Info : Listening on port 4444 for telnet connections
Info : clock speed 6000 kHz
Info : JTAG tap: mpfs.cpu tap/device found: 0x0f8181cf
Info : Examined RISC-V core; found 5 harts
Info : Listening on port 3333 for gdb connections
```

**Terminal 2: GDB Launch**

```sh
make BOARD=mpfs-disco-kit debug-lim
```

```
GNU gdb (xPack GNU RISC-V Embedded GCC x86_64) 16.3
Reading symbols from build/mpfs-disco-kit/lim-debug/mpfs-net-app.elf...
Remote debugging using localhost:3333
0x0000000080002fb0 in ?? ()
Loading section .text_init, size 0xee0 lma 0x8000000
Loading section .text, size 0x183e0 lma 0x8000ee0
Loading section .ram_code, size 0x50 lma 0x80192c0
Loading section .sdata, size 0x30 lma 0x8019310
Loading section .data, size 0xf40 lma 0x8019340
Start address 0x0000000008000000, load size 107136
Transfer rate: 17 KB/sec, 9739 bytes/write.

Thread 5 (Thread 5 (Name: mpfs.hart4_u54_4, state: debug-request)):
Thread 4 (Thread 4 (Name: mpfs.hart3_u54_3, state: debug-request)):
Thread 3 (Thread 3 (Name: mpfs.hart2_u54_2, state: debug-request)):
Thread 2 (Thread 2 (Name: mpfs.hart1_u54_1, state: debug-request)):
Thread 1 (Thread 1 (Name: mpfs.hart0_e51, state: debug-request)):
Continuing.
```

## **7\. Debugging on DDR (ddr-debug) — HSS-Assisted JTAG**

Use ddr-debug when debugging with the full 128 MiB DDR4 memory map, large MAC rings (32 RX / 1001 TX descriptors), and the 1 MiB TFTP RAM-disk.  
**Critical Prerequisite:** DDR4 memory (0x80000000) is uninitialized immediately after power-on. Pre-flashed **Hart Software Services (HSS)** must run on E51 first to initialize PLLs and train the DDR4 controller. Accordingly, debug-ddr uses a **soft halt** (monitor halt, never reset halt) and only redirects **threads 2–5 (U54\_1–U54\_4)** to \_start, leaving **thread 1 (E51)** safely parked inside HSS.

> 1. Reset the Board & Wait \~2 Seconds for HSS to finish DDR4 training.  
> 2. Start OpenOCD (Terminal 1\) and App UART (Terminal 2).  
> 3. **Terminal 3 — Build, Load into DDR & Run GDB:** make BOARD=mpfs-disco-kit debug-ddr

## **8\. Running from DDR or L2-LIM via HSS (Release Builds)**

### **Step 1 — Build the ELF and HSS Payload Image**

```sh
make BOARD=mpfs-disco-kit clean ddr-release
```

\# OR for L2-LIM: 

```sh
make BOARD=mpfs-disco-kit clean lim-release
```

```sh
 [CC]  platform/mpfs_hal/common/nwc/mss_pll.c
 [CC]  platform/mpfs_hal/common/nwc/mss_sgmii.c
 [CC]  platform/mpfs_hal/startup_gcc/newlib_stubs.c
 [CC]  platform/mpfs_hal/startup_gcc/system_startup.c
 [AS]  platform/hal/hw_reg_access.S
 [AS]  platform/mpfs_hal/startup_gcc/mss_entry.S
 [AS]  platform/mpfs_hal/startup_gcc/mss_utils.S
 [LD]  build/mpfs-disco-kit/ddr-release/mpfs-net-app.elf
 [HSS] Generating boot payload: build/mpfs-disco-kit/ddr-release/mpfs-net-app_payload.bin
 Hart Software Service formatted boot image generator v0.99.53
 Copyright (c) 2021-2026 Microchip FPGA Embedded Systems Solutions.Set-name is >>PolarFire-SoC-HSS::mpfs-net-app<<
 Output filename is >>build/mpfs-disco-kit/ddr-release/mpfs-net-app_payload.bin<<   
 text       data        bss        dec        hex    filename
 80272       3728    1410208    1494208     16ccc0    
 build/mpfs-disco-kit/ddr-release/mpfs-net-app.elf
```

This invokes hss-payload-generator to create mpfs-net-app\_payload.bin with the 0xb007c0de HSS boot header.

### **Step 2 — Launch mpfs-terminal.py on the HSS UART (MMUART0)**

```sh
python3 tools/mpfs-terminal.py /dev/ttyUSB1 --kernel build/mpfs-disco-kit/ddr-release/mpfs-net-app_payload.bin
```

### **Step 3 — Enter HSS TinyCLI & Trigger serialboot**

Reset the board and press any key inside mpfs-terminal.py to enter the HSS \>\> prompt. Execute serialboot. HSS emits the SFL magic trigger, mpfs-terminal.py automatically replies with the handshake ACK and transfers the payload with CRC16 verification, and HSS parses the payload chunks and boots U54\_1.  
**Command:** python3 tools/mpfs-terminal.py /dev/ttyUSB1 \--kernel build/mpfs-disco-kit/ddr-release/mpfs-net-app\_payload.bin  
**Purpose:** Opens the HSS bootloader UART port, waits for the user to type serialboot in the HSS console, and then streams the binary to DDR memory using the SFL protocol.  
**Expected Terminal Output:**

```
[MPFS-TERM] Opened /dev/ttyUSB1 at 115200 baud. (Press Ctrl+C to exit)
[MPFS-TERM] Type 'serialboot' on prompt to trigger download.

>> serialboot

[SERIALBOOT] Staging buffer @ 0x0080000000. Waiting for SFL host...
sL5DdSMmkekro[MPFS-TERM] Magic trigger matched! Replying with handshake ACK...
[MPFS-TERM] Uploading build/mpfs-disco-kit/ddr-release/mpfs-net-app_payload.bin (85936 bytes) -> 0x80000000...
|====================| 100.0% (85936/85936 B)
[MPFS-TERM] Transfer Complete (7.7 KB/s).
[MPFS-TERM] Sending JUMP Command to boot application...
[MPFS-TERM] Terminal active. Continuing console output...
[SERIALBOOT] Transfer complete (85936 bytes received, host base: 0x80000000).
[PAYLOAD] Detected HSS Boot Image header (magic: 0xb007c0de).
[PAYLOAD] Set Name : "PolarFire-SoC-HSS::mpfs-net-app"
[PAYLOAD] Image Len: 85936 bytes
[PAYLOAD]   Chunk[0]: Hart=1  Load=0x0000000790  Exec=0x0080000000  Size=80272 bytes
[PAYLOAD]   Chunk[1]: Hart=1  Load=0x0000014120  Exec=0x0080013990  Size=32 bytes
[PAYLOAD]   Chunk[2]: Hart=1  Load=0x0000014140  Exec=0x00800139b0  Size=3696 bytes
[PAYLOAD] Registering boot image and dispatching U54 harts...
[368.609040] boot_service(u54_1):: BOOT_FLAG_SKIP_OPENSBI found
[368.668524] boot_service(u54_1) :: [SetupPMP] -> [SetupPMPComplete]
[368.701369] u54 State Change:  [Booting] [Booting] [Booting] [Booting]
[368.710917] boot_service(u54_1) :: [SetupPMPComplete] -> [ZeroInit]
[368.757311] boot_service(u54_1) :: [ZeroInit] -> [Download]
[368.787101] boot_service(u54_1)::Processing boot image: "mpfs-net-app"
[368.852878] u54 State Change:  [Booting] [Running] [Running] [Running]
```

*Optional: To store the payload in onboard SPI Flash, pass \--flash \--kernel-adr 0x00020000 to mpfs-terminal.py.*

### **C. Application Execution Output (App UART)**

**Command:** Open a serial viewer on the App UART (e.g., minicom \-D /dev/ttyUSB2 \-b 115200\) before you trigger the boot.  
**Purpose:** Monitors the U54 application logs, DHCP state machine, and provides an interactive CLI.  
**Expected Terminal Output:**

```
========================================================
 PolarFire SoC Discovery Kit - Bare-Metal lwIP NetApp
  Entry @ 0x0080000566 | Boot T+64756 ms | Press 'h' for help
  ========================================================
  [INIT 1/7] SYSREG CLOCK_CR=0x3ffdffff  SOFT_RESET_CR=0x00000000
  [MAC e0] GEM0 TBI/SGMII + VSC8221 ready (RX ring=32, avail_rx=0, avail_tx=1001)
  [DHCP e0] Started DHCP discovery (8s timeout -> static fallback)...[NET e0 @ T+68303 ms] GEM0 TBI/SGMII + VSC8221
  [NET e0] Link : UP (1000 Mbps, Full-Duplex)
  [NET e0] MAC  : 00:04:a3:11:22:33 (PHY addr 11)
  [NET e0] IP   : 0.0.0.0 | Mask: 0.0.0.0 | GW: 0.0.0.0
  [NET e0] Pkts : RX=1  TX=1
  [INIT 7/7] Starting HTTP (TCP/80) & TFTP (UDP/69) @ T+68323 ms...
  [APP] Initialization complete. Entering main poll loop.
  [DHCP e0] Lease acquired![NET e0 @ T+76034 ms] GEM0 TBI/SGMII + VSC8221
  [NET e0] Link : UP (1000 Mbps, Full-Duplex)
  [NET e0] MAC  : 00:04:a3:11:22:33 (PHY addr 11)
  [NET e0] IP   : 192.168.20.76 | Mask: 255.255.255.128 | GW: 192.168.20.1
  [NET e0] Pkts : RX=24  TX=7
```

*Note: If you press 't' in the terminal, you will see the TFTP capacity report (e.g., Cap: 1024 KiB).*

## **9\. Host PC Network Testing Results**

After the board boots and acquires an IP (e.g., 192.168.20.76), run these tests from your host PC.

### **A. Ping Test (ICMP)**

```sh
ping -c 4 192.168.20.76
```

```
PING 192.168.20.76 (192.168.20.76) 56(84) bytes of data.
64 bytes from 192.168.20.76: icmp_seq=1 ttl=255 time=0.879 ms
64 bytes from 192.168.20.76: icmp_seq=2 ttl=255 time=3.84 ms
64 bytes from 192.168.20.76: icmp_seq=3 ttl=255 time=5.21 ms
64 bytes from 192.168.20.76: icmp_seq=4 ttl=255 time=2.53 ms

--- 192.168.20.76 ping statistics ---
4 packets transmitted, 4 received, 0% packet loss, time 3021ms
rtt min/avg/max/mdev = 0.879/3.114/5.212/1.601 ms
```

### **B. Web Server CGI Toggle Test (HTTP TCP/80)**

```sh
curl -i "http://192.168.20.76/ctrl.cgi?cmd=led_toggle"
```

```html
HTTP/1.0 200 OK
Content-Type: text/html
Connection: close

<!DOCTYPE html><html><head><title>PolarFire SoC U54 Server</title>
```

**Expected Terminal Output (on the Board's UART):**

```
[HTTP-CGI] cmd = led_toggle
```

### **C. TFTP RAM-Disk Transfer (UDP/69)**

```sh
tftp -m octet 192.168.20.76 -c put Makefile
tftp -m octet 192.168.20.76 -c get Makefile /tmp/Makefile.rx
diff -s Makefile /tmp/Makefile.rx
```

```
Files Makefile and /tmp/Makefile.rx are identical
```

## **10\. Porting mpfs-netapp to a New Custom Board**

Because hardware configuration is isolated under boards/\<board\>/, supporting a new PolarFire SoC board requires **zero changes** to application/, middleware/, or the root Makefile. Follow these steps:

> 1. **Create the Board Directory Structure:** mkdir \-p boards/my-custom-board/{fpga\_design/design\_description,fpga\_design\_config,platform\_config}  
> 2. **Add the Libero SoC MSS Hardware Export:** Export the XML to boards/my-custom-board/fpga\_design/design\_description/. The build system will generate the headers.  
> 3. **Update platform\_config/ Linker Scripts & MAC Driver Config:** Verify L2-LIM Size in Linker Scripts based on LIBERO\_SETTING\_WAY\_ENABLE. Enable your PHY Driver in mss\_ethernet\_mac\_sw\_cfg.h.  
> 4. **Create boards/my-custom-board/board.mk:** Specify your target silicon, package, stdio UART number, PHY driver C file, and MAC MDIO routing flag.  
> 5. **Map Peripherals & Ethernet Ports in boards/my-custom-board/board\_config.h.**  
> 6. **Implement boards/my-custom-board/board\_init.c:** Un-gate clocks and resets for the exact MMUART and GEM peripherals configured in board\_config.h.  
> 7. **Build & Test Your Custom Board:** make BOARD=my-custom-board lim-debug

## **11\. Critical Implementation Notes & Troubleshooting**

> * **Minimum GEM0/GEM1 DMA Ring Size (16U):** Never reduce MSS\_MAC\_RX\_RING\_SIZE or MSS\_MAC\_TX\_RING\_SIZE below 16U, as the PolarFire SoC GEM DMA descriptor cache requires at least 16 descriptors to operate reliably. Keep mss\_ethernet\_mac\_sw\_cfg.h synchronized across all platform\_config/\* subdirectories.  
> * **Single Inclusion of fsdata\_custom.c:** lwIP's middleware/lwip/src/apps/http/fs.c directly \#includes fsdata\_custom.c when HTTPD\_USE\_CUSTOM\_FSDATA \= 1\. middleware/middleware.mk intentionally excludes fsdata\_custom.c from NET\_STACK\_SRCS\_C to avoid duplicate symbol linker errors.  
> * **HSS L2-LIM Boundary (0x08040000):** Never load lim-debug (0x08000000) through HSS serialboot, and never link an HSS-booted L2-LIM binary below 0x08040000—zeroing .bss at 0x08000000 overwrites HSS's live E51 runtime data and hangs the SoC.  
> * **DDR Debug Hard-Reset Trap:** Never use debug-lim (which runs monitor reset halt) to debug a DDR target, and never run debug-ddr before HSS has finished DDR4 training (\~2 seconds after power-up), or GDB memory writes to 0x80000000 will fail with bus faults.