/*
 * Copyright (C) 2015 Sebastian Sontberg <sebastian@sontberg.de>
 *
 * This file is subject to the terms and conditions of the GNU Lesser
 * General Public License v2.1. See the file LICENSE in the top level
 * directory for more details.
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include <stddef.h>

#include "board.h"
#include "od.h"
#include "periph/adc.h"
#include "periph/gpio.h"
#include "periph/timer.h"
#include "sc_xmc.h"
#include "xtimer.h"

/* from linker script */
extern uint32_t _sfixed;
extern uint32_t _efixed;
extern uint32_t _etext;
extern uint32_t _srelocate;
extern uint32_t _erelocate;
extern uint32_t _szero;
extern uint32_t _ezero;
extern uint32_t _sstack;
extern uint32_t _estack;
extern uint32_t _sheap;
extern uint32_t _eheap;
extern uint32_t _sram;
extern uint32_t _eram;

/* from newlib/syscalls.c */
extern char *heap_top;

/* copied from xmcs periph/gpio.c */
#define PIN(gpio)  (gpio & 0x000f)
#define PORT(gpio) (PORT0_BASE | ((gpio & 0x30) << 4))
#define IOCR(gpio) ((uint8_t *)(PORT(gpio) + offsetof(PORT0_Type, IOCR0) + PIN(gpio)))

void print_gpio_status(int port, int pin, bool present, const char *comment)
{
    char buf[24]  = "";
    gpio_t gpio   = GPIO(port, pin);
    uint8_t flags = *(IOCR(gpio)) >> 3;

    sprintf(&buf[0], "P%u.%i", (port >> 4), pin);
    printf("  %-5s    ", &buf[0]);

    if (!present) {
        printf("%c %15s%10s    (%s)\n", '-', "-", "", comment);
        return;
    }

    printf("%i ", gpio_read(gpio) > 0 ? 1 : 0);

    if (flags & GPIO_DIR_OUT) {
        strcpy(&buf[0], "output/");
    }
    else if (flags & GPIO_DIR_IN_INV) {
        strcpy(&buf[0], "invert input/");
    }
    else {
        strcpy(&buf[0], "input/");
    }

    uint8_t offset = strlen(&buf[0]);

    if (flags & 0x7) {
        strncpy(&buf[offset++], (char *)&"0123457"[flags & 0x7], 1);
        buf[offset] = '\0';
    }
    else {
        buf[offset - 1] = '\0';
    }

    printf("%15s", buf);

    memset(&buf, 0, sizeof(buf));

    if (flags & GPIO_PULLDOWN) {
        strcpy(&buf[0], "[down]");
    }
    else if (flags & GPIO_PULLUP) {
        strcpy(&buf[0], "[up]");
    }

    printf("%10s", buf);

    if (comment) {
        printf("    (%s)", comment);
    }

    putchar('\n');
}

void _xmc_sc_gating_status(uint16_t cgatstat, const char *unit, uint16_t msk)
{
    printf("%-12s  - %-25s ", "", unit);

    if (cgatstat & msk) {
        puts("asserted");
    }
    else {
        puts("de-asserted");
    }
}

