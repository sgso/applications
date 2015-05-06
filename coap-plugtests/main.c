/**
 *
 * @file
 * @brief       CoAP plugtest server
 *
 */

/* #define NG_IPV6_PRIO            (PRIORITY_MAIN - 3) */
/* #define NG_UDP_PRIO             (PRIORITY_MAIN - 2) */
/* #define NG_PKTDUMP_PRIO         (PRIORITY_MAIN - 1) */
/* #define COAP_PRIO               (PRIORITY_MAIN - 1) */

#include <stdio.h>
#include <stdlib.h>
#include "board.h"
#include "crash.h"
#include "byteorder.h"
#include "kernel.h"
#include "shell.h"
#include "shell_commands.h"
#include "net/ng_netbase.h"
#include "net/ng_nomac.h"
#include "net/ng_pktdump.h"
#include "net/ng_netdev_eth.h"
#include "net/ng_ipv6.h"
#include "net/ng_udp.h"
#include "net/dev_eth.h"
#include "dev_eth_tap.h"

#include "coap.h"
#include "coap_thread.h"
#include "coap_handlers.h"

#define ENABLE_DEBUG (1)
#include "debug.h"

#define MAC_PRIO                (PRIORITY_MAIN - 4)

/**
 * @brief   Buffer size used by the shell
 */
#define SHELL_BUFSIZE           (128U)

#ifndef NOMAC_STACK_SIZE
#define NOMAC_STACK_SIZE (KERNEL_CONF_STACKSIZE_DEFAULT)
#endif

/**
 * @brief   Default stack size to use for the UDP thread
 */
#ifndef COAP_STACK_SIZE
#define COAP_STACK_SIZE (KERNEL_CONF_STACKSIZE_DEFAULT)
#endif

/**
 * @brief   Stack for the nomac thread
 */
static char nomac_stack[NOMAC_STACK_SIZE];
static char coap_stack[COAP_STACK_SIZE];

/**
 * @Brief   Read chars from STDIO
 */
static int shell_read(void)
{
    return (int)getchar();
}

/**
 * @brief   Write chars to STDIO
 */
static void shell_put(int c)
{
    putchar((char)c);
}


/**
 * @brief  Central point of exit. Replace with something useful or hand
 *         hand back control.
 */
static int error_with(char *msg, int status, int fatal)
{
    if (fatal) {
        core_panic(status, msg);
    }
    else {
        DEBUG("Error: %s\n (%i)\n", msg, status);
        return status;
    }
}

/* Stolen from sc_ipv6_nc.c */
static bool _is_iface(kernel_pid_t iface)
{
#ifdef MODULE_NG_NETIF
    size_t numof;
    kernel_pid_t *ifs = ng_netif_get(&numof);

    for (size_t i = 0; i < numof; i++) {
        if (ifs[i] == iface) {
            return true;
        }
    }

    return false;
#else
    return (thread_get(iface) != NULL);
#endif
}


int udp_send(int argc, char **argv)
{
    ng_pktsnip_t *data_snip, *udp_snip, *ip_snip;
    ng_ipv6_addr_t dest_addr;
    ng_netreg_entry_t *sendto;
    kernel_pid_t iface;
    uint16_t src_port = 0;
    uint16_t dst_port = 0;
    int data_index = 4;
    
    if (argc < 5) {
        puts("usage: udp_send <if_id> <ipv6_addr> <dst_port> [src_port] [data]");
        puts("  (a numeric fourth argument will be interpreted as source port)");
        return EINVAL;
    }
    
    iface = (kernel_pid_t)atoi(argv[1]);

    if (!_is_iface(iface)) {
        puts("error: invalid interface given.");
        return EINVAL;
    }

    if (!ng_ipv6_addr_from_str(&dest_addr, argv[2])) {
        puts("error: invalid destination address given.");
        return EINVAL;
    }

    dst_port = (uint16_t)atoi(argv[3]);

    if ((argv[4][0] <= 0x39) && (argv[4][0] >= 0x30)) {
        src_port = (uint16_t)atoi(argv[4]);
        data_index++;
    }
    else {
        prng((unsigned char *)&src_port, sizeof(uint16_t));
    }
    
    printf("Interface(%u), IPv6(%s), UDP(%u -> %u): \"%s\"\n",
           iface, argv[2], src_port, dst_port, argv[data_index]);
    
    data_snip = ng_pktbuf_add(NULL, argv[data_index], strlen(argv[data_index]),
                              NG_NETTYPE_UNDEF);

    udp_snip = ng_netreg_hdr_build(NG_NETTYPE_UDP, data_snip,
                                   (uint8_t *)&src_port, sizeof(uint16_t),
                                   (uint8_t *)&dst_port, sizeof(uint16_t));

    ip_snip = ng_netreg_hdr_build(NG_NETTYPE_IPV6, udp_snip,
                                  NULL, 0,
                                  (uint8_t *)&dest_addr.u8, sizeof(ng_ipv6_addr_t));

    /* and forward packet to the network layer */
    sendto = ng_netreg_lookup(NG_NETTYPE_UDP, NG_NETREG_DEMUX_CTX_ALL);

    /* throw away packet if no one is interested */
    if (sendto == NULL) {
        printf("netcat: cannot send packet because network layer not found\n");
        ng_pktbuf_release(ip_snip);
        return -1;
    }

    /* send packet to network layer */
    ng_pktbuf_hold(ip_snip, ng_netreg_num(NG_NETTYPE_UDP, NG_NETREG_DEMUX_CTX_ALL) - 1);

    while (sendto != NULL) {
        ng_netapi_send(sendto->pid, ip_snip);
        sendto = ng_netreg_getnext(sendto);
    }

    return 0;
}



