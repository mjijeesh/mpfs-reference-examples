#include "board_config.h"

void board_periph_init(void)
{
    SYSREG->SOFT_RESET_CR   = 0U;
    SYSREG->SUBBLK_CLOCK_CR = 0xFFFFFFFFUL;

    (void)mss_config_clk_rst(MSS_PERIPH_MMUART1, (uint8_t)MPFS_HAL_FIRST_HART, PERIPHERAL_ON);
    (void)mss_config_clk_rst(MSS_PERIPH_MAC0,    (uint8_t)MPFS_HAL_FIRST_HART, PERIPHERAL_ON);
    (void)mss_config_clk_rst(MSS_PERIPH_MAC1,    (uint8_t)MPFS_HAL_FIRST_HART, PERIPHERAL_ON);

    PLIC_init();
    __enable_irq();
}