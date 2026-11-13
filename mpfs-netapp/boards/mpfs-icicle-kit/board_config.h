#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "mpfs_hal/mss_hal.h"
#include "drivers/mss/mss_mmuart/mss_uart.h"
#include "drivers/mss/mss_ethernet_mac/mss_ethernet_mac_sw_cfg.h"
#undef __I
#undef __O
#undef __IO
#include "drivers/mss/mss_ethernet_mac/mss_ethernet_registers.h"
#include "drivers/mss/mss_ethernet_mac/mss_ethernet_mac.h"

#define APP_BOARD_NAME          "PolarFire SoC Icicle Kit"

/* HSS owns MMUART0; U54 application stdio uses MMUART1 */
#define APP_UART                (&g_mss_uart1_lo)

/* Icicle Kit: GEM1 + VSC8662 PHY (SGMII, MDIO routed through GEM0) */
#define APP_MAC                 (&g_mac1)
#define APP_MDIO_MAC            (&g_mac0)
#define APP_MAC_POLL_ISR()      mac1_int_plic_IRQHandler()
#define APP_PHY_ADDR            9U
#define APP_PHY_INTERFACE       SGMII
#define APP_PHY_TYPE            MSS_MAC_DEV_PHY_VSC8662

/* Default MAC & static fallback IPv4 (used if DHCP times out) */
#define APP_MAC_ADDR            { 0x00, 0x04, 0xA3, 0x11, 0x22, 0x60 }
#define APP_STATIC_IP           { 192, 168, 20, 51 }
#define APP_STATIC_NETMASK      { 255, 255, 255, 0 }
#define APP_STATIC_GW           { 192, 168, 20, 1 }

#endif /* BOARD_CONFIG_H */