/**
 * @brief   Setup a MAC-derived link-local, solicited-nodes and multicast address
 *          on IPv6 interface @p net_if.
 */
static int init_ipv6_linklocal(kernel_pid_t net_if, uint8_t *mac)
{
#if ENABLE_DEBUG
    char addr_buf[NG_IPV6_ADDR_MAX_STR_LEN];
#endif
    ng_ipv6_addr_t link_local, solicited, multicast;
    uint8_t eui64[8] = {0, 0, 0, 0xFF, 0xFE, 0, 0, 0};
    int res;

    /* Generate EUI-64 from MAC address */
    memcpy(&eui64[0], &mac[0], 3);
    memcpy(&eui64[5], &mac[3], 3);
    eui64[0] ^= 1 << 1;

    /* Generate link-local address from local prefix and EUI-64 */
    ng_ipv6_addr_set_link_local_prefix(&link_local);
    ng_ipv6_addr_set_aiid(&link_local, &eui64[0]);

    res = ng_ipv6_netif_add_addr(net_if,
                                 &link_local,
                                 64,
                                 false);

    if (res != 0) {
        return error_with("setting link-local address failed", res, 0);
    }
    else {
        DEBUG("link-local address: %s\n",
              ng_ipv6_addr_to_str(&addr_buf[0],
                                  &link_local,
                                  NG_IPV6_ADDR_MAX_STR_LEN));
    }

    ng_ipv6_addr_set_solicited_nodes(&solicited, &link_local);

    res = ng_ipv6_netif_add_addr(net_if,
                                 &solicited,
                                 NG_IPV6_ADDR_BIT_LEN,
                                 false);

    if (res != 0) {
        return error_with("setting solicited-nodes address failed", res, 0);
    }
    else {
        DEBUG("solicited-nodes address: %s\n",
              ng_ipv6_addr_to_str(&addr_buf[0],
                                  &solicited,
                                  NG_IPV6_ADDR_MAX_STR_LEN));
    }

    /* Setup multicast address */
    memcpy(&multicast, &link_local, sizeof(ng_ipv6_addr_t));

    ng_ipv6_addr_set_multicast(&multicast,
                               NG_IPV6_ADDR_MCAST_FLAG_TRANSIENT |   // No RP, temporary
                               NG_IPV6_ADDR_MCAST_FLAG_PREFIX_BASED, // unicast/prefix based
                               NG_IPV6_ADDR_MCAST_SCP_LINK_LOCAL);   // link-local scope

    res = ng_ipv6_netif_add_addr(net_if,
                                 &multicast,
                                 NG_IPV6_ADDR_BIT_LEN,
                                 false);

    if (res != 0) {
        return error_with("setting multicast address failed", res, 0);
    }
    else {
        DEBUG("multicast address: %s\n",
              ng_ipv6_addr_to_str(&addr_buf[0],
                                  &multicast,
                                  NG_IPV6_ADDR_MAX_STR_LEN));
    }

#if defined(ULA_PREFIX) && defined(ULA_PREFIX_LENGTH)
    /* Setup unique local address if defined */
    ng_ipv6_addr_t ula_addr;
    ng_ipv6_addr_from_str(&ula_addr, ULA_PREFIX);
    ng_ipv6_addr_set_aiid(&ula_addr, &eui64[0]);
    res = ng_ipv6_netif_add_addr(net_if, &ula_addr, ULA_PREFIX_LENGTH, 0);

    if (res < 0) {
        error_with("ULA initialization failed", res, 1);
    }
    else {
        DEBUG("unicast address (ULA): %s\n",
              ng_ipv6_addr_to_str(&addr_buf[0],
                                  &ula_addr,
                                  NG_IPV6_ADDR_MAX_STR_LEN));
    }

#else
#pragma message("ULA_PREFIX or ULA_PREFIX_LENGTH not defined")
#endif
    return 0;
}


