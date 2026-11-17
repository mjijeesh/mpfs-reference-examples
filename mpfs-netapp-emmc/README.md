
# Guide to Bare-Metal Ethernet on PolarFire SoC - DualMAC

Welcome! This guide will walk you through building, configuring, and running a dual-port bare-metal Ethernet application on the **Microchip PolarFire SoC Icicle Kit**. 

If you are new to Linux or PolarFire SoC, this document is designed for you. It explains *what* we are doing, *why* we are doing it, and provides the exact terminal commands you need.

---

## 1. Concepts & Terminology

Before diving into code, let's clarify a few terms:
* **Bare-Metal:** Running code directly on the processor without an Operating System (like Linux or FreeRTOS). We manage the hardware directly.
* **lwIP (Lightweight IP):** A tiny, open-source TCP/IP networking stack designed for embedded systems.
* **HSS (Hart Software Services):** The bootloader for PolarFire SoC. It runs on the monitor core (E51), sets up the DDR memory, and loads our bare-metal application into the application cores (U54).
* **MAC (Media Access Control) & PHY (Physical Layer):** The MAC (inside the PolarFire chip, named GEM0 and GEM1) handles digital Ethernet packets. The PHY (an external chip, the VSC8662) translates these packets into electrical signals for the RJ45 cable.
* **MDIO (Management Data Input/Output):** A simple two-wire bus used by the MAC to read/write configuration settings to the PHY.

---

## 2. Hardware Architecture (The "Shared MDIO" Quirk)

The Icicle Kit has two Ethernet ports (Port J1 and Port J2), driven by a single dual-port PHY chip (Microchip VSC8662). 

Here is the hardware quirk you must understand: **Both ports share a single MDIO management bus, and that bus is physically wired only to GEM1 (Port J2).**

| Feature | Port J2 (GEM1) | Port J1 (GEM0) |
| :--- | :--- | :--- |
| **Data Connection** | Native to GEM1 | Native to GEM0 |
| **MDIO Control** | Native to GEM1 | **Routed through GEM1** |
| **PHY Address** | `9U` (Channel 1) | `8U` (Channel 0) |

**What this means for software:** We *must* turn on and configure GEM1 (Port J2) first. If we don't, GEM0 (Port J1) cannot talk to the PHY, and its link will stay down.

---

## 3. Ethernet PHY & MAC Bring-Up Settings

To make the hardware work, we configure three specific areas in the code.

### A. Clocks and Resets (`boards/mpfs-icicle-kit/board_init.c`)
Before we can use the Ethernet ports, we must turn on their power/clocks and bring them out of reset. We do this simultaneously for both MACs:
```c
mss_config_clk_rst(MSS_PERIPH_MAC0, (uint8_t)MPFS_HAL_FIRST_HART, PERIPHERAL_ON);
mss_config_clk_rst(MSS_PERIPH_MAC1, (uint8_t)MPFS_HAL_FIRST_HART, PERIPHERAL_ON);
```

### B. Board Configuration (`boards/mpfs-icicle-kit/board_config.h`)
We define a list called `NET_PORT_LIST`. We place GEM1 (Port J2) first in this list so it gets initialized first. We also tell GEM0 to use GEM1's MDIO controller:
```c
// Inside the configuration for GEM0 (Port J1):
.mac_dev  = &g_mac0,  // Use GEM0 for data
.mdio_mac = &g_mac1,  // Borrow GEM1 for PHY configuration!
.phy_addr = 8U,       // Channel 0
```

### C. Ring Buffers (`mss_ethernet_mac_sw_cfg.h`)
The Ethernet hardware uses DMA (Direct Memory Access) to move packets into memory without bothering the CPU. DMA requires "Ring Buffers". 
*Note: The PolarFire SoC requires at least 16 descriptors per ring to function correctly. Anything less will cause packets to drop silently!*
```c
#define MSS_MAC_RX_RING_SIZE (32U) 
#define MSS_MAC_TX_RING_SIZE (16U)
```

---

## 4. lwIP Networking Configuration (`lwipopts.h`)

Our application uses lwIP in a `NO_SYS = 1` mode (meaning no RTOS/Operating System threads). 

