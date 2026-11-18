#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "board_config.h"
#include "net_port.h"

#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/etharp.h"
#include "lwip/dhcp.h"
#include "lwip/timeouts.h"
#include "lwip/ip4_addr.h"
#include "netif/ethernet.h"

#define CLINT_MTIME_ADDR        0x0200BFF8UL
#define PACKET_MAX              1518U
#define TX_SLOT_SIZE            1536U
#define RX_SLOT_SIZE            1536U
#define ETH_MIN_LEN             60U
#define DHCP_FALLBACK_MS        8000U

#if defined(MEM_TARGET_LIM)
#define TX_RING_SLOTS           4U
#define RX_QUEUE_SIZE           8U
#else
#define TX_RING_SLOTS           8U
#define RX_QUEUE_SIZE           64U
#endif

/**
 * @brief Holds a single raw ethernet frame queued by the RX interrupt.
 */
typedef struct {
    uint8_t  data[PACKET_MAX];
    uint32_t len;
} raw_frame_t;

/**
 * @brief Static hardware configuration for a single Ethernet port.
 */
typedef struct {
    const char          if_name[2];
    const char         *label;
    mss_mac_instance_t *mac_dev;
    mss_mac_instance_t *mdio_mac;
    PLIC_IRQn_Type      irqn;
    uint8_t             phy_addr;
    uint8_t             pcs_phy_addr;
    uint8_t             phy_type;
    uint32_t            phy_flags;
    uint32_t            interface_type;
    __typeof__(((mss_mac_cfg_t *)0)->phy_init)              phy_init;
    __typeof__(((mss_mac_cfg_t *)0)->phy_set_link_speed)    phy_set_link_speed;
    __typeof__(((mss_mac_cfg_t *)0)->phy_autonegotiate)     phy_autonegotiate;
    __typeof__(((mss_mac_cfg_t *)0)->phy_mac_autonegotiate) phy_mac_autonegotiate;
    __typeof__(((mss_mac_cfg_t *)0)->phy_get_link_status)   phy_get_link_status;
    uint8_t             mac_addr[6];
    uint8_t             static_ip[4];
    uint8_t             static_mask[4];
    uint8_t             static_gw[4];
} net_port_hw_t;

/**
 * @brief Dynamic runtime context for a single Ethernet port.
 */
typedef struct {
    const net_port_hw_t *hw;
    struct netif         netif;
    mss_mac_cfg_t        mac_cfg;
    raw_frame_t          rx_raw_queue[RX_QUEUE_SIZE];
    volatile uint32_t    rx_q_head;
    volatile uint32_t    rx_q_tail;
    /* 16-byte aligned hardware buffers required by the MAC DMA engine */
    uint8_t              mac_rx_buffer[MSS_MAC_RX_RING_SIZE][RX_SLOT_SIZE] __attribute__((aligned(16)));
    uint8_t              mac_tx_buffer[TX_RING_SLOTS][TX_SLOT_SIZE] __attribute__((aligned(16)));
    volatile uint32_t    tx_ring_head;
    volatile uint32_t    rx_count;
    volatile uint32_t    tx_count;
    uint8_t              last_link_up;
    uint8_t              dhcp_done;
    uint32_t             link_up_ms;
} net_port_ctx_t;

