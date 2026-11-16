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

#define APP_BOARD_NAME              "PolarFire SoC Icicle Kit"
#define APP_UART                    (&g_mss_uart1_lo)

/* Set to 1 for Port J1 (GEM0), or 0 for Port J2 (GEM1) when using single-NIC mode */
#ifndef ICICLE_USE_PORT_J1
#define ICICLE_USE_PORT_J1          0
#endif

/* Shared VSC8662 SGMII PHY configuration */
#define APP_PCS_PHY_ADDR            16U
#define APP_PHY_INTERFACE           TBI
#define APP_PHY_TYPE                MSS_MAC_DEV_PHY_VSC8662
#define APP_PHY_FLAGS               0U
#define APP_PHY_AUTONEGOTIATE       MSS_MAC_VSC8662_phy_autonegotiate
#define APP_PHY_MAC_AUTONEGOTIATE   MSS_MAC_VSC8662_mac_autonegotiate
#define APP_PHY_GET_LINK_STATUS     MSS_MAC_VSC8662_phy_get_link_status
#define APP_PHY_INIT                MSS_MAC_VSC8662_phy_init
#define APP_PHY_SET_LINK_SPEED      MSS_MAC_VSC8662_phy_set_link_speed

/* Port J1 (GEM0 Data + GEM1 MDIO, VSC8662 Ch 0 @ MDIO 8) */
#define APP_J1_MAC                  (&g_mac0)
#define APP_J1_MDIO_MAC             (&g_mac1)
#define APP_J1_MAC_IRQn             MAC0_INT_U54_INT
#define APP_J1_MAC_POLL_ISR()       ((void)PLIC_mac0_int_IRQHandler())
#define APP_J1_PHY_ADDR             8U
#define APP_J1_MAC_ADDR             { 0x00, 0x04, 0xA3, 0x11, 0x22, 0x33 }
#define APP_J1_STATIC_IP            { 192, 168, 20, 51 }
#define APP_J1_STATIC_NETMASK       { 255, 255, 255, 0 }
#define APP_J1_STATIC_GW            { 192, 168, 20, 1 }

/* Port J2 (GEM1 Data + GEM1 MDIO, VSC8662 Ch 1 @ MDIO 9) */
#define APP_J2_MAC                  (&g_mac1)
#define APP_J2_MDIO_MAC             NULL
#define APP_J2_MAC_IRQn             MAC1_INT_U54_INT
#define APP_J2_MAC_POLL_ISR()       ((void)PLIC_mac1_int_IRQHandler())
#define APP_J2_PHY_ADDR             9U
#define APP_J2_MAC_ADDR             { 0x00, 0x04, 0xA3, 0x11, 0x22, 0x34 }
#define APP_J2_STATIC_IP            { 192, 168, 20, 52 }
#define APP_J2_STATIC_NETMASK       { 255, 255, 255, 0 }
#define APP_J2_STATIC_GW            { 192, 168, 20, 1 }

/* Active primary port aliases for single-netif builds */
#if ICICLE_USE_PORT_J1
#define APP_MAC                     APP_J1_MAC
#define APP_MDIO_MAC                APP_J1_MDIO_MAC
#define APP_MAC_IRQn                APP_J1_MAC_IRQn
#define APP_MAC_POLL_ISR()          APP_J1_MAC_POLL_ISR()
#define APP_PHY_NAME                "GEM0 TBI/SGMII + VSC8662 (J1)"
#define APP_PHY_ADDR                APP_J1_PHY_ADDR
#define APP_MAC_ADDR                APP_J1_MAC_ADDR
#define APP_STATIC_IP               APP_J1_STATIC_IP
#define APP_STATIC_NETMASK          APP_J1_STATIC_NETMASK
#define APP_STATIC_GW               APP_J1_STATIC_GW
#else
#define APP_MAC                     APP_J2_MAC
#define APP_MDIO_MAC                APP_J2_MDIO_MAC
#define APP_MAC_IRQn                APP_J2_MAC_IRQn
#define APP_MAC_POLL_ISR()          APP_J2_MAC_POLL_ISR()
#define APP_PHY_NAME                "GEM1 TBI/SGMII + VSC8662 (J2)"
#define APP_PHY_ADDR                APP_J2_PHY_ADDR
#define APP_MAC_ADDR                APP_J2_MAC_ADDR
#define APP_STATIC_IP               APP_J2_STATIC_IP
#define APP_STATIC_NETMASK          APP_J2_STATIC_NETMASK
#define APP_STATIC_GW               APP_J2_STATIC_GW
#endif

void board_periph_init(void);

#endif /* BOARD_CONFIG_H */