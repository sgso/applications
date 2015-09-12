/*
 * Copyright (C) 2015 Sebastian Sontberg <sebastian@sontberg.de>
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

/**
 * @file
 * @brief       Infineon XMC 2Go Kit test application
 *
 * @author      Sebastian Sontberg <sebastian@sontberg.de>
  *
 * @}
 */

#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "board.h"
#include "od.h"
#include "periph/adc.h"
#include "periph/cpuid.h"
#include "periph/gpio.h"
#include "periph/random.h"
#include "periph/rtc.h"
#include "shell.h"
#include "shell_commands.h"
#include "thread.h"
#include "xtimer.h"

#include "xmc_utils.h"
#include "sc_xmc.h"

#define LED_INTERVAL (1000 * 25) /* 25ms */

extern int _rtc_handler(int argc, char **argv);

static char led_stack[THREAD_STACKSIZE_IDLE];
static char halfsec_stack[THREAD_STACKSIZE_IDLE];

static const shell_command_t shell_commands[] = {
    { "xmc", "XMC utilities", sc_xmc },
    { NULL, NULL, NULL }
};

static const gpio_t outputs[] = {
    GPIO(P0, 0),
    GPIO(P0, 5),
    GPIO(P0, 6),
    GPIO(P0, 7),
    GPIO(P0, 8),
    GPIO(P0, 9),
    GPIO(P0, 14),
};

void *halfsec_thread(void *args)
{
    gpio_init(GPIO(P0, 15), GPIO_DIR_OUT, GPIO_NOPULL);

    gpio_set(GPIO(P0, 15));
    gpio_set(GPIO(P1, 0));

    while (1) {
        xtimer_usleep(250 * 1000);
        gpio_toggle(GPIO(P0, 15));
        gpio_toggle(GPIO(P1, 0));
    }

    return NULL;
}

void *led_thread(void *args)
{
    int no_outputs = sizeof(outputs) / sizeof(gpio_t);

    for (int i = 0; i < no_outputs; i++) {
        gpio_init(outputs[i], GPIO_DIR_OUT, GPIO_NOPULL);
    }

    uint32_t last_wakeup = xtimer_now();
    uint8_t rand;
    while (1) {
        xtimer_usleep_until(&last_wakeup, LED_INTERVAL);

        random_read((char *)&rand, 1);
        rand = rand % no_outputs;
        gpio_toggle(outputs[rand]);
    }

    return NULL;
}

void gpio_callback(void *arg)
{
    static unsigned count;
    count++;
    printf("[%u] %s\n", count++, (char *)arg);
    gpio_toggle(GPIO(P1, 1));
}

int main(void)
{
    char buf[SHELL_DEFAULT_BUFSIZE];
    struct tm date;

    /* Get the date from embedded build time string and set the RTC */
    get_build_datetime(&date);
    rtc_set_time(&date);

    /* Init random byte generator */
    random_init();

    printf("You are running RIOT on a "RIOT_BOARD" board with a "RIOT_MCU" mcu.\n");

    printf("RTC time is: ");

    char *params[2] =  { "", "gettime"};
    _rtc_handler(2, &params[0]);

    gpio_init(GPIO(P1, 1), GPIO_DIR_OUT, GPIO_NOPULL);
    gpio_set(GPIO(P1, 1));

    /* Three GPIOs with interrupts, that all we can do */
    gpio_init_int(GPIO(P2, 0), GPIO_NOPULL, GPIO_RISING,
                  gpio_callback, "P2.0 rising");
    gpio_init_int(GPIO(P2, 6), GPIO_NOPULL, GPIO_FALLING,
                  gpio_callback, "P2.6 falling");
    gpio_init_int(GPIO(P2, 7), GPIO_NOPULL, GPIO_BOTH,
                  gpio_callback, "P2.7/8 rising or falling");

    /* These are just readable */
    gpio_init(GPIO(P2, 9), GPIO_DIR_IN, GPIO_NOPULL);
    gpio_init(GPIO(P2, 10), GPIO_DIR_IN, GPIO_NOPULL);
    gpio_init(GPIO(P2, 11), GPIO_DIR_IN, GPIO_NOPULL);

    thread_create(led_stack, sizeof(led_stack),
                  THREAD_PRIORITY_MAIN + 1, CREATE_STACKTEST,
                  led_thread, NULL, "random blink");

    thread_create(halfsec_stack, sizeof(halfsec_stack),
                  THREAD_PRIORITY_MAIN + 2, CREATE_STACKTEST,
                  halfsec_thread, NULL, "250ms blink");


    /* prepare P2.9 for analog input */
    PORT2->PDISC = (1 <<  PORT2_PDISC_PDIS9_Pos);
    /* init ADC */
    adc_init(ADC_0, ADC_RES_12BIT);

    shell_run(shell_commands, buf, SHELL_DEFAULT_BUFSIZE);

    return 0;
}