/* Backward compatibility if a board_config.h defines single-NIC APP_* macros */
#ifndef NET_PORT_LIST
#ifndef APP_MDIO_MAC
#define APP_MDIO_MAC NULL
#endif
#define NET_PORT_LIST                                               \
    {                                                               \
        .if_name               = { 'e', '0' },                      \
        .label                 = APP_PHY_NAME,                      \
        .mac_dev               = APP_MAC,                           \
        .mdio_mac              = APP_MDIO_MAC,                      \
        .irqn                  = APP_MAC_IRQn,                      \
        .phy_addr              = APP_PHY_ADDR,                      \
        .pcs_phy_addr          = APP_PCS_PHY_ADDR,                  \
        .phy_type              = APP_PHY_TYPE,                      \
        .phy_flags             = APP_PHY_FLAGS,                     \
        .interface_type        = APP_PHY_INTERFACE,                 \
        .phy_init              = APP_PHY_INIT,                      \
        .phy_set_link_speed    = APP_PHY_SET_LINK_SPEED,            \
        .phy_autonegotiate     = APP_PHY_AUTONEGOTIATE,             \
        .phy_mac_autonegotiate = APP_PHY_MAC_AUTONEGOTIATE,         \
        .phy_get_link_status   = APP_PHY_GET_LINK_STATUS,           \
        .mac_addr              = APP_MAC_ADDR,                      \
        .static_ip             = APP_STATIC_IP,                     \
        .static_mask           = APP_STATIC_NETMASK,                \
        .static_gw             = APP_STATIC_GW,                     \
    }
#endif

/* Global array instantiating hardware config for all defined network ports */
static const net_port_hw_t g_port_hw[] = {
    NET_PORT_LIST
};

#define NET_PORT_COUNT ((uint32_t)(sizeof(g_port_hw) / sizeof(g_port_hw[0])))

/* Global array holding runtime state for all network ports */
static net_port_ctx_t g_ports[NET_PORT_COUNT];

/**
 * @brief Helper to print network logs over the application UART.
 */
static void net_log(const char *s)
{
    MSS_UART_polled_tx_string(APP_UART, (const uint8_t *)s);
}

/**
 * @brief Fetches system uptime in milliseconds via the RISC-V CLINT timer.
 */
static uint32_t now_ms(void)
{
    uint64_t ticks = *(volatile uint64_t *)CLINT_MTIME_ADDR;
    return (uint32_t)(ticks / 1000ULL);
}

/**
 * @brief Required lwIP hook: returns system time in ms.
 */
u32_t sys_now(void)
{
    return (u32_t)now_ms();
}

/**
 * @brief Helper to stringify MAC speed enum values.
 */
static const char *speed_to_str(mss_mac_speed_t speed)
{
    switch (speed) {
        case MSS_MAC_10MBPS:   return "10 Mbps";
        case MSS_MAC_100MBPS:  return "100 Mbps";
        case MSS_MAC_1000MBPS: return "1000 Mbps";
        default:               return "Unknown";
    }
}

/**
 * @brief Look up a port context structure by its hardware MAC pointer.
 */
static net_port_ctx_t *find_port_by_mac(const void *p_mac)
{
    for (uint32_t i = 0U; i < NET_PORT_COUNT; ++i) {
        if ((g_ports[i].hw != NULL) && ((const void *)g_ports[i].hw->mac_dev == p_mac)) {
            return &g_ports[i];
        }
    }
    return NULL;
}

/**
 * @brief Called by lwIP to dynamically route outbound traffic to the correct interface.
 */
struct netif *net_port_route_src(const ip4_addr_t *src, const ip4_addr_t *dest)
{
    (void)dest;

    if ((src == NULL) || ip4_addr_isany(src)) {
        return NULL;
    }

    for (uint32_t i = 0U; i < NET_PORT_COUNT; ++i) {
        struct netif *netif = &g_ports[i].netif;
        if (netif_is_up(netif) && netif_is_link_up(netif) &&
            !ip4_addr_isany(netif_ip4_addr(netif)) &&
            ip4_addr_cmp(src, netif_ip4_addr(netif))) {
            return netif;
        }
    }

    return NULL;
}

/**
 * @brief Hardware interrupt callback fired when a packet transmission completes.
 */
static void mac_tx_callback(void *p_mac, uint32_t queue_no,
                            mss_mac_tx_desc_t *cdesc, void *p_user_data)
{
    (void)queue_no;
    (void)cdesc;
    (void)p_user_data;

    net_port_ctx_t *ctx = find_port_by_mac(p_mac);
    if (ctx != NULL) {
        ctx->tx_count++;
    }
}

