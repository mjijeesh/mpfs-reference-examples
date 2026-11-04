# **PolarFire SoC Bare-Metal CLI Application**

A multi-board bare-metal application and BIOS framework engineered for Microchip PolarFire SoC platforms—supporting both the **PolarFire SoC Discovery Kit (mpfs-disco-kit)** and the **PolarFire SoC Icicle Kit (mpfs-icicle-kit)**.  
The application interfaces directly with Hart Software Services (HSS). It supports rapid volatile execution directly in DDR RAM via UART streaming, native multi-hart HSS payload packaging, and persistent SPI flash booting.

## **⚠️ Mandatory Requirement: mpfs-terminal.py for Serialboot**

**CRITICAL:** Standard terminal emulators (such as PuTTY, Minicom, Picocom, Screen, or TeraTerm) **CANNOT** be used to run serialboot.  
If you type serialboot inside a standard terminal emulator, the board will print:

Plaintext  
Ready for SFL transfer. Connect host script...  
No response or cancelled. Aborting.

This occurs because HSS implements the packetized LiteX **Serial Framing Loader (SFL)** protocol. Upon entering serialboot, HSS transmits a magic trigger sequence (sL5DdSMmkekro) and requires a binary ACK response within a strict timeout window. Standard terminal emulators cannot interpret or respond to this handshake.  
**You MUST connect to the HSS console using mpfs-terminal.py** (either invoked automatically via make ddr-boot / make ddr-flash, or executed manually via Python).

### **How mpfs-terminal.py Operates**

> 1. **Transparent Console Pass-Through:** In normal operation, mpfs-terminal.py operates as an interactive terminal emulator, passing keystrokes and printing HSS log messages.  
> 2. **Handshake Interception:** When serialboot is entered at the HSS prompt, the Python script intercepts the sL5DdSMmkekro magic string, suspends terminal mode, and transmits the required SFL handshake ACK.  
> 3. **Framed Packet Streaming:** It breaks the binary payload into CRC16-protected SFL packets, handles hardware flow control acknowledgments, and streams the application into target RAM or SPI flash.  
> 4. **Target Execution:** Upon completion, it transmits the SFL JUMP command to dispatch the U54 cores and seamlessly switches back to transparent terminal mode.

## **Modifications to Original HSS**

Upstream Hart Software Services (HSS) has been customized to support both the Icicle Kit and Discovery Kit seamlessly:

> 1. **Serialboot Service (SFL Protocol):**  
   * Dynamically binds to the active board console UART (MMUART0 on the Icicle Kit, MMUART1 on the Discovery Kit).  
   * Streams raw application binaries directly into DDR RAM or burns them into SPI flash via standard serial transfers.  
   * Parks Hart 0 (E51) in a low-power wfi loop after dispatching the U54 application harts, eliminating background polling messages and terminal contention on shared serial interfaces.  
> 2. **Universal SPI Flash Driver (spi\_flash.c):**  
   * Replaced single-chip hardcoded drivers with a dynamic runtime probe engine.  
   * Automatically identifies JEDEC ID signatures: Micron MT25Q (0x20), Winbond W25Q (0xEF), Microchip SST26 (0xBF), and Microchip SST25WF 1.8V Low-Voltage (0x62).  
   * Auto-negotiates 4-byte addressing for devices $\geq$ 256 Mbit.  
   * Applies device-specific write-protection unlock sequences during initialization:  
     * **SST26:** Global Block-Protection Unlock (ULBPR 0x98).  
     * **SST25VF / SST25WF:** Enable Write Status Register (EWSR 0x50) followed by Write Status Register (WRSR 0x01 0x00).  
     * **Micron / Winbond:** Standard Write Enable (0x06) \+ Write Status (0x01 0x00).  
> 3. **Modular Board SPI HAL (board\_spi.c):**  
   * **Icicle Kit:** Maps SPI transfers through FPGA fabric via CoreSPI.  
   * **Discovery Kit:** Implements polled master transfers directly via hard silicon **MSS SPI0** (0x20108000) routed to the mikroBUS interface, bypassing PLIC interrupt constraints.  
