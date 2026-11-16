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
#define ETH_MIN_LEN             60U

#define RX_SLOT_SIZE            1536U



#define DHCP_FALLBACK_MS        8000U

#if defined(MEM_TARGET_LIM)
#define TX_RING_SLOTS           4U
#define RX_QUEUE_SIZE           8U
#else
#define TX_RING_SLOTS           8U
#define RX_QUEUE_SIZE           64U
#endif


typedef struct {
    uint8_t  data[PACKET_MAX];
    uint32_t len;
} raw_frame_t;

static raw_frame_t g_rx_raw_queue[RX_QUEUE_SIZE];
static volatile uint32_t g_rx_q_head = 0U;
static volatile uint32_t g_rx_q_tail = 0U;

static uint8_t g_mac_rx_buffer[MSS_MAC_RX_RING_SIZE][RX_SLOT_SIZE] __attribute__((aligned(16)));
static uint8_t g_mac_tx_buffer[TX_RING_SLOTS][TX_SLOT_SIZE] __attribute__((aligned(16)));

//static uint8_t g_mac_rx_buffer[MSS_MAC_RX_RING_SIZE][MSS_MAC_MAX_RX_BUF_SIZE] __attribute__((aligned(16)));
//static uint8_t g_mac_tx_buffer[TX_RING_SLOTS][TX_SLOT_SIZE] __attribute__((aligned(16)));
static volatile uint32_t g_tx_ring_head = 0U;

static struct netif g_netif;
static mss_mac_cfg_t g_mac_cfg;

static volatile uint32_t g_rx_count = 0U;
static volatile uint32_t g_tx_count = 0U;

static uint8_t  g_last_link_up = 0U;
static uint8_t  g_dhcp_done    = 0U;
static uint32_t g_link_up_ms   = 0U;




static void net_log(const char *s)
{
    MSS_UART_polled_tx_string(APP_UART, (const uint8_t *)s);
}

static uint32_t now_ms(void)
{
    uint64_t ticks = *(volatile uint64_t *)CLINT_MTIME_ADDR;
    return (uint32_t)(ticks / 1000ULL);
}

u32_t sys_now(void)
{
    return (u32_t)now_ms();
}

static const char *speed_to_str(mss_mac_speed_t speed)
{
    switch (speed) {
        case MSS_MAC_10MBPS:   return "10 Mbps";
        case MSS_MAC_100MBPS:  return "100 Mbps";
        case MSS_MAC_1000MBPS: return "1000 Mbps";
        default:               return "Unknown";
    }
}

static void mac_tx_callback(void *p_mac, uint32_t queue_no,
                            mss_mac_tx_desc_t *cdesc, void *p_user_data)
{
    (void)p_mac;
    (void)queue_no;
    (void)cdesc;
    (void)p_user_data;
    g_tx_count++;
}

static void mac_rx_callback(void *p_mac, uint32_t queue_no, uint8_t *p_rx_packet,
                            uint32_t pckt_length, mss_mac_rx_desc_t *cdesc,
                            void *p_user_data)
{
    (void)queue_no;
    (void)cdesc;
    (void)p_user_data;

    if ((p_rx_packet != NULL) && (pckt_length > 0U) && (pckt_length <= PACKET_MAX)) {
        g_rx_count++;
        uint32_t next = (g_rx_q_head + 1U) % RX_QUEUE_SIZE;
        if (next != g_rx_q_tail) {
            memcpy(g_rx_raw_queue[g_rx_q_head].data, p_rx_packet, pckt_length);
            g_rx_raw_queue[g_rx_q_head].len = pckt_length;
            __sync_synchronize();
            g_rx_q_head = next;
        }
    }

    (void)MSS_MAC_receive_pkt((mss_mac_instance_t *)p_mac, 0U, p_rx_packet, 0U, 1);
}

static err_t low_level_output(struct netif *netif, struct pbuf *p)
{
    (void)netif;
    if ((p == NULL) || (p->tot_len > PACKET_MAX)) {
        return ERR_BUF;
    }

    if (APP_MAC->queue[0].nb_available_tx_desc == 0U) {
        return ERR_MEM;
    }

    uint32_t idx = g_tx_ring_head;
    g_tx_ring_head = (g_tx_ring_head + 1U) % TX_RING_SLOTS;
    uint8_t *tx_buf = g_mac_tx_buffer[idx];

    uint32_t len = (uint32_t)pbuf_copy_partial(p, tx_buf, p->tot_len, 0U);
    if (len < ETH_MIN_LEN) {
        memset(&tx_buf[len], 0, ETH_MIN_LEN - len);
        len = ETH_MIN_LEN;
    }
    PLIC_DisableIRQ(APP_MAC_IRQn);
    uint8_t st = MSS_MAC_send_pkt(APP_MAC, 0U, tx_buf, len, (void *)1);
    PLIC_EnableIRQ(APP_MAC_IRQn);
    return (st == MSS_MAC_SUCCESS) ? ERR_OK : ERR_MEM;
}

