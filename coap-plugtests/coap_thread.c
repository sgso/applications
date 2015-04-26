#include <stdio.h>

#include "byteorder.h"
#include "kernel.h"
#include "periph/random.h"
#include "net/ng_pktbuf.h"
#include "net/ng_netbase.h"
#include "net/ng_udp.h"
#include "net/ng_ipv6/hdr.h"

#include "coap_thread.h"
#include "coap.h"


#define MSG_RETRANSMIT 0x4554
#define MSG_CHECKASYNC 0x7667



#define ENABLE_DEBUG (1)
#include "debug.h"

static bool coap_init(void)
{
    static bool initialized = false;

    if (initialized) {
        return true;
    }
    else {
        /* initiialize loggin */
        coap_set_log_level(LOG_DEBUG);

        /* initialize coap clock */
        coap_clock_init();

        /* initialize random */
        //random_init();

        initialized = true;
        return true;
    }
}

/**
 * @brief   Maybe you are a golfer?! No?!
 */
void *coap_run_context(void *arg)
{
    coap_context_t *ctx = (coap_context_t *)arg;

    if (!ctx) {
        DEBUG("coap: coap context NULL\n");
        return NULL;
    }

    /* RIOT netapi-specific variables */
    msg_t msg;
    msg_t msg_queue[COAP_MSG_QUEUE_SIZE];
    ng_netreg_entry_t me_reg;

    /* libcoap-specific variables */
    coap_tick_t now;
    coap_queue_t *nextpdu;

    /* Timers */
    timex_t retrans_time, check_time;
    vtimer_t retrans_notify, check_notify;

    if (!coap_init()) {
        DEBUG("failed to initialize coap\n");
        return NULL;
    }

    /* register interest in all UDP packets on our port */
    me_reg.demux_ctx = ctx->endpoint->addr.port;
    me_reg.pid = thread_getpid();
    ng_netreg_register(NG_NETTYPE_UDP, &me_reg);

    /* initialize message queue */
    msg_init_queue(msg_queue, COAP_MSG_QUEUE_SIZE);

    /* The time between checking for changed resources */
    check_time.microseconds = 0;
    check_time.seconds = COAP_RESOURCE_CHECK_TIME;

    vtimer_set_msg(&check_notify, check_time,
                   sched_active_pid, MSG_CHECKASYNC, NULL);

    DEBUG("coap: starting server loop on port %u.\n", ctx->endpoint->addr.port);

    /* dispatch NETAPI messages */
    while (1) {
        coap_ticks(&now);
        /* wait for incoming message as long as the timeout is */
        /* DEBUG("coap: waiting for incoming message.\n"); */
        msg_receive(&msg);

        switch (msg.type) {
            case NG_NETAPI_MSG_TYPE_RCV:
                DEBUG("coap: NG_NETAPI_MSG_TYPE_RCV\n");
                coap_handle_message(ctx, ctx->endpoint, (coap_packet_t *)msg.content.ptr);
                break;

            case MSG_RETRANSMIT:
                DEBUG("coap: MSG_RETRANSMIT\n");

                /* Loops over all pdus scheduled to send */
                while (nextpdu && nextpdu->t <= now - ctx->sendqueue_basetime) {
                    coap_retransmit(ctx, coap_pop_next(ctx));
                }

                break;

            case MSG_CHECKASYNC:
                /* DEBUG("coap: MSG_CHECKASYNC\n"); */
                vtimer_set_msg(&check_notify, check_time,
                               sched_active_pid, MSG_CHECKASYNC, NULL);
                break;

            default:
                DEBUG("coap: received unidentified message\n");
                break;
        }

        /* Returns the next pdu to retransmit without removing from
           sendqeue. */
        nextpdu = coap_peek_next(ctx);

        if (nextpdu && nextpdu->t <= COAP_RESOURCE_CHECK_TIME) {
            /* set timeout if there is a pdu to send before our
             * automatic timeout occurs */
            retrans_time.microseconds =
                ((nextpdu->t) % COAP_TICKS_PER_SECOND) *
                1000000 / COAP_TICKS_PER_SECOND;
            retrans_time.seconds = (nextpdu->t) / COAP_TICKS_PER_SECOND;
            /* (Re)set the retransmission vtimer */
            vtimer_set_msg(&retrans_notify, retrans_time,
                           sched_active_pid, MSG_RETRANSMIT, NULL);
        }
    }

    /* never reached */
    return NULL;
}