> 4. **Enhanced Diagnostic TinyCLI Commands:**  
   * serialboot: Enters binary SFL receiver mode.  
   * FLASH\_ID: Displays JEDEC identification, detected device family, and 4-byte addressing status.  
   * flashboot \<offset\>: Fallback bootloader reading binary payloads or legacy .fbi headers from flash.

## **Hardware & Interface Architecture**

### **Serial Port Mapping Comparison**

Both kits expose multiple serial channels via their onboard USB bridges:

| Board | Physical Interface | Linux Device | Assigned Subsystem / Function | Default Baud |
| :---- | :---- | :---- | :---- | :---- |
| **Icicle Kit** | Port 0 | /dev/ttyUSB0 | **HSS Monitor Console** (Requires mpfs-terminal.py) | 115200 8N1 |
|  | Port 1 | /dev/ttyUSB1 | **Application Console** (MMUART1, U54 Hart 1 CLI) | 115200 8N1 |
| **Discovery Kit** | Port 0 | /dev/ttyUSB0 | FTDI Channel A (JTAG Debugger) | N/A |
|  | Port 1 | /dev/ttyUSB1 | **HSS Monitor Console** (Requires mpfs-terminal.py) | 115200 8N1 |
|  | Port 2 | /dev/ttyUSB2 | **Application Console** (MMUART4, U54 Hart 1 CLI) | 115200 8N1 |

## **Modular Board Configuration (board.mk)**

Hardware parameters, memory mapping, driver definitions, and default serial interfaces are isolated into modular board.mk fragments inside each board directory:

> * **boards/mpfs-disco-kit/board.mk:**  
  * Target: MPFS095T in FCSG325 package.  
  * Application STDIO: MSS\_PERIPH\_MMUART4 (\&g\_mss\_uart4\_lo).  
  * Default Flashing/HSS Port: /dev/ttyUSB1.  
  * Driver: Hard silicon MSS SPI0 (-DUSE\_MSS\_SPI0).  
> * **boards/mpfs-icicle-kit/board.mk:**  
  * Target: MPFS250T in FCVG484 package.  
  * Application STDIO: MSS\_PERIPH\_MMUART1 (\&g\_mss\_uart1\_lo).  
  * Default Flashing/HSS Port: /dev/ttyUSB0.  
  * Driver: Fabric CoreSPI (-DUSE\_CORE\_SPI).

## **Prerequisites & Host Setup**

### **1\. Host Dependencies**

> * **Linux (Ubuntu/Debian):**  
>   Bash  
>   sudo apt update  
>   sudo apt install \-y build-essential git python3 python3-pip libelf-dev libyaml-dev libssl-dev picocom  
>   pip3 install pyserial pyyaml

> * **Windows:**  
  * Microchip SoftConsole v2022.2 or later.  
  * Git Bash or MSYS2.

### **2\. RISC-V Cross Compiler**

The build system automatically detects riscv-none-elf-gcc or riscv64-unknown-elf-gcc from your system PATH or SoftConsole installation:

Bash  
riscv-none-elf-gcc \--version

## **Build System & Memory Targets**

Build for either board by specifying BOARD=\<target\>:

Bash  
\# Compile application for Discovery Kit (DDR execution)  
make BOARD=mpfs-disco-kit ddr

\# Compile application for Icicle Kit (DDR execution)  
make BOARD=mpfs-icicle-kit ddr

\# Compile for Scratchpad LIM (0x08000000)  
make BOARD=mpfs-disco-kit lim

\# Clean workspace  
make clean

### **Build Artifacts**

Artifacts are generated in build/\<MEM\_TARGET\>/:

> * **mpfs-baremetal.elf**: ELF executable with debugging symbols.  
> * **mpfs-baremetal.bin**: Raw flat binary for direct DDR RAM streaming.  
> * **mpfs-baremetal\_payload.bin / payload.bin**: Multi-hart container formatted for HSS payload booting.  
> * **payload.yaml**: Auto-generated hart layout configuration derived via readelf.