/**
 * @brief Hardware interrupt callback fired when a packet is received.
 * 
 * Safely copies the incoming packet into a lock-free queue for processing
 * outside the ISR context, then re-arms the MAC DMA buffer.
 */
static void mac_rx_callback(void *p_mac, uint32_t queue_no, uint8_t *p_rx_packet,
                            uint32_t pckt_length, mss_mac_rx_desc_t *cdesc,
                            void *p_user_data)
{
    (void)queue_no;
    (void)cdesc;
    (void)p_user_data;

    net_port_ctx_t *ctx = find_port_by_mac(p_mac);
    if ((ctx != NULL) && (p_rx_packet != NULL) && (pckt_length > 0U) && (pckt_length <= PACKET_MAX)) {
        ctx->rx_count++;
        uint32_t next = (ctx->rx_q_head + 1U) % RX_QUEUE_SIZE;
        if (next != ctx->rx_q_tail) {
            /* Copy raw packet into ring buffer for main-loop processing */
            memcpy(ctx->rx_raw_queue[ctx->rx_q_head].data, p_rx_packet, pckt_length);
            ctx->rx_raw_queue[ctx->rx_q_head].len = pckt_length;
            __sync_synchronize(); /* Memory barrier ensuring copy completes before head increments */
            ctx->rx_q_head = next;
        }
    }

    /* Return the buffer to the MAC DMA engine */
    (void)MSS_MAC_receive_pkt((mss_mac_instance_t *)p_mac, 0U, p_rx_packet, 0U, 1);
}

/**
 * @brief Translates an lwIP pbuf into a physical hardware transmission.
 */
static err_t low_level_output(struct netif *netif, struct pbuf *p)
{
    net_port_ctx_t *ctx = (net_port_ctx_t *)netif->state;
    if ((ctx == NULL) || (ctx->hw == NULL) || (p == NULL) || (p->tot_len > PACKET_MAX)) {
        return ERR_BUF;
    }

    mss_mac_instance_t *mac = ctx->hw->mac_dev;
    if (mac->queue[0].nb_available_tx_desc == 0U) {
        return ERR_MEM;
    }

    /* Grab the next available 16-byte aligned TX slot */
    uint32_t idx = ctx->tx_ring_head;
    ctx->tx_ring_head = (ctx->tx_ring_head + 1U) % TX_RING_SLOTS;
    uint8_t *tx_buf = ctx->mac_tx_buffer[idx];

    /* Flatten lwIP's chained pbuf into a single contiguous hardware buffer */
    uint32_t len = (uint32_t)pbuf_copy_partial(p, tx_buf, p->tot_len, 0U);
    if (len < ETH_MIN_LEN) {
        /* Pad frames under 60 bytes (Ethernet requirement) */
        memset(&tx_buf[len], 0, ETH_MIN_LEN - len);
        len = ETH_MIN_LEN;
    }

    /* Briefly mask interrupts to kick the MAC hardware */
    PLIC_DisableIRQ(ctx->hw->irqn);
    uint8_t st = MSS_MAC_send_pkt(mac, 0U, tx_buf, len, (void *)1);
    PLIC_EnableIRQ(ctx->hw->irqn);
    return (st == MSS_MAC_SUCCESS) ? ERR_OK : ERR_MEM;
}

/**
 * @brief lwIP netif registration callback initializing MAC address and capabilities.
 */
static err_t netif_init_cb(struct netif *netif)
{
    net_port_ctx_t *ctx = (net_port_ctx_t *)netif->state;

    netif->name[0]    = ctx->hw->if_name[0];
    netif->name[1]    = ctx->hw->if_name[1];
    netif->output     = etharp_output;
    netif->linkoutput = low_level_output;
    netif->mtu        = 1500U;
    netif->hwaddr_len = 6U;
    memcpy(netif->hwaddr, ctx->hw->mac_dev->mac_addr, 6U);
    netif->flags      = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET | NETIF_FLAG_LINK_UP;
    return ERR_OK;
}

