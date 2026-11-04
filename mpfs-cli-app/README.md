# PolarFire SoC Bare-Metal CLI Application

A bare-metal target application and BIOS environment engineered for the Microchip PolarFire SoC Icicle Kit (`mpfs-icicle-kit`), designed to interface with Hart Software Services (HSS). It supports rapid volatile execution directly in DDR RAM via UART streaming, native multi-hart HSS payload packaging, and SPI flash persistent booting.

---

## Modifications to Original HSS

To enable rapid bare-metal iteration and multi-board support, the upstream Hart Software Services (HSS) bootloader was customized with several key enhancements:

1. **Serialboot Service (SFL Protocol):** Integrated a lightweight Serial Framing Loader (SFL) receiver on UART0. This allows developers to stream raw application binaries directly into DDR RAM or burn them to SPI Flash over a standard USB-Serial cable, bypassing the need for JTAG.
2. **Unified SPI Flash Driver:** Replaced board-hardcoded drivers (like `micron1gflash.c`) with a universal `spi_flash.c` driver. It reads the JEDEC ID at runtime to auto-detect flash chips—Micron (Icicle Kit), Winbond (Video Kit), and Microchip SST (Discovery Kit)—and configures 4-byte addressing automatically for chips >16MB.
3. **Board-Agnostic SPI HAL:** Abstracted low-level SPI access into `board_spi.c`. This allows the Icicle Kit to map flash through the FPGA fabric via `CoreSPI`, while allowing the Discovery and Video kits to use the hard `MSS_SPI` controllers without polluting the generic flash logic.
4. **Enhanced TinyCLI Commands:**
   * `serialboot`: Enters binary SFL receiver mode.
   * `FLASH_ID`: Displays detected SPI flash manufacturer, device IDs, and 4-byte addressing status.
   * `flashboot <offset>`: Fallback bootloader that reads legacy LiteX-style `.fbi` headers from SPI flash and launches them.


---

## Hardware & Interface Architecture

### PolarFire SoC Icicle Kit Serial Mapping

The Icicle Kit routes the PolarFire SoC MSS MMUART channels through an onboard USB-to-UART bridge (FT4232H), exposing four serial interfaces:

| Interface | Device Node (Linux) | Port (Windows) | Function / Subsystem | Baud Rate |
| :--- | :--- | :--- | :--- | :--- |
| **Port 0** | `/dev/ttyUSB0` | `COMx` | **HSS Monitor Console** (E51 monitor, TinyCLI, SFL receiver) | 115200 8N1 |
| **Port 1** | `/dev/ttyUSB1` | `COMy` | **Application Console** (U54 Hart 1 CLI, stdio) | 115200 8N1 |
| **Port 2** | `/dev/ttyUSB2` | `COMz` | Reserved / Auxiliary UART | 115200 8N1 |
| **Port 3** | `/dev/ttyUSB3` | `COMw` | Reserved / MSS System Controller JTAG / UART | 115200 8N1 |

---

## Prerequisites & Host Setup

### 1. Host Build Packages

* **Linux (Ubuntu/Debian):**
  ```bash
  sudo apt update
  sudo apt install -y build-essential git python3 python3-pip libelf-dev libyaml-dev libssl-dev
  pip3 install pyserial pyyaml
  ```
* **Windows:**
  * Microchip SoftConsole v2022.2 or later (includes Python, OpenOCD, and GCC).
  * Git Bash or MSYS2 environment.

### 2. RISC-V Cross Compiler

The build system automatically locates either `riscv-none-elf-` or `riscv64-unknown-elf-` from your system `PATH` or SoftConsole installation directory:
```bash
riscv-none-elf-gcc --version
```

---

## Build System & Memory Targets

The project uses a cross-platform Makefile supporting dynamic board definitions and multiple memory targets:

```bash
# Build for volatile DDR execution (recommended for rapid development)
make BOARD=mpfs-icicle-kit ddr

# Build for L2 Scratchpad / LIM (0x08000000)
make BOARD=mpfs-icicle-kit lim

# Clean the build workspace
make clean
```

### Build Artifacts

Outputs are generated in `build/<MEM_TARGET>/` (e.g., `build/ddr/`):
* **`mpfs-baremetal.elf`**: Linked ELF binary.
* **`mpfs-baremetal.bin`**: Raw binary extraction.
* **`mpfs-baremetal.fbi`**: Flash Binary Image with CRC32 header.
* **`mpfs-baremetal_payload.bin` / `payload.bin`**: Formatted multi-hart HSS payload container.
* **`payload.yaml`**: Auto-generated payload generator configuration derived via `readelf`.

