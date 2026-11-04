#include "uart.h"
#include "drivers/mss/mss_mmuart/mss_uart.h"
#include "mpfs_hal/mss_hal.h"

/* Default fallbacks if not defined by Makefile */
#ifndef MICROCHIP_STDIO_THRU_MMUARTX
#define MICROCHIP_STDIO_THRU_MMUARTX (&g_mss_uart1_lo)
#endif
#ifndef MICROCHIP_STDIO_PERIPH
#define MICROCHIP_STDIO_PERIPH MSS_PERIPH_MMUART1
#endif

/* Local pointer alias to ensure correct pointer indirection */
static mss_uart_instance_t * const s_uart = MICROCHIP_STDIO_THRU_MMUARTX;

void uart_init(void)
{
    /* Enable peripheral clock for the selected UART */
    (void)mss_config_clk_rst(MICROCHIP_STDIO_PERIPH, (uint8_t)1, PERIPHERAL_ON);

    MSS_UART_init(
        s_uart,
        MSS_UART_115200_BAUD,
        MSS_UART_DATA_8_BITS | MSS_UART_NO_PARITY | MSS_UART_ONE_STOP_BIT
    );
}

void uart_write(char c)
{
    uint8_t byte = (uint8_t)c;
    MSS_UART_polled_tx(s_uart, &byte, 1);
}

char uart_read(void)
{
    uint8_t rx_byte = 0;
    while (MSS_UART_get_rx(s_uart, &rx_byte, 1) == 0) {
        /* Wait for incoming character */
    }
    return (char)rx_byte;
}

int uart_read_nonblock(void)
{
    uint8_t rx_byte = 0;
    if (MSS_UART_get_rx(s_uart, &rx_byte, 1) > 0) {
        return (int)rx_byte;
    }
    return 0;
}

void uart_sync(void)
{
    /* Wait until transmitter is empty (LSR bit 6 = TEMT) */
    while ((s_uart->hw_reg->LSR & 0x40u) == 0) {
        __asm__ __volatile__("" ::: "memory");
    }
}