/**
 * @brief Applies fallback static IP addressing if DHCP fails.
 */
static void apply_static_ip(net_port_ctx_t *ctx)
{
    ip4_addr_t ip, mask, gw;

    IP4_ADDR(&ip,   ctx->hw->static_ip[0],   ctx->hw->static_ip[1],   ctx->hw->static_ip[2],   ctx->hw->static_ip[3]);
    IP4_ADDR(&mask, ctx->hw->static_mask[0], ctx->hw->static_mask[1], ctx->hw->static_mask[2], ctx->hw->static_mask[3]);
    IP4_ADDR(&gw,   ctx->hw->static_gw[0],   ctx->hw->static_gw[1],   ctx->hw->static_gw[2],   ctx->hw->static_gw[3]);

    netif_set_addr(&ctx->netif, &ip, &mask, &gw);
}

/**
 * @brief Prints current interface telemetry to UART.
 */
static void print_single_port_status(net_port_ctx_t *ctx)
{
    char line[256];
    char ip_str[16], mask_str[16], gw_str[16];
    mss_mac_speed_t speed = MSS_MAC_1000MBPS;
    uint8_t fullduplex = 1U;
    uint8_t link_up = MSS_MAC_get_link_status(ctx->hw->mac_dev, &speed, &fullduplex);

    ip4addr_ntoa_r(netif_ip4_addr(&ctx->netif),    ip_str,   sizeof(ip_str));
    ip4addr_ntoa_r(netif_ip4_netmask(&ctx->netif), mask_str, sizeof(mask_str));
    ip4addr_ntoa_r(netif_ip4_gw(&ctx->netif),      gw_str,   sizeof(gw_str));

    snprintf(line, sizeof(line),
             "\r\n[NET %c%c @ T+%u ms] %s\r\n"
             "[NET %c%c] Link : %s (%s, %s)\r\n"
             "[NET %c%c] MAC  : %02x:%02x:%02x:%02x:%02x:%02x (PHY addr %u)\r\n"
             "[NET %c%c] IP   : %s | Mask: %s | GW: %s\r\n"
             "[NET %c%c] Pkts : RX=%u  TX=%u\r\n",
             ctx->hw->if_name[0], ctx->hw->if_name[1], (unsigned int)now_ms(), ctx->hw->label,
             ctx->hw->if_name[0], ctx->hw->if_name[1],
             link_up ? "UP" : "DOWN",
             link_up ? speed_to_str(speed) : "N/A",
             link_up ? (fullduplex ? "Full-Duplex" : "Half-Duplex") : "N/A",
             ctx->hw->if_name[0], ctx->hw->if_name[1],
             ctx->netif.hwaddr[0], ctx->netif.hwaddr[1], ctx->netif.hwaddr[2],
             ctx->netif.hwaddr[3], ctx->netif.hwaddr[4], ctx->netif.hwaddr[5],
             (unsigned int)ctx->hw->phy_addr,
             ctx->hw->if_name[0], ctx->hw->if_name[1], ip_str, mask_str, gw_str,
             ctx->hw->if_name[0], ctx->hw->if_name[1],
             (unsigned int)ctx->rx_count, (unsigned int)ctx->tx_count);
    net_log(line);
}

/**
 * @brief Iterates over all ports to print link status.
 */
void net_port_print_status(void)
{
    for (uint32_t i = 0U; i < NET_PORT_COUNT; ++i) {
        print_single_port_status(&g_ports[i]);
    }
}

/**
 * @brief Boot initialization mapping hardware configs to lwIP netifs.
 */
