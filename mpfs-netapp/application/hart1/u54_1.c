#include <stdio.h>
#include <stdint.h>
#include "board_config.h"
#include "net_port.h"
#include "web_server.h"
#include "tftp_server_app.h"

#define CLINT_MTIME_ADDR    0x0200BFF8UL

extern void board_periph_init(void);

static void uart_puts(const char *s)
{
    MSS_UART_polled_tx_string(APP_UART, (const uint8_t *)s);
}

static uint32_t uptime_ms(void)
{
    uint64_t ticks = *(volatile uint64_t *)CLINT_MTIME_ADDR;
    return (uint32_t)(ticks / 1000ULL);
}

static void poll_uart_cli(void)
{
    uint8_t rx = 0U;
    if (MSS_UART_get_rx(APP_UART, &rx, 1U) > 0U) {
        switch (rx) {
            case 's':
            case 'S':
                net_port_print_status();
                break;
            case 't':
            case 'T':
                tftp_server_print_info();
                break;
            case 'h':
            case '?':
                uart_puts("\r\n[CLI] Keys: [s] Network Status  [t] TFTP RAM-Disk Info  [h] Help\r\n");
                break;
            default:
                break;
        }
    }
}

void u54_1(void)
{
    char msg[256];

    clear_soft_interrupt();
    set_csr(mie, MIP_MSIP);

    board_periph_init();

    MSS_UART_init(APP_UART,
                  MSS_UART_115200_BAUD,
                  MSS_UART_DATA_8_BITS | MSS_UART_NO_PARITY | MSS_UART_ONE_STOP_BIT);

    snprintf(msg, sizeof(msg),
             "\r\n========================================================\r\n"
             " %s - Bare-Metal lwIP NetApp\r\n"
             " Entry @ 0x%010lx | Boot T+%u ms | Press 'h' for help\r\n"
             "========================================================\r\n",
             APP_BOARD_NAME,
             (unsigned long)(uintptr_t)&u54_1,
             (unsigned int)uptime_ms());
    uart_puts(msg);

    snprintf(msg, sizeof(msg),
             "[INIT 1/7] SYSREG CLOCK_CR=0x%08x  SOFT_RESET_CR=0x%08x\r\n",
             (unsigned int)SYSREG->SUBBLK_CLOCK_CR,
             (unsigned int)SYSREG->SOFT_RESET_CR);
    uart_puts(msg);

    net_port_init();

    snprintf(msg, sizeof(msg), "[INIT 7/7] Starting HTTP (TCP/80) & TFTP (UDP/69) @ T+%u ms...\r\n",
             (unsigned int)uptime_ms());
    uart_puts(msg);

    web_server_init();
    tftp_server_app_init();

    uart_puts("[APP] Initialization complete. Entering main poll loop.\r\n");

    while (1) {
        net_port_poll();
        poll_uart_cli();
    }
}