static err_t netif_init_cb(struct netif *netif)
{
    netif->name[0]    = 'e';
    netif->name[1]    = '0';
    netif->output     = etharp_output;
    netif->linkoutput = low_level_output;
    netif->mtu        = 1500U;
    netif->hwaddr_len = 6U;
    memcpy(netif->hwaddr, APP_MAC->mac_addr, 6U);
    netif->flags      = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP | NETIF_FLAG_ETHERNET | NETIF_FLAG_LINK_UP;
    return ERR_OK;
}

static void apply_static_ip(void)
{
    const uint8_t ip_b[4]   = APP_STATIC_IP;
    const uint8_t mask_b[4] = APP_STATIC_NETMASK;
    const uint8_t gw_b[4]   = APP_STATIC_GW;
    ip4_addr_t ip, mask, gw;

    IP4_ADDR(&ip,   ip_b[0],   ip_b[1],   ip_b[2],   ip_b[3]);
    IP4_ADDR(&mask, mask_b[0], mask_b[1], mask_b[2], mask_b[3]);
    IP4_ADDR(&gw,   gw_b[0],   gw_b[1],   gw_b[2],   gw_b[3]);

    netif_set_addr(&g_netif, &ip, &mask, &gw);
}

void net_port_print_status(void)
{
    char line[256];
    char ip_str[16], mask_str[16], gw_str[16];
    mss_mac_speed_t speed = MSS_MAC_1000MBPS;
    uint8_t fullduplex = 1U;
    uint8_t link_up = MSS_MAC_get_link_status(APP_MAC, &speed, &fullduplex);

    ip4addr_ntoa_r(netif_ip4_addr(&g_netif),    ip_str,   sizeof(ip_str));
    ip4addr_ntoa_r(netif_ip4_netmask(&g_netif), mask_str, sizeof(mask_str));
    ip4addr_ntoa_r(netif_ip4_gw(&g_netif),      gw_str,   sizeof(gw_str));

    snprintf(line, sizeof(line),
             "\r\n[NET @ T+%u ms] Link : %s (%s, %s)\r\n"
             "[NET] MAC  : %02x:%02x:%02x:%02x:%02x:%02x (PHY addr %u)\r\n"
             "[NET] IP   : %s\r\n"
             "[NET] Mask : %s\r\n"
             "[NET] GW   : %s\r\n"
             "[NET] Pkts : RX=%u  TX=%u\r\n",
             (unsigned int)now_ms(),
             link_up ? "UP" : "DOWN",
             link_up ? speed_to_str(speed) : "N/A",
             link_up ? (fullduplex ? "Full-Duplex" : "Half-Duplex") : "N/A",
             g_netif.hwaddr[0], g_netif.hwaddr[1], g_netif.hwaddr[2],
             g_netif.hwaddr[3], g_netif.hwaddr[4], g_netif.hwaddr[5],
             (unsigned int)APP_PHY_ADDR,
             ip_str, mask_str, gw_str,
             (unsigned int)g_rx_count, (unsigned int)g_tx_count);
    net_log(line);
}