void net_port_init(void)
{
    char line[192];

    lwip_init();

    for (uint32_t i = 0U; i < NET_PORT_COUNT; ++i) {
        net_port_ctx_t *ctx = &g_ports[i];
        memset(ctx, 0, sizeof(*ctx));
        ctx->hw = &g_port_hw[i];

        const net_port_hw_t *hw = ctx->hw;
        mss_mac_instance_t *mac = hw->mac_dev;

        MSS_MAC_cfg_struct_def_init(&ctx->mac_cfg);
        ctx->mac_cfg.speed_duplex_select = MSS_MAC_ANEG_ALL_SPEEDS;
        memcpy(ctx->mac_cfg.mac_addr, hw->mac_addr, 6U);

        ctx->mac_cfg.tsu_clock_select      = 1U;
        ctx->mac_cfg.phy_controller        = hw->mdio_mac;
        ctx->mac_cfg.phy_addr              = hw->phy_addr;
        ctx->mac_cfg.phy_type              = hw->phy_type;
        ctx->mac_cfg.phy_flags             = hw->phy_flags;
        ctx->mac_cfg.pcs_phy_addr          = hw->pcs_phy_addr;
        ctx->mac_cfg.interface_type        = hw->interface_type;

        ctx->mac_cfg.phy_autonegotiate     = hw->phy_autonegotiate;
        ctx->mac_cfg.phy_mac_autonegotiate = hw->phy_mac_autonegotiate;
        ctx->mac_cfg.phy_get_link_status   = hw->phy_get_link_status;
        ctx->mac_cfg.phy_init              = hw->phy_init;
        ctx->mac_cfg.phy_set_link_speed    = hw->phy_set_link_speed;

        MSS_MAC_init(mac, &ctx->mac_cfg);

        MSS_MAC_set_tx_callback(mac, 0U, mac_tx_callback);
        MSS_MAC_set_rx_callback(mac, 0U, mac_rx_callback);

        for (uint32_t count = 0U; count < MSS_MAC_RX_RING_SIZE; ++count) {
            int enable = (count != (MSS_MAC_RX_RING_SIZE - 1U)) ? 0 : -1;
            (void)MSS_MAC_receive_pkt(mac, 0U, ctx->mac_rx_buffer[count], 0U, enable);
        }

        snprintf(line, sizeof(line),
                 "[MAC %c%c] %s ready (RX ring=%u, avail_rx=%u, avail_tx=%u)\r\n",
                 hw->if_name[0], hw->if_name[1],
                 hw->label,
                 (unsigned int)MSS_MAC_RX_RING_SIZE,
                 (unsigned int)mac->queue[0].nb_available_rx_desc,
                 (unsigned int)mac->queue[0].nb_available_tx_desc);
        net_log(line);

        netif_add(&ctx->netif, IP4_ADDR_ANY4, IP4_ADDR_ANY4, IP4_ADDR_ANY4,
                  ctx, netif_init_cb, ethernet_input);
        if (i == 0U) {
            netif_set_default(&ctx->netif);
        }
        netif_set_up(&ctx->netif);
        netif_set_link_up(&ctx->netif);

        PLIC_EnableIRQ(hw->irqn);

        mss_mac_speed_t speed = MSS_MAC_1000MBPS;
        uint8_t fullduplex = 1U;
        ctx->last_link_up = MSS_MAC_get_link_status(mac, &speed, &fullduplex);
        if (ctx->last_link_up != 0U) {
            netif_set_link_up(&ctx->netif);
        } else {
            netif_set_link_down(&ctx->netif);
        }

#if LWIP_DHCP
        ctx->dhcp_done  = 0U;
        ctx->link_up_ms = now_ms();
        dhcp_start(&ctx->netif);
        snprintf(line, sizeof(line),
                 "[DHCP %c%c] Started DHCP discovery (8s timeout -> static fallback)...\r\n",
                 hw->if_name[0], hw->if_name[1]);
        net_log(line);
#else
        ctx->dhcp_done = 1U;
        apply_static_ip(ctx);
#endif
    }

    net_port_print_status();
}

/**
 * @brief Master cyclic network poll function.
 * 
 * Called continuously from the main application loop. It services lwIP timers, 
 * unloads raw packets from the ISR lock-free queue into lwIP, tracks physical
 * PHY link state changes, and enforces DHCP fallbacks.
 */
