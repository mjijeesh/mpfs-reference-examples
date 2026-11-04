# Multi-Hart Bare-Metal Execution on PolarFire SoC

This example demonstrates Asymmetric Multiprocessing (AMP) on the Microchip PolarFire SoC by executing four distinct, concurrent workloads on the four symmetrical 64-bit U54 application cores (Harts 1-4).

## Architecture Overview

The PolarFire SoC contains a CPU cluster with five cores:
* **Hart 0 (E51):** Reserved for System Management and Hart Software Services (HSS).
* **Harts 1–4 (U54):** 64-bit application cores sharing a coherent L2 memory subsystem.

In this bare-metal application, Hart 1 acts as the master interface, while Harts 2, 3, and 4 run continuous background compute and monitoring tasks.

### Core Assignments

| Core | Source File | Workload Description | Task Details |
| :--- | :--- | :--- | :--- |
| **Hart 1** | `u54_1.c` | **Interactive CLI Master** | Initializes UART, bootstraps the system, runs the `readline` loop, and parses user commands. |
| **Hart 2** | `u54_2.c` | **CRC32 Compute Engine** | Simulates a heavy compute pipeline by continuously calculating CRC32 checksums over a mutating data buffer. |
| **Hart 3** | `u54_3.c` | **DDR Bus Stress Test** | Validates memory integrity by continuously writing and verifying bit-patterns across a dedicated 64 KiB DDR buffer. |
| **Hart 4** | `u54_4.c` | **Watchdog & Hardware Timer** | Tracks the 600 MHz hardware cycle counter (`mcycle`), calculates system uptime, and monitors peer heartbeats for deadlocks. |

---

## Shared Memory & Inter-Process Communication (IPC)

Because the cores operate concurrently, they need a way to report their status back to Hart 1 (which owns the UART) without colliding or causing race conditions. This is achieved using a **Lockless Shared Memory Block** stored in DDR RAM.

### The Telemetry Structure (`hart_shared.h`)

```c
typedef struct {
    volatile uint32_t state;
    volatile uint64_t heartbeat;
    volatile uint64_t iterations;
    volatile uint64_t errors;
    volatile uint64_t custom_data;
    char task_name[32];
} __attribute__((aligned(64))) hart_telemetry_t;

typedef struct {
    volatile bool system_ready;
    hart_telemetry_t hart[5]; // Index 1..4 map to U54 Harts
} __attribute__((aligned(64))) hart_shared_block_t;

extern hart_shared_block_t g_hart_shared;
```

**Key Design Elements:**
1. **Cache-Line Alignment:** Structures are aligned to 64 bytes (`__attribute__((aligned(64)))`) to prevent *false sharing* across the L2 cache.
2. **Volatile Keyword:** Forces the GCC compiler to fetch fresh values directly from memory/L2 cache rather than optimizing them into CPU registers, ensuring Hart 1 always reads the latest telemetry from Harts 2-4.
3. **Memory Barriers (`fence rw, rw`):** Used to ensure memory writes are globally visible to all other harts before subsequent instructions are executed.

---

## Synchronization Flow

When HSS boots the application via `serialboot` or an SPI payload, it sends an Inter-Processor Interrupt (IPI) to wake all four U54 harts simultaneously. 

To prevent Harts 2, 3, and 4 from executing before the system is initialized:

1. **Barrier Entry:** Harts 2, 3, and 4 enter a polling loop waiting for `g_hart_shared.system_ready == true`.
2. **Master Initialization:** Hart 1 initializes the UART peripheral, clears soft interrupts, and sets up its own telemetry.
3. **Barrier Release:** Hart 1 sets `g_hart_shared.system_ready = true` and issues a memory barrier.
4. **Execution:** Harts 2, 3, and 4 break out of their loops and begin their isolated `while(1)` workloads. Hart 1 enters the interactive CLI loop.

---

## Important Development Lessons

During the creation of this multi-hart example, several architecture-specific hurdles were addressed:

### 1. Releasing All Application Harts
By default, bare-metal templates often restrict execution to a single core. To allow all four U54 cores to wake up, the HAL must be instructed by passing the following flags to the compiler (defined in `board.mk`):
```makefile
CFLAGS += -DMPFS_HAL_FIRST_HART=1 -DMPFS_HAL_LAST_HART=4
```

### 2. Machine Mode Timer Access (`rdtime` vs `rdcycle`)
Attempting to read the `time` CSR (`rdtime`) in Machine Mode (`PRV_M`) on SiFive U54 cores will trigger an **Illegal Instruction Exception**. The `time` CSR is unprivileged/user-level, and the hardware expects the Machine-level timer to be read via memory-mapped registers (CLINT). 
* **Fix:** Hart 4 was updated to use the `mcycle` CSR (`rdcycle`) instead, calculating real-time seconds by dividing by the 600 MHz core clock frequency.

### 3. `newlib-nano` 64-bit Integer Formatting
To save memory, the build system links against `newlib-nano` (`--specs=nano.specs`). By default, `newlib-nano` lacks support for the `%llu` and `%llx` (long long) print formatters. 
* **Fix:** On 64-bit RISC-V (`lp64d`), standard `long` integers are inherently 64-bit. We cast 64-bit values to `(unsigned long)` and format them using `%lu` and `%lX`.

---

## Running the Example

1. **Compile the application:**
   ```bash
   make BOARD=mpfs-disco-kit clean ddr
   ```
2. **Stream to the board via HSS:**
   ```bash
   make BOARD=mpfs-disco-kit ddr-boot
   ```
3. **Open the Application Console** (`/dev/ttyUSB2` on Discovery Kit, `/dev/ttyUSB1` on Icicle Kit) and type the `harts` command:

```text
mpfs-disco-kit> harts

================ PolarFire SoC Multi-Hart Status ================
 Core | State   | Workload Description     | Loops / Data
------+---------+--------------------------+---------------------
  1   | RUNNING | Interactive CLI Master   | Iter: 3       
------+---------+--------------------------+---------------------
  2   | RUNNING | Compute (CRC32 Engine)   | Iter: 18402   
      |         | Last CRC Result          | 0xD4B29A10
------+---------+--------------------------+---------------------
  3   | RUNNING | Memory Stress (DDR Bus)  | Iter: 4910    
      |         | Buffer Addr / Err Count  | 0x80012A40 (Err: 0)
------+---------+--------------------------+---------------------
  4   | RUNNING | Watchdog & System Timer  | Iter: 94821092
      |         | System Uptime (seconds)  | 24 s
------+---------+--------------------------+---------------------

mpfs-disco-kit>
```