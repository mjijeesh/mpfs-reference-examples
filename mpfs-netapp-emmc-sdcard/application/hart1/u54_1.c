/* application/hart1/u54_1.c */
#include <stdio.h>
#include <stdint.h>
#include "board_config.h"
#include "net_port.h"
#include "web_server.h"
#include "tftp_server_app.h"
#include "mmc_storage.h"

/* Memory-mapped RISC-V CLINT mtime register (ticks at 1 MHz -> 1 tick = 1 us) */
#define CLINT_MTIME_ADDR    0x0200BFF8UL

/* Board-specific peripheral clock/reset initialization (defined in board_init.c) */
extern void board_periph_init(void);

/**
 * @brief Transmits a null-terminated string over the application debug UART.
 *
 * Uses blocking (polled) transmission on APP_UART (typically MMUART1) so boot
 * diagnostics and CLI responses are guaranteed to flush immediately, even before
 * interrupts are fully configured.
 *
 * @param s Pointer to the null-terminated string to transmit.
 */
static void uart_puts(const char *s)
{
    MSS_UART_polled_tx_string(APP_UART, (const uint8_t *)s);
}

/**
 * @brief Returns system uptime in milliseconds using the hardware CLINT timer.
 *
 * Reads the 64-bit memory-mapped RISC-V machine timer (mtime), which increments
 * at 1 MHz on the PolarFire SoC, and converts microseconds to milliseconds.
 *
 * @return uint32_t Elapsed time in milliseconds since system reset.
 */
static uint32_t uptime_ms(void)
{
    uint64_t ticks = *(volatile uint64_t *)CLINT_MTIME_ADDR;
    return (uint32_t)(ticks / 1000ULL);
}

/**
 * @brief Non-blocking interactive serial CLI handler polled in the main loop.
 *
 * Checks APP_UART for a single incoming character without stalling network
 * polling, and dispatches runtime diagnostics, storage self-tests, and
 * dynamic Bank 4 MSSIO media switching between eMMC and microSD (slot J22):
 *   - 's' / 'S' : Print Ethernet link status, MAC, IPv4 config, and packet counts
 *   - 'm' / 'M' / 't' / 'T' : Print active storage medium and stored file metadata
 *   - 'w' / 'W' : Run a non-destructive 512 B write/verify test at sector 20479
 *   - 'e' / 'E' : Switch Bank 4 mux to onboard 8-bit eMMC and initialize
 *   - 'c' / 'C' : Switch Bank 4 mux to 4-bit microSD slot J22 and initialize
 *   - 'h' / '?' : Display available keyboard shortcuts
 */
static void poll_uart_cli(void)
{
    uint8_t rx = 0U;

    /* Poll for 1 byte; returns 0 immediately if the RX FIFO is empty */
    if (MSS_UART_get_rx(APP_UART, &rx, 1U) > 0U) {
        switch (rx) {
            case 's':
            case 'S':
                /* Display link state, IPv4 address, and RX/TX counters for all NICs */
                net_port_print_status();
                break;

            case 'm':
            case 'M':
            case 't':
            case 'T':
                /* Read sector 20480 and print active media + stored TFTP file details */
                mmc_storage_print_info();
                break;

            case 'w':
            case 'W':
                /* Write and verify a 512-byte pattern on scratch sector 20479 */
                mmc_storage_run_selftest();
                break;

            case 'h':
            case '?':
                uart_puts("\r\n[CLI] Keys: [s] Net Status  [m] eMMC/SD Info  [w] eMMC/SD Write Test  [e] eMMC  [c] SD Card  [h] Help\r\n");
                break;

            case 'e':
            case 'E':
                /* Reconfigure Bank 4 MSSIO pads for onboard 1.8V 8-bit eMMC */
                uart_puts("\r\n[CLI] Switching Bank 4 mux to onboard eMMC...\r\n");
                (void)mmc_storage_init_media(true);
                break;

            case 'c':
            case 'C':
                /* Reconfigure Bank 4 MSSIO pads for 4-bit microSD slot J22 */
                uart_puts("\r\n[CLI] Switching Bank 4 mux to microSD slot J22...\r\n");
                (void)mmc_storage_init_media(false);
                break;

            default:
                /* Ignore unrecognized characters (such as CR/LF) */
                break;
        }
    }
}

