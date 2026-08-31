/*******************************************************************************
 * lwipopts.h — lwIP Configuration for CM55 WiFi
 *
 * Based on wifi-core-freertos-lwip-mbedtls/configs/lwipopts.h
 * with project-specific tuning for MicroPython + LVGL + WiFi SoftAP/STA.
 *******************************************************************************/

#pragma once

#include <whd_types.h>

#define MEM_ALIGNMENT                   (4)
#define LWIP_RAW                        (1)

/* IPv4/IPv6 */
#define LWIP_IPV4                       (1)
#define LWIP_IPV6                       (1)

#define ETHARP_SUPPORT_STATIC_ENTRIES   (1)

#define LWIP_ICMP                       (1)
#define LWIP_TCP                        (1)
#define LWIP_UDP                        (1)
#define LWIP_IGMP                       (1)

/* Memory allocation: Use C library malloc (goes to FreeRTOS heap in DTCM)
 * instead of lwIP's internal heap (which would go to .cy_gpu_buf in PSRAM).
 * gfx_mem PSRAM is fully used by display framebuffers — no room for lwIP. */
#define MEM_SIZE                        (16*1024)
#define MEM_LIBC_MALLOC                 (1)
#define MEMP_MEM_MALLOC                 (1)

#define LWIP_PROVIDE_ERRNO              (1)

#if !defined(__llvm__) && defined(__GNUC__) && !defined(__ARMCC_VERSION)
#define LWIP_TIMEVAL_PRIVATE            (0)
#endif

#if defined(__llvm__) && !defined(__ARMCC_VERSION)
__attribute__((weak)) __thread int __lwip_errno = 0;
#define errno __lwip_errno
#endif

/* DHCP + DNS */
#define LWIP_DHCP                       (1)
#define DHCP_DOES_ARP_CHECK             (0)
#define LWIP_DNS                        (1)

/* Socket timeouts */
#define LWIP_SO_SNDTIMEO                (1)
#define LWIP_SO_RCVTIMEO                (1)
#define SO_REUSE                        (1)

/* TCP Keep-alive */
#define LWIP_TCP_KEEPALIVE              (1)

/* WHD header space */
#define PBUF_LINK_HLEN                  (WHD_PHYSICAL_HEADER)
#define TCP_MSS                         (WHD_PAYLOAD_MTU)

/* Checksum */
#define LWIP_CHECKSUM_CTRL_PER_NETIF    1
#define CHECKSUM_GEN_IP                 1
#define CHECKSUM_GEN_UDP                1
#define CHECKSUM_GEN_TCP                1
#define CHECKSUM_GEN_ICMP               1
#define CHECKSUM_GEN_ICMP6              1
#define CHECKSUM_CHECK_IP               1
#define CHECKSUM_CHECK_UDP              1
#define CHECKSUM_CHECK_TCP              1
#define CHECKSUM_CHECK_ICMP             1
#define CHECKSUM_CHECK_ICMP6            1
#define LWIP_CHECKSUM_ON_COPY           1

/* Netconn + Socket API */
#define LWIP_NETCONN                    (1)
#define LWIP_SOCKET                     (1)

/* TCP buffer sizes */
#define TCP_SND_BUF                     (4 * TCP_MSS)
#define TCP_SND_QUEUELEN                ((6 * (TCP_SND_BUF) + (TCP_MSS - 1))/(TCP_MSS))

/* System protection */
#define SYS_LIGHTWEIGHT_PROT            (1)
#define LWIP_ALLOW_MEM_FREE_FROM_OTHER_CONTEXT (1)

/* Mailbox/queue sizes */
#define LWIP_SO_RCVBUF                  (128)
#define DEFAULT_TCP_RECVMBOX_SIZE       (12)
#define TCPIP_MBOX_SIZE                 (16)
#define TCPIP_THREAD_STACKSIZE          (4*1024)
#define TCPIP_THREAD_PRIO               (4)
#define DEFAULT_RAW_RECVMBOX_SIZE       (12)
#define DEFAULT_UDP_RECVMBOX_SIZE       (12)
#define DEFAULT_ACCEPTMBOX_SIZE         (8)

/* Pool sizes (moderate for SoftAP + MQTT, no RTSP streaming) */
#define MEMP_NUM_UDP_PCB                8
#define MEMP_NUM_TCP_PCB                8
#define MEMP_NUM_TCP_PCB_LISTEN         2
#define MEMP_NUM_TCP_SEG                27
#define MEMP_NUM_SYS_TIMEOUT            12
#define PBUF_POOL_SIZE                  20
#define MEMP_NUM_NETBUF                 8
#define MEMP_NUM_NETCONN                8

/* Statistics (debug) */
#define LWIP_STATS                      1

/* Thread-safe core locking */
#define LWIP_TCPIP_CORE_LOCKING         1
#define LWIP_TCPIP_CORE_LOCKING_INPUT   1

/* Netif API */
#define LWIP_NETIF_API                  1
#define LWIP_NETIF_TX_SINGLE_PBUF       (1)
#define LWIP_NETIF_STATUS_CALLBACK      (1)
#define LWIP_NETIF_LINK_CALLBACK        (1)
#define LWIP_NETIF_REMOVE_CALLBACK      (1)

#define LWIP_RAND                       rand

/* Core locking check */
#define LWIP_FREERTOS_CHECK_CORE_LOCKING (1)
#define LWIP_ASSERT_CORE_LOCKED()       sys_check_core_locking()

#define LWIP_CHKSUM_ALGORITHM           (3)

/* CM55 GCC checksum workaround */
#if (CY_CPU_CORTEX_M55) && defined(__GNUC__) && !defined(__ARMCC_VERSION) && !defined(__llvm__) && defined(NDEBUG)
#define LWIP_CHKSUM                     cy_lwip_standard_chksum
extern uint16_t                         cy_lwip_standard_chksum();
#endif

extern void sys_check_core_locking();

/* Increased 64 bytes for each pbuf for memory alignment */
#define PBUF_POOL_BUFSIZE LWIP_MEM_ALIGN_SIZE(TCP_MSS+40+PBUF_LINK_ENCAPSULATION_HLEN+PBUF_LINK_HLEN+64)