/**
 * @brief   main function
 */
int main(void)
{
    int res;
    shell_t shell;
    kernel_pid_t netif, coap;
    size_t num_netif;

    /* initialize network module(s) */
    ng_netif_init();

    /* initialize IPv6 interfaces */
    ng_ipv6_netif_init();

    /* initialize netdev_eth layer */
    ng_netdev_eth_init(&ng_netdev_eth, (dev_eth_t *)&dev_eth_tap);

    /* start MAC layer */
    res = ng_nomac_init(nomac_stack, sizeof(nomac_stack), MAC_PRIO,
                        "eth_mac", (ng_netdev_t *)&ng_netdev_eth);

    if (res < 0) {
        error_with("starting nomac thread failed", res, 1);
    }

    /* initialize IPv6 addresses */
    netif = *(ng_netif_get(&num_netif));

    if (num_netif > 0) {

        DEBUG("Found %i active interface\n", num_netif);
        ng_ipv6_netif_reset_addr(netif);
        res = init_ipv6_linklocal(netif, dev_eth_tap.addr);

        if (res < 0) {
            error_with("link-local address initialization failed", res, 1);
        }
        else {
            DEBUG("Successfully initialized link-local adresses on first interface\n");
        }


        /* Setup neighbour cache while NDP is unavailable */
#ifdef REMOTE_IP
        uint8_t remote_mac[6];
        ng_ipv6_addr_t remote_addr;
        ng_ipv6_addr_from_str(&remote_addr, REMOTE_IP);
#ifdef REMOTE_MAC
        char mac_buf[32];


        memcpy(&mac_buf, REMOTE_MAC, 18);
        ng_netif_addr_from_str(&remote_mac[0],
                               6,
                               &mac_buf[0]);
#else   /* Derive the MAC from our own one. dev_eth_tap sets it plus 1
         * the one it reads from the tap device */
        memcpy(&remote_mac, &dev_eth_tap.addr, sizeof(remote_mac));
        remote_mac[5] -= 1;
#endif  /* REMOTE_MAC */
        res = ng_ipv6_nc_add(netif, &remote_addr, &remote_mac[0], 6, 0);

        if (res < 0) {
            error_with("setup of neighbour cache failed", res, 0);
        }

#endif  /* REMOTE_IP */
    }
    else {
        error_with("no active interfaces", num_netif, 1);
    }

    /* initialize and register pktdump */
    ng_netreg_entry_t dump;
    dump.pid = ng_pktdump_init();
    dump.demux_ctx = NG_NETREG_DEMUX_CTX_ALL;

    if (dump.pid <= KERNEL_PID_UNDEF) {
        puts("Error starting pktdump thread");
        return -1;
    }

    ng_netreg_register(NG_NETTYPE_IPV6, &dump);
    
    /* coap_endpoint_t ep; */
    /* coap_context_t ctx; */

    /* Setup an endpoint for CoAP (::/5683) on netif */
    /* coap_init_endpoint(&ep, */
    /*                    ipv6_addr_any, */
    /*                    COAP_DEFAULT_PORT, */
    /*                    netif); */

    /* You put that into a "context", libcoaps state struct */
    /* coap_init_context(&ctx, &ep, 0); */

    /* Register handlers for resources */
    /* register_handlers(&ctx); */

    /* Run it with with coap_run_context */
    /* coap = thread_create(coap_stack, sizeof(coap_stack), COAP_PRIO, */
    /*                      CREATE_STACKTEST, &coap_run_context, &ctx, "coap"); */

    /* if (coap <= KERNEL_PID_UNDEF) { */
    /*     error_with("starting coap thread failed", coap, 1); */
    /* } */

    /* now coap is running and you can't stop it gracefully */

    /* start the shell */
    const shell_command_t shell_commands[] = {
        {"udp_send", "Send arbitrary UDP packets", udp_send},
        {NULL, NULL, NULL}
    };
    
    shell_init(&shell, shell_commands, SHELL_BUFSIZE, shell_read, shell_put);
    shell_run(&shell);

    return 0;
}