/**
 * @brief Main application entry point running on U54 Application Hart 1.
 *
 * Called by the MPFS HAL startup code after low-level CPU, clock, and memory
 * initialization completes. Executes a four-stage bare-metal bring-up sequence:
 *   1. Enables peripheral clocks/resets and initializes MMUART1 (115200 8-N-1).
 *   2. Initializes the eMMC / microSD block storage driver (mmc_storage_init).
 *   3. Brings up the Gigabit Ethernet MACs, lwIP stack, and DHCP/static IP.
 *   4. Starts the HTTP web server (TCP/80) and TFTP-to-MMC server (UDP/69).
 *
 * After initialization, enters a cooperative bare-metal superloop that
 * continuously services lwIP network polling and interactive UART commands.
 */
void u54_1(void)
{
    char msg[256];

    /* Clear any pending inter-hart software interrupt and enable MSIP */
    clear_soft_interrupt();
    set_csr(mie, MIP_MSIP);

    /* Bring MSS peripherals (UART, MAC, eMMC/SD, GPIO) out of reset */
    board_periph_init();

    /* Initialize the serial console at 115200 baud, 8 data bits, no parity, 1 stop bit */
    MSS_UART_init(APP_UART,
                  MSS_UART_115200_BAUD,
                  MSS_UART_DATA_8_BITS | MSS_UART_NO_PARITY | MSS_UART_ONE_STOP_BIT);

    /* Print startup banner with firmware entry address and boot timestamp */
    snprintf(msg, sizeof(msg),
             "\r\n========================================================\r\n"
             " %s - Bare-Metal lwIP + eMMC/SD Demo\r\n"
             " Entry @ 0x%010lx | Boot T+%u ms | Press 'h' for help\r\n"
             "========================================================\r\n",
             APP_BOARD_NAME,
             (unsigned long)(uintptr_t)&u54_1,
             (unsigned int)uptime_ms());
    uart_puts(msg);

    /* [INIT 1/4] Log SYSREG clock and reset registers to verify peripheral enable state */
    snprintf(msg, sizeof(msg),
             "[INIT 1/4] SYSREG CLOCK_CR=0x%08x  SOFT_RESET_CR=0x%08x\r\n",
             (unsigned int)SYSREG->SUBBLK_CLOCK_CR,
             (unsigned int)SYSREG->SOFT_RESET_CR);
    uart_puts(msg);

    /* [INIT 2/4] Probe and initialize eMMC (with SD card fallback) before network services */
    uart_puts("[INIT 2/4] Initializing eMMC / SD Storage...\r\n");
    (void)mmc_storage_init();

    /* [INIT 3/4] Initialize lwIP core, GEM MAC DMA rings, PLIC IRQs, and DHCP/Static IP */
    uart_puts("[INIT 3/4] Initializing Ethernet & lwIP Stack...\r\n");
    net_port_init();

    /* [INIT 4/4] Bind application-layer network daemons (HTTP on TCP/80, TFTP on UDP/69) */
    snprintf(msg, sizeof(msg),
             "[INIT 4/4] Starting HTTP (TCP/80) & TFTP-to-MMC (UDP/69) @ T+%u ms...\r\n",
             (unsigned int)uptime_ms());
    uart_puts(msg);

    web_server_init();
    tftp_server_app_init();

    uart_puts("[APP] Ready. Press 'w' for local eMMC/SD test or send a file via TFTP.\r\n");

    /* Cooperative bare-metal superloop: service network frames/timers and UART CLI */
    while (1) {
        net_port_poll();
        poll_uart_cli();
    }
}