void net_port_poll(void)
{
    char line[128];
    mss_mac_speed_t speed = MSS_MAC_1000MBPS;
    uint8_t fullduplex = 1U;

    /* Advance lwIP state machines (TCP timeouts, DHCP timers, etc.) */
    sys_check_timeouts();

    for (uint32_t i = 0U; i < NET_PORT_COUNT; ++i) {
        net_port_ctx_t *ctx = &g_ports[i];

        /* Drain packets queued by this port's MAC RX ISR */
        while (ctx->rx_q_head != ctx->rx_q_tail) {
            uint32_t tail = ctx->rx_q_tail;
            uint32_t len  = ctx->rx_raw_queue[tail].len;
            const uint8_t *data = ctx->rx_raw_queue[tail].data;

            /* Safely allocate and hand off packet to lwIP outside interrupt context */
            struct pbuf *p = pbuf_alloc(PBUF_RAW, (u16_t)len, PBUF_POOL);
            if (p != NULL) {
                pbuf_take(p, data, (u16_t)len);
                if (ctx->netif.input(p, &ctx->netif) != ERR_OK) {
                    pbuf_free(p);
                }
            }

            __sync_synchronize();
            ctx->rx_q_tail = (tail + 1U) % RX_QUEUE_SIZE;
        }

        /* Check for PHY unplug/replug events */
        uint8_t link_up = MSS_MAC_get_link_status(ctx->hw->mac_dev, &speed, &fullduplex);
        if (link_up != ctx->last_link_up) {
            ctx->last_link_up = link_up;
            if (link_up != 0U) {
                netif_set_link_up(&ctx->netif);
                ctx->link_up_ms = now_ms();
                ctx->dhcp_done  = 0U;
                snprintf(line, sizeof(line), "\r\n[PHY %c%c] Link UP -> %s %s\r\n",
                         ctx->hw->if_name[0], ctx->hw->if_name[1],
                         speed_to_str(speed), fullduplex ? "Full-Duplex" : "Half-Duplex");
                net_log(line);
#if LWIP_DHCP
                /* Restart DHCP transaction on hot-plug */
                dhcp_stop(&ctx->netif);
                netif_set_addr(&ctx->netif, IP4_ADDR_ANY4, IP4_ADDR_ANY4, IP4_ADDR_ANY4);
                dhcp_start(&ctx->netif);
#else
                ctx->dhcp_done = 1U;
                print_single_port_status(ctx);
#endif
            } else {
                netif_set_link_down(&ctx->netif);
                snprintf(line, sizeof(line), "\r\n[PHY %c%c] Link DOWN\r\n",
                         ctx->hw->if_name[0], ctx->hw->if_name[1]);
                net_log(line);
            }
        }

#if LWIP_DHCP
        /* Evaluate DHCP lease status if link is stable */
        if ((ctx->last_link_up != 0U) && (ctx->dhcp_done == 0U)) {
            if (dhcp_supplied_address(&ctx->netif)) {
                ctx->dhcp_done = 1U;
                snprintf(line, sizeof(line), "[DHCP %c%c] Lease acquired!\r\n",
                         ctx->hw->if_name[0], ctx->hw->if_name[1]);
                net_log(line);
                print_single_port_status(ctx);
            } else if ((now_ms() - ctx->link_up_ms) >= DHCP_FALLBACK_MS) {
                /* DHCP server didn't respond; apply static IP to allow point-to-point connections */
                ctx->dhcp_done = 1U;
                dhcp_stop(&ctx->netif);
                apply_static_ip(ctx);
                snprintf(line, sizeof(line), "[DHCP %c%c] Timed out -> applied static IPv4.\r\n",
                         ctx->hw->if_name[0], ctx->hw->if_name[1]);
                net_log(line);
                print_single_port_status(ctx);
            }
        }
#endif
    }
}