## **Boot & Test Workflows**

  \+-------------------------------------------------------------+  
  \|                        Host PC                              \|  
  \|                                                             \|  
  \|   Terminal 1 (Flashing & HSS)       Terminal 2 (App Console)\|  
  \|   make BOARD=\<target\> ddr-boot      picocom /dev/ttyUSBx    \|  
  \|     └─\> runs mpfs-terminal.py       (Standard terminal)     \|  
  \+-----------┼────────────────────────────────┼----------------+  
              │                                │  
       HSS Console                      App Console  
       Icicle:    /dev/ttyUSB0          Icicle:    /dev/ttyUSB1  
       Discovery: /dev/ttyUSB1          Discovery: /dev/ttyUSB2  
              │                                │  
  \+-----------┼────────────────────────────────┼----------------+  
  \|           ▼                                ▼                \|  
  \|     \+------------+                  \+--------------+        \|  
  \|     \|  E51 Core  \|                  \|  U54 Hart 1  \|        \|  
  \|     \|   (HSS)    \|                  \|  (Bare-Metal)\|        \|  
  \|     \+------------+                  \+--------------+        \|  
  \|              PolarFire SoC Target Hardware                  \|  
  \+-------------------------------------------------------------+

### **Method 1: Direct DDR RAM Execution (Fastest Development Loop)**

Streams the binary directly into DDR RAM over UART without erasing or rewriting flash memory.

#### **Step 1: Open the Application Console (Terminal 2\)**

Open a standard terminal emulator for application STDIO and leave it running in the background:

> * **For Discovery Kit (MMUART4):**  
>   Bash  
>   picocom \-b 115200 /dev/ttyUSB2

> * **For Icicle Kit (MMUART1):**  
>   Bash  
>   picocom \-b 115200 /dev/ttyUSB1

#### **Step 2: Start mpfs-terminal.py on the HSS Port (Terminal 1\)**

Run the automated ddr-boot target, which starts mpfs-terminal.py on the board's designated HSS console port:

> * **Discovery Kit (/dev/ttyUSB1):**  
>   Bash  
>   make BOARD=mpfs-disco-kit ddr-boot

> * **Icicle Kit (/dev/ttyUSB0):**  
>   Bash  
>   make BOARD=mpfs-icicle-kit ddr-boot

*(Alternatively, run the script directly:)*

Bash  
\# Discovery Kit  
python3 tools/mpfs-terminal.py /dev/ttyUSB1 \--kernel build/ddr/mpfs-baremetal.bin \--kernel-adr 0x80000000

\# Icicle Kit  
python3 tools/mpfs-terminal.py /dev/ttyUSB0 \--kernel build/ddr/mpfs-baremetal.bin \--kernel-adr 0x80000000

#### **Step 3: Trigger Transfer**

> 1. Press the board's physical **RESET** button.  
> 2. Press any key to stop autoboot and enter the TinyCLI prompt (\>\> ).  
> 3. Type:  
>    Plaintext  
>    \>\> serialboot

#### **Expected HSS Console Output (mpfs-terminal.py in Terminal 1):**

Plaintext  
\==================================================  
 Streaming & Executing Raw Binary to RAM  
 Board       : mpfs-disco-kit  
 Serial Port : /dev/ttyUSB1  
 Load Addr   : 0x80000000  
\==================================================  
\[MPFS-TERM\] Opened /dev/ttyUSB1 at 115200 baud. (Press Ctrl+C to exit)  
\[MPFS-TERM\] Type 'serialboot' on prompt to trigger download.

\>\> serialboot

Ready for SFL transfer. Connect host script...  
sL5DdSMmkekro  
\[MPFS-TERM\] Magic trigger matched\! Replying with handshake ACK...  
\[MPFS-TERM\] Uploading build/ddr/mpfs-baremetal.bin (32512 bytes) \-\> 0x80000000...  
\|========================================\| 100.0% (32512/32512 B)  
\[MPFS-TERM\] Transfer Complete (7.8 KB/s).  
\[MPFS-TERM\] Sending JUMP Command to boot application...  
\[MPFS-TERM\] Terminal active. Continuing console output...