### A. Memory Sizing
Because we are booting into external DDR memory (which is huge), we can give lwIP generous memory limits for high-speed transfers:
* `PBUF_POOL_SIZE = 32`: Pre-allocates 32 packet buffers.
* `MEMP_NUM_TCP_PCB = 16`: Allows up to 16 simultaneous HTTP web server connections.
* `MEMP_NUM_UDP_PCB = 8`: Allows multiple TFTP file transfers and DHCP.

### B. Source-Based Routing
If you plug both J1 and J2 into the same network switch (e.g., your home router), they will both get IP addresses on the same subnet (like `192.168.1.51` and `192.168.1.52`). 

Normally, lwIP sends all outbound replies out of the *first* port it finds. This means if you ping Port J1, the reply might exit out of Port J2, causing networking errors! To fix this, we enable a special hook to route replies out of the exact port that received them:
```c
struct netif *net_port_route_src(const struct ip4_addr *src, const struct ip4_addr *dest);
#define LWIP_HOOK_IP4_ROUTE_SRC(src, dest) net_port_route_src((src), (dest))
```

---

## 5. How the Application Code Works

Because there is no Operating System, the code uses an infinite loop (a "Superloop") to process data.

### `u54_1.c` (The Main Function)
This is where the application starts. 
1. Initializes the hardware and UART (Serial Console).
2. Initializes the network stack (`net_port_init()`).
3. Starts the HTTP (Web) and TFTP (File Transfer) servers.
4. Enters an infinite `while(1)` loop, constantly calling `net_port_poll()` to check for new network packets, and `poll_uart_cli()` to check if you pressed a key on your keyboard.

### `net_port.c` (The Network Manager)
This file connects the Microchip hardware drivers to the lwIP software stack safely.
* **Interrupts (`mac_rx_callback`):** When a packet arrives, the hardware triggers a hardware interrupt. Our code simply copies the packet into a temporary lock-free software queue (`rx_raw_queue`) and immediately exits. It does **not** process the packet here to keep the system responsive.
* **Polling (`net_port_poll`):** Inside the safe `while(1)` loop, this function drains the temporary queue and passes the packets up into the lwIP stack (`netif->input()`). It also checks if the Ethernet cable was unplugged and processes DHCP IP assignments.

---

## 6. Building the Project

You need a Linux environment with the RISC-V GNU Toolchain (`riscv-none-elf-gcc`) installed.

1. **Open a terminal** and navigate to your project folder:
   ```bash
   cd ~/git/mpfs-reference-examples/mpfs-netapp
   ```
2. **Make the payload generator executable** (Linux requires explicit permission to run scripts):
   ```bash
   chmod +x tools/hss-payload-generator/hss-payload-generator
   ```
3. **Build the application** for the external DDR memory target:
   ```bash
   make BOARD=mpfs-icicle-kit clean ddr-release
   ```
   *Expected Output: The compiler will run and eventually print a table showing memory usage, creating a file named `mpfs-net-app_payload.bin`.*

---

## 7. Loading the Application onto the Board

You can load your compiled binary onto the board using the pre-installed HSS bootloader in two ways:

### Option A: Loading via HSS SerialBoot (Over USB) - *Best for Beginners*
This sends the file over the USB cable. 
1. Ensure the Icicle Kit's Micro-USB is connected to your PC.
2. In a Linux terminal, run the provided Python script to trigger the upload to the HSS serial port (usually `/dev/ttyUSB0`):
   ```bash
   python3 tools/mpfs-terminal.py /dev/ttyUSB0 \
       --kernel build/mpfs-icicle-kit/ddr-release/mpfs-net-app_payload.bin \
       --serial-boot
   ```
   *The script will say "Uploading...", write the file into the board's RAM, and command the board to boot.*

### Option B: Loading via HSS Netboot (Over Network)
This is much faster but requires a TFTP server on your Linux PC.
1. Copy `mpfs-net-app_payload.bin` to your PC's TFTP folder and rename it to `boot.bin`.
2. Open a standard serial terminal to the HSS console:
   ```bash
   python3 -m serial.tools.miniterm /dev/ttyUSB0 115200
   ```
3. Press a key on your keyboard to stop the default boot process and enter the `TinyCLI` prompt.
4. Tell the board to download the file from your PC's IP address (e.g., `192.168.20.94`):
   ```text
   >> netboot 192.168.20.94 boot.bin
   ```

