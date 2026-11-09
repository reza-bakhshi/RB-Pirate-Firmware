/**
 * @file hardfault.c
 * @brief HardFault crash handler with register capture and controlled reboot.
 * @details Overrides the weak isr_hardfault from pico_crt0.
 *          On crash, captures ARM Cortex-M33 stacked and SCB registers into
 *          persistent RAM (bp_crash_record), blinks RGB LEDs red briefly,
 *          and initiates a controlled watchdog reboot.
 */

#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/watchdog.h"
#include "hardware/structs/pio.h"
#include "pico/multicore.h"
#include "pirate.h"
#include "pirate/crash_record.h"

/* WS2812 GRB pixel format, shifted left 8 bits for PIO */
#define FAULT_COLOR_RED   ((uint32_t)0x00FF0000u)  /* G=0x00, R=0xFF, B=0x00 */
#define FAULT_COLOR_BLACK ((uint32_t)0x00000000u)

/* System Control Block fault registers (Cortex-M33) */
#define SCB_CFSR  (*(volatile uint32_t*)0xE000ED28)
#define SCB_HFSR  (*(volatile uint32_t*)0xE000ED2C)
#define SCB_MMFAR (*(volatile uint32_t*)0xE000ED34)
#define SCB_BFAR  (*(volatile uint32_t*)0xE000ED38)

static void __attribute__((noinline)) fault_delay(volatile uint32_t cycles) {
    while (cycles--) {
        __asm volatile("" ::: "memory");
    }
}

static void fault_push_pixels(pio_hw_t *pio, uint sm, uint32_t color, uint count) {
    for (uint i = 0; i < count; i++) {
        while (pio->fstat & (1u << (PIO_FSTAT_TXFULL_LSB + sm))) {
            /* tight loop */
        }
        pio->txf[sm] = color;
    }
}

void __attribute__((noreturn, used)) bp_hardfault_handler_c(uint32_t *stack_args) {
    /* Abort any in-flight RGB DMA to avoid bus contention */
    for (uint ch = 0; ch < NUM_DMA_CHANNELS; ch++) {
        dma_hw->abort = (1u << ch);
    }
    while (dma_hw->abort) {
        /* wait for abort */
    }

    bp_crash_registers_t regs = {
        .r0 = stack_args[0],
        .r1 = stack_args[1],
        .r2 = stack_args[2],
        .r3 = stack_args[3],
        .r12 = stack_args[4],
        .lr = stack_args[5],
        .pc = stack_args[6],
        .xpsr = stack_args[7],
        .cfsr = SCB_CFSR,
        .hfsr = SCB_HFSR,
        .mmfar = SCB_MMFAR,
        .bfar = SCB_BFAR,
        .sp = (uint32_t)stack_args,
    };

    bp_crash_record_capture(BP_CRASH_REASON_HARDFAULT, get_core_num(), NULL, 0, "HardFault Exception", &regs);

    /* Visual indication: blink LEDs red 3 times (~300ms total) */
    pio_hw_t *pio = PIO_RGB_LED_PIO;
    uint sm = PIO_RGB_LED_SM;
    uint led_count = RGB_LEN;

    for (int i = 0; i < 3; i++) {
        fault_push_pixels(pio, sm, FAULT_COLOR_RED, led_count);
        fault_delay(2000000u);
        fault_push_pixels(pio, sm, FAULT_COLOR_BLACK, led_count);
        fault_delay(2000000u);
    }

    /* Controlled reset via hardware watchdog */
    watchdog_reboot(0, 0, 10);

    for (;;) {
        __asm volatile("wfe");
    }
}

void __attribute__((naked)) isr_hardfault(void) {
    __asm volatile(
        "tst lr, #4\n"
        "ite eq\n"
        "mrseq r0, msp\n"
        "mrsne r0, psp\n"
        "ldr r1, =bp_hardfault_handler_c\n"
        "bx r1\n"
    );
}