---

## Understanding the Serialboot Flow & Host Tooling

### Why `mpfs-terminal.py` is Required

You cannot use a standard terminal emulator (like PuTTY, TeraTerm, or minicom) on UART0 to perform a serial boot.

The `serialboot` command invokes the **Serial Framing Loader (SFL)** protocol. This is a binary, packetized protocol that requires CRC validation, chunked data transfers, and handshake acknowledgments.

`mpfs-terminal.py` acts as a dual-purpose tool:
1. **Console Mode:** Passes your keystrokes to the board and prints ASCII output, completely replacing standard terminal emulators for UART0.
2. **Binary Upload Mode:** When you type `serialboot`, HSS transmits a magic string (`sL5DdSMmkekro`). The Python script detects this trigger, suspends console mode, negotiates the SFL handshake, transmits the requested file in binary frames, and returns to console mode once the transfer finishes.

### CRITICAL: Always Reset Before Downloading

Before initiating a new `serialboot` transfer, **you must reset the board** (using the physical RESET button or typing `reset` in HSS).

If a previously loaded application is still resident in memory or has altered hardware state (e.g., reconfigured UART clocks, locked memory regions via PMP, or left DMAs active), attempting to download a new image over it will cause framing errors or an immediate CPU exception upon execution.

---

## Boot & Test Workflows

```
  +-------------------------------------------------------------+
  |                        Host PC                              |
  |                                                             |
  |   Terminal 1 (Host Shell)           Terminal 2 (PuTTY/minicom)|
  |   make ddr-boot                     Connected to:           |
  |     └─> runs mpfs-terminal.py         /dev/ttyUSB1          |
  |           │                                │                |
  +-----------┼────────────────────────────────┼----------------+
              │                                │
       UART0 (/dev/ttyUSB0)             UART1 (/dev/ttyUSB1)
       (HSS Console / SFL)              (Application Console)
              │                                │
  +-----------┼────────────────────────────────┼----------------+
  |           ▼                                ▼                |
  |     +------------+                  +--------------+        |
  |     |  E51 Core  |                  |  U54 Hart 1  |        |
  |     |   (HSS)    |                  |  (Bare-Metal)|        |
  |     +------------+                  +--------------+        |
  |             PolarFire SoC Icicle Kit Board                  |
  +-------------------------------------------------------------+
```

---

### Method 1: Direct DDR RAM Execution (Fastest Development Loop)

This method streams the raw binary directly into DDR RAM over UART. No SPI flash or eNVM writes are performed.

#### Step 1: Open the Application Console (UART1)
Open a terminal emulator (e.g., PuTTY, minicom, or picocom) connected to **UART1** (`/dev/ttyUSB1`) at 115200 8N1. Keep this open in the background.

```bash
picocom -b 115200 /dev/ttyUSB1
```

> **IMPORTANT:** Do **NOT** open `/dev/ttyUSB0` in any serial program. `make ddr-boot` needs exclusive access to that port.

#### Step 2: Start Host Script on UART0
In your main terminal window, run:

```bash
make BOARD=mpfs-icicle-kit ddr-boot
```

#### Step 3: Trigger Transfer from HSS Prompt
In the window running `ddr-boot`, press the physical **RESET** push-button on the Icicle Kit (or type `reset` and hit Enter). When the HSS command prompt appears, enter `serialboot`.

#### Expected Terminal 1 Output (`/dev/ttyUSB0` via `ddr-boot`):

```text
==================================================
 Streaming & Executing Raw Binary to RAM
 File        : build/ddr/mpfs-baremetal.bin
 Load Addr   : 0x80000000
 Serial Port : /dev/ttyUSB0
==================================================
[MPFS-TERM] Opened /dev/ttyUSB0 at 115200 baud. (Press Ctrl+C to exit)
[MPFS-TERM] Type 'serialboot' on prompt to trigger download.

Hart Software Services (HSS) Release v2024.06
mpfs-icicle-kit selected
Initializing DDR... Passed
>> serialboot

Ready for SFL transfer. Connect host script...
[MPFS-TERM] Magic trigger matched! Replying with handshake ACK...
[MPFS-TERM] Uploading build/ddr/mpfs-baremetal.bin (79008 bytes) -> 0x80000000...
|========================================| 100.0% (79008/79008 B)
[MPFS-TERM] Transfer Complete (10.2 KB/s).
[MPFS-TERM] Sending JUMP Command to boot application...
[MPFS-TERM] Terminal active. Continuing console output...

[SERIALBOOT] Synchronizing caches and dispatching all U54 harts to 0x0000000080000000...
[SERIALBOOT] All U54 harts launched. Application active on UART1 (/dev/ttyUSB1).
[SERIALBOOT] Returning to HSS TinyCLI.
>>
```