---

## 8. Expected Outputs & Testing

To see the application output, open a second terminal window to the **Application Console** (`MMUART1`, usually `/dev/ttyUSB1` at 115200 baud):

```bash
python3 -m serial.tools.miniterm /dev/ttyUSB1 115200
```

**Expected Boot Log:**
```text
========================================================
 PolarFire SoC Icicle Kit - Bare-Metal lwIP NetApp
========================================================
[INIT 1/7] SYSREG CLOCK_CR=0xffffffff  SOFT_RESET_CR=0x00000000
[MAC e0] GEM1 TBI/SGMII + VSC8662 (J2) ready
[DHCP e0] Started DHCP discovery...
[MAC e1] GEM0 TBI/SGMII + VSC8662 (J1) ready
[DHCP e1] Started DHCP discovery...
[APP] Initialization complete. Entering main poll loop.
```

**Plug Ethernet cables into J1 and J2.** You will see:
```text
[PHY e0] Link UP -> 1000 Mbps Full-Duplex
[PHY e1] Link UP -> 1000 Mbps Full-Duplex
[DHCP e0] Lease acquired!
[NET e0] IP   : 192.168.20.155 | Mask: 255.255.255.0
[DHCP e1] Lease acquired!
[NET e1] IP   : 192.168.20.156 | Mask: 255.255.255.0
```

### Run Tests from your Linux PC:

1. **Ping the Board:** 
   ```bash
   ping -c 4 192.168.20.155
   ```
   *(Expected: 0% packet loss).*

