#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "mpfs_hal/mss_hal.h"
#include "drivers/mss/mss_mmuart/mss_uart.h"
#include "drivers/mss/mss_ethernet_mac/mss_ethernet_registers.h"
#include "drivers/mss/mss_ethernet_mac/mss_ethernet_mac_sw_cfg.h"
#include "drivers/mss/mss_ethernet_mac/mss_ethernet_mac_regs.h"
#include "drivers/mss/mss_ethernet_mac/mss_ethernet_mac.h"
#include "drivers/mss/mss_ethernet_mac/phy.h"

extern uint8_t PLIC_mac0_int_IRQHandler(void);

#define APP_BOARD_NAME          "PolarFire SoC Discovery Kit"

/* HSS owns MMUART0; U54 application stdio uses MMUART4 */
#define APP_UART                (&g_mss_uart4_lo)

/* Discovery Kit: GEM0 + VSC8221 SGMII PHY (TBI, MDIO addr 11) */
#define APP_MAC                 (&g_mac0)
#define APP_MDIO_MAC            (&g_mac0)
#define APP_MAC_IRQn            MAC0_INT_U54_INT
#define APP_MAC_POLL_ISR()      ((void)PLIC_mac0_int_IRQHandler())
#define APP_PHY_ADDR            PHY_VSC8221_MDIO_ADDR /* 11U */
#define APP_PCS_PHY_ADDR        SGMII_MDIO_ADDR
#define APP_PHY_INTERFACE       TBI
#define APP_PHY_TYPE            MSS_MAC_DEV_PHY_VSC8221
#define APP_PHY_FLAGS           PHY_VSC8221_EEPROM_INIT

/* Default MAC & static fallback IPv4 (used if DHCP times out) */
#define APP_MAC_ADDR            { 0x00, 0xFC, 0x00, 0x12, 0x34, 0x60 }
#define APP_STATIC_IP           { 192, 168, 20, 50 }
#define APP_STATIC_NETMASK      { 255, 255, 255, 0 }
#define APP_STATIC_GW           { 192, 168, 20, 1 }

#endif /* BOARD_CONFIG_H */