#### Expected Terminal 2 Output (`/dev/ttyUSB1` Application Console):

Immediately upon dispatch, Terminal 2 will show the bare-metal prompt:

```text
==================================================
         CLI Example: PolarFire SoC               
         Build: 2026-11-04 12:25:00
==================================================
Type 'help' for a list of available commands.

mpfs-icicle-kit> 
```

---

### Method 2: Flash & Boot Native HSS Payload (Production Workflow)

This method programs the multi-hart payload container (`payload.bin`) into onboard SPI flash at offset `0x20000`. On subsequent power cycles or resets, HSS verifies and loads the application into DDR automatically.

#### Step 1: Launch Flash Tooling
Ensure `/dev/ttyUSB0` is closed, then run:

```bash
make BOARD=mpfs-icicle-kit ddr-flash
```

#### Step 2: Trigger Programming
Press the board's **RESET** button, wait for the HSS prompt, and type `serialboot`.

#### Expected Terminal 1 Output (`/dev/ttyUSB0` via `ddr-flash`):

```text
==================================================
 Flashing HSS Payload to SPI Flash
 File        : build/ddr/mpfs-baremetal_payload.bin
 Port        : /dev/ttyUSB0
 Flash Addr  : 0x00020000
==================================================
[MPFS-TERM] Opened /dev/ttyUSB0 at 115200 baud. (Press Ctrl+C to exit)
[MPFS-TERM] Type 'serialboot' on prompt to trigger download.

Hart Software Services (HSS) Release v2024.06
mpfs-icicle-kit selected
>> serialboot

Ready for SFL transfer. Connect host script...
[MPFS-TERM] Magic trigger matched! Replying with handshake ACK...
[MPFS-TERM] Requesting SPI Flash erase/prep (32576 bytes) at Offset 0x00020000...
[MPFS-TERM] Erase complete. Streaming image chunks...
|========================================| 100.0% (32576/32576 B)
[MPFS-TERM] Transfer Complete (9.8 KB/s).
[MPFS-TERM] Sending DONE Command...

[FLASHWRITE] SPI Flash Direct Upload Complete!
 Target Flash Addr  : 0x00020000
 Total Written Size : 32576 bytes

[INFO] Flash programming finished. Returning to prompt.
>>
```

#### Step 3: Boot Execution
Once flashed, boot the payload using either method:
* **Automatic:** Power-cycle or press RESET. HSS trains DDR, parses the payload header at `0x20000`, unpacks chunks to `0x80000000`, and releases all U54 harts.
* **Manual:** At the HSS prompt, type `boot`.

#### Expected HSS Boot Log (`/dev/ttyUSB0`):

```text
>> boot
[HSS] Attempting to boot from SPI Flash (0x00020000)...
[HSS] Found valid payload container: "PolarFire-SoC-HSS::mpfs-baremetal"
[HSS] Unpacking chunk 0 (79008 bytes) -> DDR 0x80000000
[HSS] Chunk verification: CRC32 OK
[HSS] Releasing Hart 1 (U54_1) to 0x80000000...
[HSS] Releasing Hart 2 (U54_2) to 0x80000000...
[HSS] Releasing Hart 3 (U54_3) to 0x80000000...
[HSS] Releasing Hart 4 (U54_4) to 0x80000000...
[HSS] Boot complete.
```

The application console on **UART1** will immediately display the `mpfs-icicle-kit>` CLI prompt.

---

### Method 3: Flash & Boot Custom FBI Image (CoreSPI Fallback)

If using the custom LiteX-style `.fbi` binary image format with an 8-byte `[Length, CRC32]` header:

1. Close `/dev/ttyUSB0` in external terminal software and start the upload helper:
   ```bash
   python3 tools/mpfs-terminal.py /dev/ttyUSB0 --kernel build/ddr/mpfs-baremetal.fbi --flash --kernel-adr 0x00020000
   ```
2. Reset the board and enter `serialboot`.
3. Once completed, execute the fallback booter directly from HSS TinyCLI:
   ```text
   >> flashboot 0x20000
   ```

#### Expected HSS Console Log (`flashboot`):

```text
>> flashboot 0x20000

[FLASHBOOT] Reading FBI header from SPI Flash offset 0x00020000...
[FLASHBOOT] FBI [Length, CRC32] detected:
  Payload Size : 79008 bytes (0x000134A0)
  Payload CRC  : 0x5D8A4B92
  Target DDR   : 0x0000000080000000
[FLASHBOOT] Copying 79008 bytes from SPI 0x00020008 to DDR 0x0000000080000000...
[FLASHBOOT] DDR First 4 Words: 0x00000297 0x01828293 0x30529073 0x00000517
[FLASHBOOT] Launching application on U54 harts...

[SERIALBOOT] Synchronizing caches and dispatching all U54 harts to 0x0000000080000000...
[SERIALBOOT] All U54 harts launched. Application active on UART1 (/dev/ttyUSB1).
```

---

## Application CLI Reference & Examples

Once booted, interact with the application on **UART1** (`/dev/ttyUSB1`):

```text
==================================================
         CLI Example: PolarFire SoC               
         Build: 2026-11-04 12:25:00
==================================================
Type 'help' for a list of available commands.

mpfs-icicle-kit> 
```

### Command Reference

| Command | Usage | Description |
| :--- | :--- | :--- |
| **`help`** | `help` | Lists all supported CLI commands and brief syntax. |
| **`ident`** | `ident` | Displays target SoC info, active application core, and build timestamp. |
| **`mr`** | `mr <hex_addr> [count]` | Memory Read: Dumps 32-bit dwords from memory (count in decimal). |
| **`mw`** | `mw <hex_addr> <hex_val>` | Memory Write: Writes a 32-bit hex value to the target address. |
| **`go`** | `go <hex_addr>` | Synchronizes serial FIFO and jumps execution to memory location. |
| **`serialboot`** | `serialboot` | Puts the target into slave SFL receiver mode for chained code loading. |

### Example CLI Sessions

#### 1. Display System Identity (`ident`)
```text
mpfs-icicle-kit> ident

Bootloader Target Test Application (Microchip PolarFire SoC MPFS)
Target Core : U54 Application Core (Hart 1)
Build Time  : 2026-11-04 12:25:00

mpfs-icicle-kit> 
```

#### 2. Reading Memory Words (`mr`)
Dumping 4 words starting from DDR entry point (`0x80000000`):
```text
mpfs-icicle-kit> mr 0x80000000 4

Reading 0x4 dwords from 0x0000000080000000:

0x0000000080000000: 00000297 01828293 30529073 00000517 

mpfs-icicle-kit> 
```

#### 3. Writing and Verifying Memory (`mw` / `mr`)
```text
mpfs-icicle-kit> mw 0x80100000 0xCAFEBABE
Wrote 0xcafebabe to 0x0000000080100000

mpfs-icicle-kit> mr 0x80100000 1

Reading 0x1 dwords from 0x0000000080100000:

0x0000000080100000: cafebabe 

mpfs-icicle-kit> 
```

---

## Diagnostic Utility Commands in HSS (UART0)

When sitting at the HSS prompt (`>>`), use these custom diagnostic commands to inspect hardware status:

```text
>> FLASH_ID

[SPI_FLASH] JEDEC ID & Device Info:
 Identified   : Micron MT25Q
 Manufacturer : 0x20 (0x20=Micron, 0xEF=Winbond, 0xBF=SST)
 Device Type  : 0xBA
 Capacity ID  : 0x21
 4-Byte Addr  : Enabled
```

---

## Hardware JTAG & eNVM Operations

```bash
# Start OpenOCD JTAG server using SoftConsole configuration
make openocd

# Launch a GDB debugging session attached to Hart 1
make debug

# Program Boot Mode 1 eNVM client using mpfsBootmodeProgrammer
make program-envm-lim BOARD=mpfs-icicle-kit

# Reprogram FPGA fabric bitstream via FlashPro Express job file
make program-job BOARD=mpfs-icicle-kit
```