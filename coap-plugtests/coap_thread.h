#ifndef COAP_THREAD_H
#define COAP_THREAD_H

#include "coap.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief   Default message queue size for the CoAP thread
 */
#ifndef COAP_MSG_QUEUE_SIZE
#define COAP_MSG_QUEUE_SIZE   (8U)
#endif

#ifndef COAP_PORT
#define COAP_PORT (5683U)
#endif

/**
 * @brief   Priority of the CoAP thread
 */
#ifndef COAP_PRIO
#define COAP_PRIO             (PRIORITY_MAIN - 1)
#endif


static const ng_ipv6_addr_t ipv6_addr_any;
/**
 * @brief   Initializes the coap endpoint @p ep
 *
 * @param[out] ep   Pointer to a coap endpoint.
 * @param[in] addr  The local interface address to bind to.
 * @param[in] port  The UDP port number to bind to.
 * @param[in] netif A valid network interface pid.
 *
 * @return  0 on success
 * @return  -1 on error
 */
static inline int coap_init_endpoint(coap_endpoint_t *ep, ng_ipv6_addr_t addr,
                                     uint16_t port, kernel_pid_t netif)
{
    if (ep && thread_getstatus(netif) != STATUS_NOT_FOUND) {
        ep->addr.addr = addr;
        ep->addr.port = port;
        ep->ifindex = netif;
        /* No options yet */
        ep->flags = COAP_ENDPOINT_NOSEC;
        /* Rip this out of the coap_endpoint_t structure. We don't need it */
        ep->handle = 42;
        return 0;
    }
    else {
        return -1;
    }
}

/**
 * @brief Initializes the coap context @p ctx and associates the
 * endpoint @ep with it
 *
 * @param[out] ctx  Pointer to a coap context.
 * @param[in] ep    The endpoint to use with @ ctx
 * @param[in] msgid The value used for the first message id.
 *                  If passed 0, the first message id will be generated randomly.
 *
 * @return  0 on success
 * @return  -1 on error
 */
static inline int coap_init_context(coap_context_t *ctx, coap_endpoint_t *ep, unsigned short msgid)
{
    memset(ctx, 0, sizeof(coap_context_t));

    if (ep) {
        ctx->endpoint = ep;
    }
    else {
        return -1;
    }

    /* initialize message id */
    if (msgid > 0) {
        ctx->message_id = msgid;
    }
    else {
        prng((unsigned char *)&ctx->message_id, sizeof(unsigned short));
    }

    /* register the critical options that we know */
    coap_register_option(ctx, COAP_OPTION_IF_MATCH);
    coap_register_option(ctx, COAP_OPTION_URI_HOST);
    coap_register_option(ctx, COAP_OPTION_IF_NONE_MATCH);
    coap_register_option(ctx, COAP_OPTION_URI_PORT);
    coap_register_option(ctx, COAP_OPTION_URI_PATH);
    coap_register_option(ctx, COAP_OPTION_URI_QUERY);
    coap_register_option(ctx, COAP_OPTION_ACCEPT);
    coap_register_option(ctx, COAP_OPTION_PROXY_URI);
    coap_register_option(ctx, COAP_OPTION_PROXY_SCHEME);
    coap_register_option(ctx, COAP_OPTION_BLOCK2);
    coap_register_option(ctx, COAP_OPTION_BLOCK1);

    return 0;
}

void *coap_run_context(void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* COAP_THREAD_H */