void _xmc_sc_gating(void)
{
    uint32_t *gating_ptr = (uint32_t *)0x10001014;
    uint32_t gating = *gating_ptr;

    printf("[%8p] %-28s ", (void *)gating_ptr,
           "Initial gating configuration");

    if (gating & 0x80000000) {
        puts("(not activated)");
    }
    else {
        printf("\n");
    }

    gating = ~gating;

    _xmc_sc_gating_status(gating, "VADC/SHS", SCU_CLK_CGATSTAT0_VADC_Msk);
    _xmc_sc_gating_status(gating, "CCU40", SCU_CLK_CGATSTAT0_CCU40_Msk);
    _xmc_sc_gating_status(gating, "USIC0", SCU_CLK_CGATSTAT0_USIC0_Msk);
    _xmc_sc_gating_status(gating, "WDT", SCU_CLK_CGATSTAT0_WDT_Msk);
    _xmc_sc_gating_status(gating, "RTC", SCU_CLK_CGATSTAT0_RTC_Msk);

    printf("\n");

    gating_ptr = (uint32_t *)&SCU_CLK->CGATSTAT0;
    gating = *gating_ptr;

    printf("[%8p] %-28s\n", (void *)gating_ptr,
           "Current module gating status");

    _xmc_sc_gating_status(gating, "VADC/SHS", SCU_CLK_CGATSTAT0_VADC_Msk);
    _xmc_sc_gating_status(gating, "CCU40", SCU_CLK_CGATSTAT0_CCU40_Msk);
    _xmc_sc_gating_status(gating, "USIC0", SCU_CLK_CGATSTAT0_USIC0_Msk);
    _xmc_sc_gating_status(gating, "WDT", SCU_CLK_CGATSTAT0_WDT_Msk);
    _xmc_sc_gating_status(gating, "RTC", SCU_CLK_CGATSTAT0_RTC_Msk);
}

void _xmc_sc_clock_freq(uint32_t clk_val)
{
    unsigned freq;
    uint16_t idiv = (clk_val & SCU_CLK_CLKCR_IDIV_Msk) >> SCU_CLK_CLKCR_IDIV_Pos;
    uint16_t fdiv = (clk_val & SCU_CLK_CLKCR_FDIV_Msk) >> SCU_CLK_CLKCR_FDIV_Pos;

    if (idiv) {
        printf("%-12s %-28s %u\n", "",
               " - Fractional Divider (FDIV)", (unsigned)fdiv);
        printf("%-12s %-28s %u\n", "",
               " - Divider Selection  (IDIV)", (unsigned)idiv);
        freq = ((DCO1_FREQUENCY << 6) / ((idiv << 8) + fdiv)) << 1;
    }
    else {
        printf("%-12s %-28s\n", "", "   (divider bypassed)");
        freq =  DCO1_FREQUENCY >> 1;
    }

    printf("%-12s %-28s %u\n", "",
           "MCLK frequency", freq);

    if (clk_val & SCU_CLK_CLKCR_PCLKSEL_Msk) {
        freq *= 2;
    }

    printf("%-12s %-28s %u\n", "",
           "PCLK frequency", freq);
}

void _xmc_sc_clock(void)
{
    uint32_t *clk_val1_ptr = (uint32_t *)0x10001010;
    uint32_t *scu_clk_ptr = (uint32_t *)&SCU_CLK->CLKCR;

    uint32_t clk_val1 = *clk_val1_ptr;
    uint32_t scu_clk = *scu_clk_ptr;


    printf("[%8p] %-28s 0x%04x ", (void *)clk_val1_ptr,
           "Flash configuration", (unsigned)clk_val1);

    if (clk_val1 & 0x80000000) {
        puts("(not activated)");
    }
    else {
        printf("\n");
    }

    _xmc_sc_clock_freq(clk_val1);

    printf("\n");

    printf("[%8p] %-28s 0x%05x\n", (void *)scu_clk_ptr,
           "Active configuration", (unsigned)scu_clk & 0x1ffff);

    _xmc_sc_clock_freq(scu_clk);
}

void _xmc_sc_mem_region(const char *title, const char *segment, const void *start, const void *end)
{
    unsigned size  = (unsigned)end - (unsigned)start;

    printf("%-12s %-28s ", title ? title : "", segment);
    printf("[%8p, %8p] size: %u\n", start, end, size);
}

void _xmc_sc_data_od(const char *name, const void *data, size_t data_len, uint16_t flags)
{
    printf("[%p] %-28s", data, name);
    od(data, data_len, 80, OD_FLAGS_ADDRESS_NONE | OD_FLAGS_BYTES_HEX | flags);
}