\[SERIALBOOT\] Synchronizing caches and dispatching all U54 harts to 0x0000000080000000...  
\[SERIALBOOT\] All U54 harts launched. Parking HSS Monitor Hart.

#### **Expected Application Console Output (Terminal 2):**

Plaintext  
\==================================================  
         CLI Example: PolarFire SoC                 
         Build: 2026-11-04 16:15:00  
\==================================================  
Type 'help' for a list of available commands.

mpfs-disco-kit\> 

### **Method 2: Flash & Boot Native HSS Payload (Persistent SPI Flash)**

Writes the application payload directly to onboard SPI flash at offset 0x20000 via mpfs-terminal.py. HSS automatically loads and verifies this container on power-up.

> 1. Start the flash programming helper:  
>    Bash  
>    \# Discovery Kit  
>    make BOARD=mpfs-disco-kit ddr-flash

>    \# Icicle Kit  
>    make BOARD=mpfs-icicle-kit ddr-flash

> 2. Reset the board, enter TinyCLI, and run serialboot.  
> 3. mpfs-terminal.py automatically handles the handshake, unlocks the flash array, erases the required sectors, and streams the image:  
>    Plaintext  
>    \[MPFS-TERM\] Requesting SPI Flash erase/prep (32576 bytes) at Offset 0x00020000...  
>    \[MPFS-TERM\] Erase complete. Streaming image chunks...  
>    \|========================================\| 100.0% (32576/32576 B)  
>    \[MPFS-TERM\] Transfer Complete.  
>    \[FLASHWRITE\] SPI Flash Direct Upload Complete\!

> 4. Reset the board or run boot in HSS to start the application.

## **Diagnostic Utility Commands in HSS**

Query connected flash memories from the HSS CLI prompt (\>\> ) using mpfs-terminal.py:

### **Discovery Kit (SST25WF on MSS SPI0):**

Plaintext  
\>\> FLASH\_ID

\[SPI\_FLASH\] JEDEC ID & Device Info:  
 Identified   : Microchip SST25WF  
 Manufacturer : 0x62 (0x20=Micron, 0xEF=Winbond, 0xBF=SST)  
 Device Type  : 0x06  
 Capacity ID  : 0x13  
 4-Byte Addr  : Disabled

### **Icicle Kit (Micron MT25Q on CoreSPI):**

Plaintext  
\>\> FLASH\_ID

\[SPI\_FLASH\] JEDEC ID & Device Info:  
 Identified   : Micron MT25Q  
 Manufacturer : 0x20 (0x20=Micron, 0xEF=Winbond, 0xBF=SST)  
 Device Type  : 0xBA  
 Capacity ID  : 0x21  
 4-Byte Addr  : Enabled

## **Application CLI Command Reference**

Once the application prompt appears (mpfs-disco-kit\> or mpfs-icicle-kit\>), the following commands are available:

| Command | Syntax | Description |
| :---- | :---- | :---- |
| **help** | help | Lists available commands. |
| **ident** | ident | Displays active hart, system architecture, and build time. |
| **mr** | mr \<hex\_addr\> \[count\] | Memory Read: Dumps 32-bit words from memory. |
| **mw** | mw \<hex\_addr\> \<hex\_val\> | Memory Write: Writes a 32-bit hex value to target address. |
| **go** | go \<hex\_addr\> | Flushes serial FIFO and branches execution to target address. |
| **serialboot** | serialboot | Puts Hart 1 into slave SFL receiver mode for secondary chained loading. |

## **Hardware JTAG & Direct Programming**

Bash  
\# Launch OpenOCD daemon using SoftConsole target scripts  
make openocd

\# Attach GDB to Hart 1  
make debug

\# Program FPGA bitstream (.job) using FlashPro Express  
make program-job BOARD=mpfs-disco-kit

\# Program Boot Mode 1 eNVM client via mpfsBootmodeProgrammer  
make program-envm-lim BOARD=mpfs-disco-kit  
