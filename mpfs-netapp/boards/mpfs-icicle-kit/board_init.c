#include "board_config.h"

void board_periph_init(void)
{
    (void)mss_config_clk_rst(MSS_PERIPH_MMUART1, (uint8_t)MPFS_HAL_FIRST_HART, PERIPHERAL_ON);
    (void)mss_config_clk_rst(MSS_PERIPH_MAC0,    (uint8_t)MPFS_HAL_FIRST_HART, PERIPHERAL_ON);
    (void)mss_config_clk_rst(MSS_PERIPH_MAC1,    (uint8_t)MPFS_HAL_FIRST_HART, PERIPHERAL_ON);
}