void _xmc_sc_mem(void)
{
    _xmc_sc_data_od("initial stack pointer", (const void *)0x10001000, 4, 0);
    _xmc_sc_data_od("initial start address", (const void *)0x10001004, 4, 0);

    printf("\n");

    _xmc_sc_mem_region("[memory map]", ".text segment", &_sfixed, &_efixed);
    _xmc_sc_mem_region(NULL, ".etext segment", &_efixed, &_etext);
    _xmc_sc_mem_region(NULL, ".relocate segment", &_srelocate, &_erelocate);
    _xmc_sc_mem_region(NULL, ".bss segment", &_szero, &_ezero);
    _xmc_sc_mem_region(NULL, ".stack segment (ISR)", &_sstack, &_estack);
    _xmc_sc_mem_region(NULL, ".heap area", &_sheap, &_eheap);
    _xmc_sc_mem_region(NULL, "used heap (heap top)", &_sheap, heap_top);
    _xmc_sc_mem_region(NULL, "RAM", &_sram, &_eram);
}

void _xmc_sc_timer(void)
{
    printf("%-12s overhead (%u) + usleep_until_overhead (%u) <= backoff (%u)\n",
           "[xtimer]",
           XTIMER_OVERHEAD, XTIMER_USLEEP_UNTIL_OVERHEAD, XTIMER_BACKOFF);
    printf("%-12s xtimer_isr_backoff (%u)  ", "", XTIMER_ISR_BACKOFF);
    if (XTIMER_MASK) {
        printf("xtimer_mask (%x)\n", XTIMER_MASK);
    }
    else {
        printf("(no mask defined)\n");
    }
    printf("%-12s xtimer uses timer %u on channel %u\n", "", XTIMER, XTIMER_CHAN);

    printf("\n");

    uint32_t x_now = xtimer_now();
    uint32_t t_now = timer_read(0);

    printf("%-12s %"PRIu32"\n",
           "[xtimer_now]", x_now);

    printf("\n");

    printf("%-12s %-10"PRIu32"\n\n", "[TIMER 0]", t_now);

    printf("%-12s %-10s = %5u, %-10s = %5u  => %10u\n", "[CHANNEL 0]", "CC1->TIMER",
           (unsigned)CCU40_CC41->TIMER, "CC0->TIMER",
           (unsigned)CCU40_CC40->TIMER,
           (unsigned)CCU40_CC41->TIMER * 0xffff +
           (unsigned)CCU40_CC40->TIMER);

    printf("%-12s %-10s = %5u, %-10s = %5u  => %10u\n", "", "CC1->CR",
           (unsigned)CCU40_CC41->CR, "CC0->CR",
           (unsigned)CCU40_CC40->CR,
           (unsigned)CCU40_CC41->CR * 0xffff +
           (unsigned)CCU40_CC40->CR);

    printf("%-12s %-10s = %5u, %-10s = %5u  => %10u\n", "", "CC1->CRS",
           (unsigned)CCU40_CC41->CRS, "CC0->CRS",
           (unsigned)CCU40_CC40->CRS,
           (unsigned)CCU40_CC41->CRS * 0xffff +
           (unsigned)CCU40_CC40->CRS);

    printf("\n");

    printf("%-12s %-10s = %5u, %-10s = %5u  => %10u\n", "[CHANNEL 1]", "CC3->TIMER",
           (unsigned)CCU40_CC43->TIMER, "CC2->TIMER",
           (unsigned)CCU40_CC42->TIMER,
           (unsigned)CCU40_CC43->TIMER * 0xffff +
           (unsigned)CCU40_CC42->TIMER);

    printf("%-12s %-10s = %5u, %-10s = %5u  => %10u\n", "", "CC3->CR",
           (unsigned)CCU40_CC43->CR, "CC2->CR",
           (unsigned)CCU40_CC42->CR,
           (unsigned)CCU40_CC43->CR * 0xffff +
           (unsigned)CCU40_CC42->CR);

    printf("%-12s %-10s = %5u, %-10s = %5u  => %10u\n", "", "CC3->CRS",
           (unsigned)CCU40_CC43->CRS, "CC2->CRS",
           (unsigned)CCU40_CC42->CRS,
           (unsigned)CCU40_CC43->CRS * 0xffff +
           (unsigned)CCU40_CC42->CRS);
}

