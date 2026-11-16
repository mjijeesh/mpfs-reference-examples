#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "mpfs_hal/mss_hal.h"
#include "drivers/mss/mss_mmuart/mss_uart.h"
#include "drivers/mss/mss_ethernet_mac/mss_ethernet_registers.h"
#include "drivers/mss/mss_ethernet_mac/mss_ethernet_mac_sw_cfg.h"
#include "drivers/mss/mss_ethernet_mac/mss_ethernet_mac_regs.h"
#include "drivers/mss/mss_ethernet_mac/mss_ethernet_mac.h"
#include "drivers/mss/mss_ethernet_mac/phy.h"

extern uint8_t PLIC_mac1_int_IRQHandler(void);

#define APP_BOARD_NAME              "PolarFire SoC Icicle Kit"

/* HSS owns MMUART0; U54 application stdio uses MMUART1 */
#define APP_UART                    (&g_mss_uart1_lo)

/* Icicle Kit: GEM1 + VSC8662 SGMII PHY (RJ45 J2, MDIO addr 9 via GEM1 MDIO) */
#define APP_MAC                     (&g_mac1)
#define APP_MDIO_MAC                (&g_mac1)
#define APP_MAC_IRQn                MAC1_INT_U54_INT
#define APP_MAC_POLL_ISR()          ((void)PLIC_mac1_int_IRQHandler())
#define APP_PHY_NAME                "GEM1 TBI/SGMII + VSC8662 (J2)"
#define APP_PHY_ADDR                9U /* VSC8662 Port 1 (J2); Port 0 (J1) is 8U */
#define APP_PCS_PHY_ADDR            SGMII_MDIO_ADDR
#define APP_PHY_INTERFACE           TBI
#define APP_PHY_TYPE                MSS_MAC_DEV_PHY_VSC8662
#define APP_PHY_FLAGS               0U
#define APP_PHY_AUTONEGOTIATE       MSS_MAC_VSC8662_phy_autonegotiate
#define APP_PHY_MAC_AUTONEGOTIATE   MSS_MAC_VSC8662_mac_autonegotiate
#define APP_PHY_GET_LINK_STATUS     MSS_MAC_VSC8662_phy_get_link_status
#define APP_PHY_INIT                MSS_MAC_VSC8662_phy_init
#define APP_PHY_SET_LINK_SPEED      MSS_MAC_VSC8662_phy_set_link_speed

/* Default MAC & static fallback IPv4 (used if DHCP times out) */
#define APP_MAC_ADDR                { 0x00, 0xFC, 0x00, 0x12, 0x34, 0x62 }
#define APP_STATIC_IP               { 192, 168, 20, 52 }
#define APP_STATIC_NETMASK          { 255, 255, 255, 0 }
#define APP_STATIC_GW               { 192, 168, 20, 1 }

/* Board-specific peripheral clock, reset & interrupt setup */
void board_periph_init(void);

#endif /* BOARD_CONFIG_H */