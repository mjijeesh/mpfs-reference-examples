#ifndef LWIPOPTS_H
#define LWIPOPTS_H

/* Bare-metal NO_SYS superloop */
#define NO_SYS                          1
#define SYS_LIGHTWEIGHT_PROT            0

/* Heap & pool sizing scaled to L2-LIM SRAM vs DDR */
#define MEM_ALIGNMENT                   8
#if defined(MEM_TARGET_LIM)
#define MEM_SIZE                        (20 * 1024)
#define MEMP_NUM_PBUF                   16
#define MEMP_NUM_UDP_PCB                6
#define MEMP_NUM_TCP_PCB                8
#define MEMP_NUM_TCP_SEG                16
#define MEMP_NUM_SYS_TIMEOUT            12
#define PBUF_POOL_SIZE                  8
#else
#define MEM_SIZE                        (128 * 1024)
#define MEMP_NUM_PBUF                   32
#define MEMP_NUM_UDP_PCB                8
#define MEMP_NUM_TCP_PCB                16
#define MEMP_NUM_TCP_SEG                32
#define MEMP_NUM_SYS_TIMEOUT            16
#define PBUF_POOL_SIZE                  32
#endif
#define PBUF_POOL_BUFSIZE               1536

/* Protocols */
#define LWIP_ARP                        1
#define LWIP_ETHERNET                   1
#define LWIP_ICMP                       1
#define LWIP_RAW                        1
#define LWIP_DHCP                       1
#define LWIP_UDP                        1
#define LWIP_TCP                        1
#define LWIP_IGMP                       0
#define LWIP_DNS                        0
#define TCP_MSS                         1460
#define TCP_WND                         (4 * TCP_MSS)
#define TCP_SND_BUF                     (4 * TCP_MSS)

#define LWIP_NETCONN                    0
#define LWIP_SOCKET                     0
#define LWIP_STATS                      0
#define LWIP_NETIF_STATUS_CALLBACK      1
#define LWIP_NETIF_LINK_CALLBACK        1

/* Built-in HTTP & TFTP apps */
#define LWIP_HTTPD                      1
#define LWIP_HTTPD_CGI                  1
#define LWIP_HTTPD_SSI                  0
#define HTTPD_USE_CUSTOM_FSDATA         1
#define TFTP_MAX_FILENAME_LEN           64


/* Source-based IPv4 routing: ensure replies exit the interface owning src IP */
struct netif;
struct ip4_addr;
struct netif *net_port_route_src(const struct ip4_addr *src, const struct ip4_addr *dest);
#define LWIP_HOOK_IP4_ROUTE_SRC(src, dest) net_port_route_src((src), (dest))

#endif /* LWIPOPTS_H */