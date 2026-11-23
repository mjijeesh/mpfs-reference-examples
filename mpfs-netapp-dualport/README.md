
# Guide to Bare-Metal Ethernet on PolarFire SoC  ICICLE Kit - DualMAC

mpfs-netapp-dualport  is a bare-metal RISC-V Ethernet reference application for the Microchip PolarFire SoC family. 
Built on the mpfs_hal hardware abstraction layer and a cooperative polling (NO_SYS = 1) lwIP TCP/IP stack, 
This reference design is tested o nthe  PolarFire SoC Icicle Kit. This can be ported on to Poalrfire Video Kit or any Custom Board with  Dual Ethernet Interface.
Program execution can be either from on-chip L2-LIM SRAM or external DDR4 memory.

Out of the box, the application provides DHCPv4 discovery with automatic static IPv4 fallback, ICMP echo (ping), an embedded HTTP web server (TCP port 80) with CGI board control, and an in-memory TFTP server (UDP port 69).

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
   ```sh
   cd ~/git/mpfs-reference-examples/mpfs-netapp-dualport
   ```

2. **Build the application** for the external DDR memory target:
   ```sh
   make BOARD=mpfs-icicle-kit clean ddr-release
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


   *Expected Output: The compiler will run and eventually print a table showing memory usage, creating a file named `mpfs-net-app-dualport_payload.bin`.*

---

## 7. Loading the Application onto the Board

You can load your compiled binary onto the board using the pre-installed HSS bootloader in two ways:

### Option A: Loading via HSS SerialBoot (Over USB) - 
This sends the file over the USB cable. 
1. Ensure the Icicle Kit's Micro-USB is connected to your PC.
2. In a Linux terminal, run the provided Python script to trigger the upload to the HSS serial port (usually `/dev/ttyUSB0`):
   ```
   python3 tools/mpfs-terminal.py /dev/ttyUSB0 --kernel build/mpfs-icicle-kit/ddr-release/mpfs-net-app-dualport_payload.bin 
   ```
   *The script will say "Uploading...", write the file into the board's RAM, and command the board to boot.*

   
Reset the board and press any key inside mpfs-terminal.py to enter the `HSS >>` prompt.

Execute serialboot. HSS emits the SFL magic trigger, mpfs-terminal.py automatically replies with the handshake ACK and transfers the payload with CRC16 verification, and HSS parses the payload chunks and boots `U54_1`.


* Expected Terminal Output:

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



### Option B: Loading via HSS Netboot (Over Network)
This is much faster but requires a TFTP server on your Linux PC.
1. Copy `mpfs-net-app-dualport_payload.bin` to your PC's TFTP folder and rename it to `boot.bin`.
2. Open a standard serial terminal to the HSS console:
   ```bash
     minicom -b 115200  -D /dev/ttyUSB0      
   ```

   Or if using the `mpfs-term.py`  terminal application

   ```
   python3 tools/mpfs-terminal.py /dev/ttyUSB0  

   ```
3. Press a key on your keyboard to stop the default boot process and enter the `TinyCLI` prompt.

4. Tell the board to download the file from your PC's IP address (e.g., `192.168.20.94`):
   ```text
   >> netboot 192.168.20.94 boot.bin
   ```

---

## 8. Expected Outputs & Testing

To see the application output, open a second terminal window to the **Application Console** (`MMUART1`, usually `/dev/ttyUSB1` at 115200 baud):

```
python3 tools/mpfs-terminal.py  /dev/ttyUSB1 115200
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

**Tip:** At any time, you can press **`s`** in your `miniterm` application console window to print the current packet counters (TX/RX) for both ports!




## 9. Debugging on DDR (ddr-debug) — HSS-Assisted JTAG

Use ddr-debug when debugging with the full 128 MiB DDR4 memory map, large MAC rings (32 RX / 1001 TX descriptors), and the 1 MiB TFTP RAM-disk.  
**Critical Prerequisite:** DDR4 memory (0x80000000) is uninitialized immediately after power-on. Pre-flashed **Hart Software Services (HSS)** must run on E51 first to initialize PLLs and train the DDR4 controller. Accordingly, debug-ddr uses a **soft halt** (monitor halt, never reset halt) and only redirects **threads 2–5 (U54\_1–U54\_4)** to \_start, leaving **thread 1 (E51)** safely parked inside HSS.



 1. **Terminal 1 — Start the OpenOCD JTAG Server:** make BOARD=mpfs-icicle-kit openocd  
 2. **Terminal 2 — Open the Application Serial Console (MMUART4):** minicom \-b 115200 \-D /dev/ttyUSB2  
 3. **Terminal 3 — Build, Load into DDR & Run GDB:** make BOARD=mpfs-icicle-kit debug-lim

 
**Terminal 1: OpenOCD**

```sh
make BOARD=mpfs-icicle-kit openocd
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
make BOARD=mpfs-disco-kit debug-ddr
```

```
GNU gdb (xPack GNU RISC-V Embedded GCC x86_64) 16.3
Reading symbols from build/mpfs-icicle-kit/ddr-debug/mpfs-net-app-dualport.elf...
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


### **C. Application Execution Output (App UART)**

**Command:** Open a serial viewer on the App UART (e.g., minicom \-D /dev/ttyUSB2 \-b 115200\) before you trigger the boot.  
**Purpose:** Monitors the U54 application logs, DHCP state machine, and provides an interactive CLI.  
**Expected Terminal Output:**

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

*Note: If you press 't' in the terminal, you will see the TFTP capacity report (e.g., Cap: 1024 KiB).*