void net_port_init(void)
{
    char line[192];
    const uint8_t mac[6] = APP_MAC_ADDR;

    g_tx_ring_head = 0U;
    g_rx_q_head    = 0U;
    g_rx_q_tail    = 0U;
    g_rx_count     = 0U;
    g_tx_count     = 0U;

    MSS_MAC_cfg_struct_def_init(&g_mac_cfg);
    g_mac_cfg.speed_duplex_select = MSS_MAC_ANEG_ALL_SPEEDS;
    memcpy(g_mac_cfg.mac_addr, mac, 6U);

    g_mac_cfg.tsu_clock_select      = 1U;
    g_mac_cfg.phy_addr              = APP_PHY_ADDR;
    g_mac_cfg.phy_type              = APP_PHY_TYPE;
    g_mac_cfg.phy_flags             = APP_PHY_FLAGS;
    g_mac_cfg.pcs_phy_addr          = APP_PCS_PHY_ADDR;
    g_mac_cfg.interface_type        = APP_PHY_INTERFACE;

    g_mac_cfg.phy_autonegotiate     = APP_PHY_AUTONEGOTIATE;
    g_mac_cfg.phy_mac_autonegotiate = APP_PHY_MAC_AUTONEGOTIATE;
    g_mac_cfg.phy_get_link_status   = APP_PHY_GET_LINK_STATUS;
    g_mac_cfg.phy_init              = APP_PHY_INIT;
    g_mac_cfg.phy_set_link_speed    = APP_PHY_SET_LINK_SPEED;
    

    MSS_MAC_init(APP_MAC, &g_mac_cfg);

    MSS_MAC_set_tx_callback(APP_MAC, 0U, mac_tx_callback);
    MSS_MAC_set_rx_callback(APP_MAC, 0U, mac_rx_callback);

    for (uint32_t count = 0U; count < MSS_MAC_RX_RING_SIZE; ++count) {
        int enable = (count != (MSS_MAC_RX_RING_SIZE - 1U)) ? 0 : -1;
        (void)MSS_MAC_receive_pkt(APP_MAC, 0U, g_mac_rx_buffer[count], 0U, enable);
    }

    snprintf(line, sizeof(line),
             "[MAC] " APP_PHY_NAME " ready (RX ring=%u, avail_rx=%u, avail_tx=%u)\r\n",
             (unsigned int)MSS_MAC_RX_RING_SIZE,
             (unsigned int)APP_MAC->queue[0].nb_available_rx_desc,
             (unsigned int)APP_MAC->queue[0].nb_available_tx_desc);
    net_log(line);

    lwip_init();
    netif_add(&g_netif, IP4_ADDR_ANY4, IP4_ADDR_ANY4, IP4_ADDR_ANY4,
              APP_MAC, netif_init_cb, ethernet_input);
    netif_set_default(&g_netif);
    netif_set_up(&g_netif);
    netif_set_link_up(&g_netif);

    PLIC_EnableIRQ(APP_MAC_IRQn);

    mss_mac_speed_t speed = MSS_MAC_1000MBPS;
    uint8_t fullduplex = 1U;
    g_last_link_up = MSS_MAC_get_link_status(APP_MAC, &speed, &fullduplex);

#if LWIP_DHCP
    g_dhcp_done  = 0U;
    g_link_up_ms = now_ms();
    dhcp_start(&g_netif);
    net_log("[DHCP] Started DHCP discovery (8s timeout -> static fallback)...\r\n");
#else
    g_dhcp_done = 1U;
    apply_static_ip();
#endif

    net_port_print_status();
}

void net_port_poll(void)
{
    char line[128];
    mss_mac_speed_t speed = MSS_MAC_1000MBPS;
    uint8_t fullduplex = 1U;

    sys_check_timeouts();

    /* Drain packets queued by MAC0 RX ISR */
    while (g_rx_q_head != g_rx_q_tail) {
        uint32_t tail = g_rx_q_tail;
        uint32_t len  = g_rx_raw_queue[tail].len;
        const uint8_t *data = g_rx_raw_queue[tail].data;

        struct pbuf *p = pbuf_alloc(PBUF_RAW, (u16_t)len, PBUF_POOL);
        if (p != NULL) {
            pbuf_take(p, data, (u16_t)len);
            if (g_netif.input(p, &g_netif) != ERR_OK) {
                pbuf_free(p);
            }
        }

        __sync_synchronize();
        g_rx_q_tail = (tail + 1U) % RX_QUEUE_SIZE;
    }

    uint8_t link_up = MSS_MAC_get_link_status(APP_MAC, &speed, &fullduplex);
    if (link_up != g_last_link_up) {
        g_last_link_up = link_up;
        if (link_up != 0U) {
            netif_set_link_up(&g_netif);
            g_link_up_ms = now_ms();
            g_dhcp_done  = 0U;
            snprintf(line, sizeof(line), "\r\n[PHY] Link UP -> %s %s\r\n",
                     speed_to_str(speed), fullduplex ? "Full-Duplex" : "Half-Duplex");
            net_log(line);
#if LWIP_DHCP
            dhcp_stop(&g_netif);
            netif_set_addr(&g_netif, IP4_ADDR_ANY4, IP4_ADDR_ANY4, IP4_ADDR_ANY4);
            dhcp_start(&g_netif);
#else
            g_dhcp_done = 1U;
            net_port_print_status();
#endif
        } else {
            netif_set_link_down(&g_netif);
            net_log("\r\n[PHY] Link DOWN\r\n");
        }
    }

#if LWIP_DHCP
    if ((g_last_link_up != 0U) && (g_dhcp_done == 0U)) {
        if (dhcp_supplied_address(&g_netif)) {
            g_dhcp_done = 1U;
            net_log("[DHCP] Lease acquired!\r\n");
            net_port_print_status();
        } else if ((now_ms() - g_link_up_ms) >= DHCP_FALLBACK_MS) {
            g_dhcp_done = 1U;
            dhcp_stop(&g_netif);
            apply_static_ip();
            net_log("[DHCP] Timed out -> applied static IPv4.\r\n");
            net_port_print_status();
        }
    }
#endif
}