2. **Test the Web Server:**
   ```bash
   curl -i http://192.168.20.155/
   ```
   *(Expected: Outputs raw HTML code from the board's embedded web server).*

3. **Test the TFTP Server (File Transfer):**
   *(Note: The `-m octet` flag is critical to force binary mode).*
   ```bash
   tftp -m octet 192.168.20.155 -c put Makefile
   ```
   *(Expected: Instantly uploads your local Makefile into the board's RAM).*

**Tip:** At any time, you can press **`s`** in your `miniterm` application console window to print the current packet counters (TX/RX) for both ports!

## Feature Update: L2-LIM Execution Support (`lim-app`)

This update enables the bare-metal lwIP network application (DHCP, ICMP Ping, HTTP Server on TCP/80, and TFTP RAM-Disk Server on UDP/69) to execute directly from on-chip **L2 Loosely Integrated Memory (L2-LIM)** on the **PolarFire SoC Discovery Kit** when booted via Hart Software Services (HSS).

---

### 1. L2-LIM Hardware Memory Map & HSS Constraints

On the Discovery Kit hardware design (`LIBERO_SETTING_WAY_ENABLE = 0x0000000BUL`), 12 of the 16 L2 SRAM ways (1.5 MiB) are allocated to L2 Cache, leaving 4 ways (**512 KiB**, `0x0800_0000` – `0x0807_FFFF`) mapped as L2-LIM.

Because HSS executes from eNVM (`0x2022_0000`) and remains resident on **E51** in the background after booting the U54 payload, HSS reserves the lower 256 KiB of L2-LIM for its runtime data, `.bss`, E51 stack, and IPI queues. The `lim-app` target (`-DMEM_TARGET_LIM -DIMAGE_LOADED_BY_BOOTLOADER=1`) therefore links into the upper **256 KiB High-LIM** region:

| Address Range | Size | Owner | Allocation |
| :--- | :--- | :--- | :--- |
| `0x0800_0000` – `0x0803_FFFF` | **256 KiB** | **HSS (E51 Runtime)** | HSS `.data`/`.bss`, E51 stack, IPI queues, boot scratchpad |
| `0x0804_0000` – `0x0807_FFFF` | **256 KiB** | **U54 Application (`lim-app`)** | `.text`, `.rodata`, `.data`, `.bss`, heap, and hart stacks |

---

### 2. Memory Footprint Scaling (`ddr` vs. `lim-app`)

To fit the entire application code, Ethernet DMA buffers, lwIP pools, TFTP RAM-disk, heap, and hart stacks inside the **256 KiB** High-LIM budget without altering the high-throughput `ddr` build, memory allocations are conditionally scaled when `-DMEM_TARGET_LIM` is active:

| Component | File | `ddr` Build | `lim-app` Build | L2-LIM Footprint |
| :--- | :--- | :--- | :--- | :--- |
| **MAC RX DMA Ring** | `mss_ethernet_mac_sw_cfg.h` | `32U` descriptors | `16U` descriptors (GEM0 DMA min) | — |
| **MAC TX DMA Ring** | `mss_ethernet_mac_sw_cfg.h` | `1001U` descriptors | `16U` descriptors (GEM0 DMA min) | — |
| **MAC RX Buffer Pool** | `net_port.c` | `32 × 10,240 B` (320 KiB) | `16 × 1,536 B` (`RX_SLOT_SIZE`) | **24,576 B** |
| **MAC TX Buffer Pool** | `net_port.c` | `8 × 1,536 B` (12 KiB) | `4 × 1,536 B` (`TX_RING_SLOTS`) | **6,144 B** |
| **ISR-to-Poll RX Queue** | `net_port.c` | `64` frames (~95 KiB) | `8` frames (`RX_QUEUE_SIZE`) | **12,192 B** |
| **lwIP Dynamic Heap** | `lwipopts.h` | `128 KiB` | `24 KiB` (`MEM_SIZE`) | **~24.0 KiB** |
| **lwIP PBUF Pool** | `lwipopts.h` | `32 × 1,536 B` (48 KiB) | `12 × 1,536 B` (`PBUF_POOL_SIZE`) | **~18.3 KiB** |
| **TFTP RAM-Disk** | `tftp_server_app.c` | `1 MiB` (`1024 KiB`) | `16 KiB` (`TFTP_RAMDISK_BYTES`) | **16,384 B** |
| **Heap & Hart Stacks** | `mpfs-lim-app.ld` | `52 KiB` total | `U54_1=8k`, `E51/U54_2..4=1k`, `Heap=4k`, `Common=2k` | **18,432 B** |

---

### 3. File-by-File Configuration Changes

#### A. Linker Script (`boards/mpfs-disco-kit/platform_config/lim-release/linker/mpfs-lim-app.ld`)
Keep `l2lim` anchored at `ORIGIN = 0x08040000, LENGTH = 256k` and reclaim **34 KiB** from idle harts (`E51` is managed by HSS; `u54_2`–`u54_4` execute an idle `wfi` loop) while preserving an `8k` stack for `u54_1`:

```ld
MEMORY
{
    /* High LIM target: 256KB offset within 512KB LIM region (HSS uses 0x08000000-0x0803FFFF) */
    l2lim (rwx)                : ORIGIN = 0x08040000, LENGTH = 256k
    ddr_cached_32bit (rwx)     : ORIGIN = 0x80000000, LENGTH = 128M
    ddr_non_cached_32bit (rwx) : ORIGIN = 0xC0000000, LENGTH = 128M
    ddr_wcb_32bit (rwx)        : ORIGIN = 0xD0000000, LENGTH = 256M
}

HEAP_SIZE               = 4k;
SIZE_OF_COMMON_HART_MEM = 2k;

STACK_SIZE_E51_APPLICATION   = 1k;
STACK_SIZE_U54_1_APPLICATION = 8k;
STACK_SIZE_U54_2_APPLICATION = 1k;
STACK_SIZE_U54_3_APPLICATION = 1k;
STACK_SIZE_U54_4_APPLICATION = 1k;

```

#### **B. Ethernet MAC Ring Configuration (`mss_ethernet_mac_sw_cfg.h`)**

GEM0's DMA descriptor cache requires at least `16U` descriptors per ring. Scale the RX/TX rings conditionally for `MEM_TARGET_LIM` in `platform/drivers/mss/mss_ethernet_mac/mss_ethernet_mac_sw_cfg.h` and remove any duplicate `(4U)` macro definitions:

C

```
#if defined(MEM_TARGET_LIM)
#define MSS_MAC_RX_RING_SIZE (16U)
#define MSS_MAC_TX_RING_SIZE (16U)
#else
#define MSS_MAC_RX_RING_SIZE (32U)
#if defined(MSS_MAC_SPEED_TEST)
#define MSS_MAC_TX_RING_SIZE (4001U)
#else
#define MSS_MAC_TX_RING_SIZE (1001U)
#endif
#endif
```

Sync this header across all board build configurations (`lim-release`, `lim-debug`, `ddr-release`, `envm-scratchpad-release`) so `CONFIG_DIR` never picks up a stale copy:

Bash

```
find boards -name "mss_ethernet_mac_sw_cfg.h" \
  -exec cp -f platform/drivers/mss/mss_ethernet_mac/mss_ethernet_mac_sw_cfg.h {} \;
```

#### **C. MAC Driver Buffer & Software Queues (`middleware/net_stack/net_port.c`)**

Replace the jumbo-frame RX buffer stride (`MSS_MAC_MAX_RX_BUF_SIZE = 10240U`) with standard MTU-aligned `RX_SLOT_SIZE` (`1536U`, saving **136 KiB** in L2-LIM) and scale the software TX/RX ring queues under `MEM_TARGET_LIM`:

C

```
#define PACKET_MAX              1518U
#define RX_SLOT_SIZE            1536U
#define TX_SLOT_SIZE            1536U
#define ETH_MIN_LEN             60U

#if defined(MEM_TARGET_LIM)
#define TX_RING_SLOTS           4U
#define RX_QUEUE_SIZE           8U
#else
#define TX_RING_SLOTS           8U
#define RX_QUEUE_SIZE           64U
#endif

static uint8_t g_mac_rx_buffer[MSS_MAC_RX_RING_SIZE][RX_SLOT_SIZE] __attribute__((aligned(16)));
static uint8_t g_mac_tx_buffer[TX_RING_SLOTS][TX_SLOT_SIZE] __attribute__((aligned(16)));
```

#### **D. lwIP Heap & Pool Sizing (`middleware/net_stack/lwipopts.h`)**

Scale lwIP pools under `MEM_TARGET_LIM` while maintaining enough `MEMP_NUM_SYS_TIMEOUT` slots (`12`) for ARP, TCP, DHCP, and TFTP timers simultaneously:

C

```
#if defined(MEM_TARGET_LIM)
#define MEM_SIZE                        (24 * 1024)
#define MEMP_NUM_PBUF                   16
#define MEMP_NUM_UDP_PCB                6
#define MEMP_NUM_TCP_PCB                8
#define MEMP_NUM_TCP_SEG                16
#define MEMP_NUM_SYS_TIMEOUT            12
#define PBUF_POOL_SIZE                  12
#else
#define MEM_SIZE                        (128 * 1024)
#define MEMP_NUM_PBUF                   32
#define MEMP_NUM_UDP_PCB                8
#define MEMP_NUM_TCP_PCB                16
#define MEMP_NUM_TCP_SEG                32
#define MEMP_NUM_SYS_TIMEOUT            16
#define PBUF_POOL_SIZE                  32
#endif
```

#### **E. TFTP RAM-Disk Capacity (`middleware/net_stack/tftp_server_app.c`)**

Scale the static RAM-disk buffer to `16 KiB` in L2-LIM (`1 MiB` in DDR):

C

```
#if defined(MEM_TARGET_LIM)
#define TFTP_RAMDISK_BYTES  (16U * 1024U)       /* 16 KiB in L2-LIM SRAM */
#else
#define TFTP_RAMDISK_BYTES  (1024U * 1024U)     /* 1 MiB in DDR */
#endif
```

### **4\. Building, Booting & Testing `lim-app`**

#### **1\. Build & Audit L2-LIM Memory Usage**

Bash

```
make BOARD=mpfs-disco-kit clean lim-app
make BOARD=mpfs-disco-kit MEM_TARGET=lim-app memusage
```

Verify that `l2lim` usage stays below `256 KiB` (`0x40000` bytes).

#### **2\. Boot via HSS YMODEM (`lim-app-boot`)**

1. **Terminal 1 — Open Application Console (`MMUART4`):**  
2. Bash

```
python3 -m serial.tools.miniterm /dev/ttyUSB2 115200
```

3.   
4. **Terminal 2 — Upload & Boot `lim-app` Payload via HSS (`MMUART0`):**  
5. Bash

```
make BOARD=mpfs-disco-kit lim-app-boot
```

6.   
7. **Verify L2-LIM Execution on `/dev/ttyUSB2`:**  
   * Confirm the boot banner prints **`Entry @ 0x0008040...`** (confirming execution from High-LIM rather than DDR `0x80000000`).  
   * Confirm MAC initialization logs **`[MAC] GEM0 TBI/SGMII + VSC8221 ready (RX ring=16, ...)`**.  
   * Press **`t`** in the UART console to verify **`[TFTP] Buffer Addr : 0x00080... (Cap: 16 KiB)`**.

#### **3\. Optional: Program to SPI Flash for Cold-Boot (`lim-flash`)**

Once verified over YMODEM, flash the `lim-app` HSS payload into onboard SPI Flash (`0x00020000`) so the board boots autonomously into L2-LIM on power-cycle:

Bash

```
make BOARD=mpfs-disco-kit lim-flash
```

#### **4\. Functional Network Verification (Host PC)**

Replace `<board-ip>` with the IPv4 address printed on `/dev/ttyUSB2` (DHCP lease or static fallback `192.168.20.50`):

Bash

```
# 1. ICMP Echo (Ping)
ping -c 4 <board-ip>

# 2. HTTP Web Server & CGI Endpoint (TCP/80)
curl -i http://<board-ip>/
curl -i "http://<board-ip>/ctrl.cgi?cmd=led_toggle"

# 3. TFTP RAM-Disk Upload & Download (UDP/69, binary octet mode, <= 16 KiB)
tftp -m octet <board-ip> -c put Makefile
tftp -m octet <board-ip> -c get Makefile /tmp/Makefile.lim.rx
diff -s Makefile /tmp/Makefile.lim.rx
```





# PolarFire SoC Bare-Metal lwIP Network Application

This project is a bare-metal C application demonstrating Ethernet networking (lwIP 2.x) on the **PolarFire SoC Discovery Kit (MPFS095T)**. It runs entirely without an RTOS (`NO_SYS=1`) and features a DHCP client, an ICMP Ping responder, a static HTTP Web Server (TCP/80) with CGI handling, and a TFTP RAM-Disk Server (UDP/69).

## Hardware & Peripheral Routing

* **Target Board**: PolarFire SoC Discovery Kit

* **Ethernet MAC**: GEM0

* **Ethernet PHY**: VSC8221 (over Bank 5 SGMII / TBI interface)

* **MDIO Address**: `11`

* **HSS Boot Console**: `MMUART0` (typically `/dev/ttyUSB0` or `/dev/ttyUSB1`)

* **Application Stdio**: `MMUART4` (typically `/dev/ttyUSB2` or `/dev/ttyUSB3`)

## 1. Critical Configuration Settings

Getting the MAC, PHY, and lwIP stack to work cooperatively requires specific configurations across several files.

### A. Board Build Flags (`boards/mpfs-disco-kit/board.mk`)

Ensure the compiler builds for the Discovery Kit and utilizes a single-queue TX structure:

```makefile
BOARD_CFLAGS += -DBOARD_MPFS_DISCO_KIT \
                -DMPFS_DISCOVERY_KIT \
                -DTARGET_DISCOVERY_KIT \
                -DTARGET_G5_SOC \
                -DMSS_MAC_SIMPLE_TX_QUEUE \
                -DGEM0_MSS_MDIO
```

### B. Board Hardware Config (`boards/mpfs-disco-kit/board_config.h`)

The Discovery Kit uses a VSC8221 PHY connected via the TBI interface. The MAC interrupt must route to the PLIC handler.

```c
#define APP_MAC_IRQn            MAC0_INT_U54_INT
#define APP_MAC_POLL_ISR()      ((void)PLIC_mac0_int_IRQHandler())
#define APP_PHY_ADDR            11U   /* PHY_VSC8221_MDIO_ADDR */
#define APP_PCS_PHY_ADDR        SGMII_MDIO_ADDR
#define APP_PHY_INTERFACE       TBI   /* Bank 5 SGMII TBI Mode */
#define APP_PHY_TYPE            MSS_MAC_DEV_PHY_VSC8221
```

### C. Clock & Reset Initialization (`boards/mpfs-disco-kit/board_init.c`)

Before calling `MSS_MAC_init()`, the system registers must enable the MAC and UART clocks, and the PLIC must be initialized to accept interrupts.

```c
SYSREG->SOFT_RESET_CR   = 0U;
SYSREG->SUBBLK_CLOCK_CR = 0xFFFFFFFFUL;
(void)mss_config_clk_rst(MSS_PERIPH_MMUART4, MPFS_HAL_FIRST_HART, PERIPHERAL_ON);
(void)mss_config_clk_rst(MSS_PERIPH_MAC0,    MPFS_HAL_FIRST_HART, PERIPHERAL_ON);
PLIC_init();
__enable_irq();
```

### D. MAC Software Config (`mss_ethernet_mac_sw_cfg.h`)

**CRITICAL REQUIREMENT:** The build system uses different `platform_config` subdirectories depending on your memory target (`lim-debug`, `lim-release`, `ddr-release`, `envm-release`, etc.). You **must** ensure the correctly modified `mss_ethernet_mac_sw_cfg.h` is present in *every* configuration folder for your specific board. For example:
* `boards/mpfs-disco-kit/platform_config/lim-debug/drivers/mss/mss_ethernet_mac/`
* `boards/mpfs-disco-kit/platform_config/ddr-release/drivers/mss/mss_ethernet_mac/`

If an older copy is picked up by the compiler from one of these configuration folders, the `RX_RING_SIZE` may default to 4. This will silently drop burst packets from modern web browsers (which open multiple parallel connections) causing the web server to fail or hang.

To prevent dropped packets during parallel TCP requests, the RX ring must be sufficiently large in this file:

```c
#define MSS_MAC_RX_RING_SIZE (32U)  /* Do NOT use 4U */
#define MSS_MAC_TX_RING_SIZE (16U)
```

### E. lwIP Options (`middleware/net_stack/lwipopts.h`)

TFTP and DHCP require enough UDP PCBs and System Timeout slots to function simultaneously.

```c
#define NO_SYS                          1
#define SYS_LIGHTWEIGHT_PROT            0
#define MEMP_NUM_UDP_PCB                8
#define MEMP_NUM_SYS_TIMEOUT            16  /* Prevents TFTP allocation failures */
```

## 2. Building & Booting

1. **Build the DDR Target**:

   ```bash
   make BOARD=mpfs-disco-kit clean ddr
   ```

2. **Open the Application Console (MMUART4)**:
   In a separate terminal, open the application's UART port.

   ```bash
   python3 -m serial.tools.miniterm /dev/ttyUSB2 115200
   ```

3. **Boot via HSS**:
   Trigger the HSS payload transfer over MMUART0.

   ```bash
   make BOARD=mpfs-disco-kit ddr-boot
   ```

*(Wait for the board to boot. In your `miniterm` console, you will see the hardware init sequence, link auto-negotiation, and either a DHCP IP assignment or a fallback to the static `192.168.20.50`.)*

## 3. Testing the Network Services

Once the application console prints `[DHCP] Lease acquired!` or `applied static IPv4`, note the IP address and run the following tests from your host PC.

### A. ICMP Ping Test

Verify basic Layer 3 connectivity:

```bash
ping -c 4 <board-ip>
```

*Note: Press `s` in the UART console to watch the `RX` and `TX` packet counters increment.*

### B. HTTP Web Server (TCP Port 80)

Test the web server handshake and payload delivery using `curl`:

```bash
curl -v http://<board-ip>/
```

Test the simulated CGI endpoint (this will trigger a log message on the board's UART console):

```bash
curl -i "http://<board-ip>/ctrl.cgi?cmd=led_toggle"
```

*Note on Web Browsers: You can also open `http://<board-ip>/` in a standard desktop web browser (e.g., Firefox, Chrome). If it fails to load, ensure your browser is not forcing a redirect to HTTPS (port 443) and that your browser is not routing the local IP address through a proxy.*

### C. TFTP RAM-Disk Server (UDP Port 69)

The TFTP server strictly requires **Binary/Octet** mode. The default Linux `tftp` client uses `netascii`, which will result in a timeout if not overridden.

Test uploading a file (`put`):

```bash
tftp -m octet <board-ip> -c put Makefile
```

Test downloading the file back (`get`):

```bash
tftp -m octet <board-ip> -c get Makefile /tmp/Makefile.rx
diff -s Makefile /tmp/Makefile.rx
```

*Note: Press `t` in the UART console to view the currently stored file in the RAM-disk buffer and its size in bytes.*