void _xmc_sc_bmi(void)
{
    uint16_t *bmi_ptr = (uint16_t *)0x10000e00;
    uint16_t *bmi_ptr_inv = (uint16_t *)0x10000e10;
    uint16_t bmi = *bmi_ptr;

    printf("[%p] %-28s 0x%0x\n", bmi_ptr, "Boot Mode Index (BMI)", (unsigned)bmi);
    printf("[%p] %-28s 0x%0x ", bmi_ptr + 8, "Inverse check (BMI)", *bmi_ptr_inv);

    if (((*bmi_ptr) | (*bmi_ptr_inv)) == 0xffff) {
        puts("(valid)");
    }
    else {
        puts("(invalid!)");
    }

    printf("%-12s %-28s ", "", " - Start-up mode selection");
    switch (bmi & 0x3f) {
        case 0:
            puts("ASC Bootstrap Loader mode (ASC_BSL)");
            break;
        case 1:
            puts("User productive Mode (UPM)");
            break;
        case 3:
            puts("User mode with debug enabled (UMD)");
            break;
        case 7:
            puts("User mode with debug enabled and HAR (UMHAR)");
            break;
        case 8:
            puts("SSC Bootstrap Loeader mode (SSC_BSL)");
            break;
        case 16:
            puts("ASC BSL mode with time-out (ASC_BSLTO)");
            break;
        case 24:
            puts("SSC BSL mode with time-out (SSC_BSLTO)");
            break;
        case 58:
            puts("Secure Bootstrap Loader mode (SBSL)");
            break;
        default:
            puts("Not defined");
            break;
    }

    bmi = bmi >> 8;

    printf("%-12s %-28s ", "", " - DAP Type Selection");

    if (bmi & 0x1) {
        puts("SPD");
    }
    else {
        puts("SWD");
    }

    bmi = bmi >> 1;

    printf("%-12s %-28s ", "", " - SWD/SPD Input/Output");

    if ((bmi & 0x3) == 0) {
        puts("SWD/SPD_0");
    }
    else if ((bmi & 0x3) == 1) {
        puts("SWD/SPD_1");
    }
    else {
        puts("Unknown");
    }

    bmi = bmi >> 3;

    printf("%-12s %-28s %u (%u MCLK cycles)\n", "", " - ASC BSL timeout (BSLTO)", bmi, bmi * 26664000);
}

void _xmc_sc_adc(void)
{
    adc_poweron(ADC_0);

    for (int i = 0; i < 100; i++) {
        printf("[%2i] %8i\n", i, adc_sample(ADC_0, 2));
    }

    adc_poweroff(ADC_0);
}


void _xmc_sc_dev(void)
{
    _xmc_sc_data_od("Chip ID (SCU_IDCHIP)", (const void *)0x10000f00, 4, 0);
    _xmc_sc_data_od("Chip Variant ID Number", (const void *)0x10000f04, 28, 0);
    _xmc_sc_data_od("Unique Chip ID", (const void *)0x10000ff0, 16, 0);
    _xmc_sc_data_od("ANA_TSE_1 / ANA_TSE_2", (const void *)0x10000f30, 2, OD_FLAGS_LENGTH_1);
    _xmc_sc_data_od("DCO_ADJLO_T1 / DCO_ADJLO_T2", (const void *)0x10000f32, 2, OD_FLAGS_LENGTH_1);

    printf("\n");
}

