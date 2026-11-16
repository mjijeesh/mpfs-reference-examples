#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "mpfs_hal/mss_hal.h"
#include "drivers/mss/mss_mmuart/mss_uart.h"
#include "drivers/mss/mss_ethernet_mac/mss_ethernet_registers.h"
#include "drivers/mss/mss_ethernet_mac/mss_ethernet_mac_regs.h"
#include "drivers/mss/mss_ethernet_mac/mss_ethernet_mac_sw_cfg.h"
#include "drivers/mss/mss_ethernet_mac/mss_ethernet_mac.h"
#include "drivers/mss/mss_ethernet_mac/phy.h"

extern uint8_t PLIC_mac0_int_IRQHandler(void);
extern uint8_t PLIC_mac1_int_IRQHandler(void);

#define APP_BOARD_NAME      "PolarFire SoC Icicle Kit"
#define APP_UART            (&g_mss_uart1_lo)

/* Dual-NIC table: J2 (GEM1) initialized first so GEM1 MDIO is ready for J1 (GEM0) */
#define NET_PORT_LIST                                                               \
    {                                                                               \
        /* Port J2: GEM1 Direct, VSC8662 Ch 1 @ MDIO 9 */                           \
        .if_name               = { 'e', '0' },                                      \
        .label                 = "GEM1 TBI/SGMII + VSC8662 (J2)",                   \
        .mac_dev               = &g_mac1,                                           \
        .mdio_mac              = NULL,                                              \
        .irqn                  = MAC1_INT_U54_INT,                                  \
        .phy_addr              = 9U,                                                \
        .pcs_phy_addr          = 16U,                                               \
        .phy_type              = MSS_MAC_DEV_PHY_VSC8662,                           \
        .phy_flags             = 0U,                                                \
        .interface_type        = TBI,                                               \
        .phy_init              = MSS_MAC_VSC8662_phy_init,                          \
        .phy_set_link_speed    = MSS_MAC_VSC8662_phy_set_link_speed,                \
        .phy_autonegotiate     = MSS_MAC_VSC8662_phy_autonegotiate,                 \
        .phy_mac_autonegotiate = MSS_MAC_VSC8662_mac_autonegotiate,                 \
        .phy_get_link_status   = MSS_MAC_VSC8662_phy_get_link_status,               \
        .mac_addr              = { 0x00, 0x04, 0xA3, 0x11, 0x22, 0x34 },            \
        .static_ip             = { 192, 168, 20, 52 },                              \
        .static_mask           = { 255, 255, 255, 0 },                              \
        .static_gw             = { 192, 168, 20, 1 },                               \
    },                                                                              \
    {                                                                               \
        /* Port J1: GEM0 Data + GEM1 MDIO, VSC8662 Ch 0 @ MDIO 8 */                 \
        .if_name               = { 'e', '1' },                                      \
        .label                 = "GEM0 TBI/SGMII + VSC8662 (J1)",                   \
        .mac_dev               = &g_mac0,                                           \
        .mdio_mac              = &g_mac1,                                           \
        .irqn                  = MAC0_INT_U54_INT,                                  \
        .phy_addr              = 8U,                                                \
        .pcs_phy_addr          = 16U,                                               \
        .phy_type              = MSS_MAC_DEV_PHY_VSC8662,                           \
        .phy_flags             = 0U,                                                \
        .interface_type        = TBI,                                               \
        .phy_init              = MSS_MAC_VSC8662_phy_init,                          \
        .phy_set_link_speed    = MSS_MAC_VSC8662_phy_set_link_speed,                \
        .phy_autonegotiate     = MSS_MAC_VSC8662_phy_autonegotiate,                 \
        .phy_mac_autonegotiate = MSS_MAC_VSC8662_mac_autonegotiate,                 \
        .phy_get_link_status   = MSS_MAC_VSC8662_phy_get_link_status,               \
        .mac_addr              = { 0x00, 0x04, 0xA3, 0x11, 0x22, 0x33 },            \
        .static_ip             = { 192, 168, 20, 51 },                              \
        .static_mask           = { 255, 255, 255, 0 },                              \
        .static_gw             = { 192, 168, 20, 1 },                               \
    }

void board_periph_init(void);

#endif /* BOARD_CONFIG_H */