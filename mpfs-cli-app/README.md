# PolarFire SoC Bare-Metal CLI Application

A bare-metal target application and BIOS environment engineered for the Microchip PolarFire SoC Icicle Kit (`mpfs-icicle-kit`), designed to interface with Hart Software Services (HSS). It supports rapid volatile execution directly in DDR RAM via UART streaming, native multi-hart HSS payload packaging, and SPI flash persistent booting.

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

## Boot & Test Workflows

### Method 1: Direct DDR RAM Execution (Fastest Development Loop)
This method streams the raw binary directly into DDR RAM over UART using the Serial Framing Loader (SFL) protocol. No SPI flash or eNVM writes are performed.

1. **Open Serial Terminals:**
   * Terminal 1: Connect to HSS Console on `/dev/ttyUSB0` at 115200 baud.
   * Terminal 2: Connect to Application Console on `/dev/ttyUSB1` at 115200 baud.

2. **Trigger HSS Serialboot:**
   On Terminal 1 (`/dev/ttyUSB0`), enter:
   ```text
   >> serialboot
   ```
   HSS displays: `Ready for SFL transfer. Connect host script...`

3. **Stream and Execute from Host:**
   In your host project directory, execute:
   ```bash
   make BOARD=mpfs-icicle-kit ddr-boot
   ```
   `mpfs-terminal.py` transfers the binary chunks to DDR (`0x80000000`) and dispatches all four U54 application harts simultaneously.

4. **Interact with the Application:**
   Switch to Terminal 2 (`/dev/ttyUSB1`) to access the interactive CLI prompt.

---

### Method 2: Flash & Boot Native HSS Payload (Production Workflow)
This method programs the Microchip formatted payload container (`payload.bin`) into onboard SPI flash at offset `0x20000`. On subsequent power cycles or resets, HSS's built-in System Controller SPI driver automatically verifies and loads the application into DDR.

1. **Put HSS into Serialboot Receiver Mode:**
   On Terminal 1 (`/dev/ttyUSB0`), run:
   ```text
   >> serialboot
   ```

2. **Flash the Generated Payload from Host:**
   Run the dedicated flash target:
   ```bash
   make BOARD=mpfs-icicle-kit flash-payload
   ```
   *This uses `mpfs-terminal.py` to transmit `build/ddr/mpfs-baremetal_payload.bin` to SPI Flash offset `0x00020000`*.

3. **Boot Execution:**
   * **Automatic:** Power-cycle or reset the Icicle Kit. After DDR training, HSS reads the image from SPI Flash +`0x20000`, unpacks it, releases the U54 cores, and starts your application.
   * **Manual:** At the HSS prompt, type `boot`.

---

### Method 3: Flash & Boot Custom FBI Image (CoreSPI Fallback)
If you are using the custom LiteX-compatible `.fbi` image format with an 8-byte header `[Length, CRC32]`:

1. Enter `serialboot` on the HSS console (`/dev/ttyUSB0`).
2. Upload the `.fbi` binary to flash:
   ```bash
   python3 tools/mpfs-terminal.py /dev/ttyUSB0 --kernel build/ddr/mpfs-baremetal.fbi --flash --kernel-adr 0x00020000
   ```
3. Boot the image manually from the HSS CLI:
   ```text
   >> flashboot 0x20000
   ```

---

## Application CLI Reference

Once booted, the application console on UART1 (`/dev/ttyUSB1`) presents the CLI:

```text
==================================================
         CLI Example: PolarFire SoC               
         Build: 2026-11-03 18:25:00
==================================================
Type 'help' for a list of available commands.

mpfs-icicle-kit> 
```

### Available Commands

| Command | Usage | Description |
| :--- | :--- | :--- |
| **`help`** | `help` | Lists all supported CLI commands and syntax. |
| **`ident`** | `ident` | Displays system target info, active core, and compile timestamp. |
| **`mr`** | `mr <hex_addr> [count]` | Memory Read: Dumps 32-bit dwords from memory (e.g., `mr 0x80000000 16`). |
| **`mw`** | `mw <hex_addr> <hex_val>` | Memory Write: Writes a 32-bit value to target address (e.g., `mw 0x80000000 0x12345678`). |
| **`go`** | `go <hex_addr>` | Flushes serial buffers and jumps execution to a memory location. |
| **`serialboot`** | `serialboot` | Puts the application into slave SFL receiver mode for chained code loading. |

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