void _xmc_sc_gpios(char *cmd)
{
    const char *not_present = "not included in package";
    const char *not_connected = "no pin-out on board";
    const char *input_only = "input only / analog";

    bool all = false;

    if (cmd && (strncmp(cmd, "all", 3) == 0)) {
        all = true;
    }

    printf("  %s   %s        %s   %s  %s\n\n",
           "GPIO",
           "status",
           "setup",
           "resistor",
           "comment");

    print_gpio_status(P0,  0, true,  NULL);

    if (all) {
        print_gpio_status(P0,  1, false, not_present);
        print_gpio_status(P0,  2, false, not_present);
        print_gpio_status(P0,  3, false, not_present);
        print_gpio_status(P0,  4, false, not_present);
    }

    print_gpio_status(P0,  5, true,  NULL);
    print_gpio_status(P0,  6, true,  NULL);
    print_gpio_status(P0,  7, true,  NULL);
    print_gpio_status(P0,  8, true,  NULL);
    print_gpio_status(P0,  9, true,  NULL);

    if (all) {
        print_gpio_status(P0, 10, false, not_present);
        print_gpio_status(P0, 11, false, not_present);
        print_gpio_status(P0, 12, false, not_connected);
        print_gpio_status(P0, 13, false, not_connected);
    }

    print_gpio_status(P0, 14, true,  NULL);
    print_gpio_status(P0, 15, true,  NULL);

    print_gpio_status(P1,  0, true, "user led 1");
    print_gpio_status(P1,  1, true, "user led 2");
    print_gpio_status(P1,  2, true, "clock pin SWD");
    print_gpio_status(P1,  3, true, "data pin SWD/SPD");

    if (all) {
        print_gpio_status(P1,  4, false, not_present);
        print_gpio_status(P1,  5, false, not_present);
        print_gpio_status(P1,  6, false, not_present);
    }

    print_gpio_status(P2,  0, true,  NULL);
    print_gpio_status(P2,  1, true,  "uart tx");
    print_gpio_status(P2,  2, true,  "uart rx");

    if (all) {
        print_gpio_status(P2,  3, false, not_present);
        print_gpio_status(P2,  4, false, not_present);
        print_gpio_status(P2,  5, false, not_present);
    }

    print_gpio_status(P2,  6, true,  input_only);
    print_gpio_status(P2,  7, true,  input_only);
    print_gpio_status(P2,  9, true,  input_only);
    print_gpio_status(P2, 10, true,  NULL);
    print_gpio_status(P2, 11, true,  NULL);
}

void _xmc_sc_usage(void)
{
    puts("usage: xmc <command> [arguments]");
    puts("commands:");
    puts("\tgpio\t[all]\tshow GPIO status");
    puts("\tmem\t\tshow memory map");
    puts("\tbmi\t\tshow boot mode selection data");
    puts("\tdev\t\tshow device identification data");
    puts("\ttimer\t\tshow peripheral timer state");
    puts("\tclock\t\tshow clock configuration");
    puts("\tgating\t\tshow initial gating configuration");
    puts("\tadc\tsample 100 values from channel 2 (P2.9)");
}

int sc_xmc(int argc, char **argv)
{
    if (argc < 2) {
        _xmc_sc_usage();
        return 1;
    }
    else if (strncmp(argv[1], "gpio", 4) == 0) {
        _xmc_sc_gpios(argv[2]);
    }
    else if (strncmp(argv[1], "mem", 3) == 0) {
        _xmc_sc_mem();
    }
    else if (strncmp(argv[1], "dev", 3) == 0) {
        _xmc_sc_dev();
    }
    else if (strncmp(argv[1], "bmi", 3) == 0) {
        _xmc_sc_bmi();
    }
    else if (strncmp(argv[1], "clock", 5) == 0) {
        _xmc_sc_clock();
    }
    else if (strncmp(argv[1], "gating", 5) == 0) {
        _xmc_sc_gating();
    }
    else if (strncmp(argv[1], "adc", 3) == 0) {
        _xmc_sc_adc();
    }
    else if (strncmp(argv[1], "timer", 5) == 0) {
        _xmc_sc_timer();
    }
    else {
        printf("unknown command: %s\n", argv[1]);
        return 1;
    }
    return 0;
}
