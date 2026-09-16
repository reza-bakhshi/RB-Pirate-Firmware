/* SubGHz IRQ capture implementation (RP2040/RP2350, core 0).
 * Copyright (c) 2026 Bus Pirate contributors. SPDX-License-Identifier: MIT
 */
#include "mode/subghz_cap.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/sync.h"
#include "pirate/bio.h"

#define SUBGHZ_CAP_DEPTH 2048u

static volatile uint32_t cap_stamps[SUBGHZ_CAP_DEPTH];
static volatile uint32_t cap_head;
static volatile uint32_t cap_tail;
static volatile uint32_t cap_overruns;
static volatile bool cap_active;
static volatile bool cap_start_level;
static volatile uint32_t cap_intervals;
static uint8_t cap_gpio = 0xff;

/* BIO to RP GPIO mapping lives in bio.c; GDO pins are plain GPIOs. */
extern const uint8_t bio2bufiopin[];

static void subghz_cap_handler(void) {
    uint32_t gpio = cap_gpio;
    if (gpio == 0xff) {
        return;
    }
    uint32_t events =
        gpio_get_irq_event_mask(gpio) & (GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL);
    if (!events) {
        return;
    }
    gpio_acknowledge_irq(gpio, events);
    if (!cap_active) {
        return;
    }
    uint32_t next = (cap_head + 1u) % SUBGHZ_CAP_DEPTH;
    if (next == cap_tail) {
        cap_overruns++;
        /* Overwrite the oldest edge to keep the newest timing. */
        cap_tail = (cap_tail + 1u) % SUBGHZ_CAP_DEPTH;
    }
    cap_stamps[cap_head] = time_us_32();
    cap_head = next;
}

void subghz_cap_start(uint8_t bio_pin) {
    subghz_cap_stop();
    if (bio_pin > 3u) {
        return;
    }
    bio_input(bio_pin);
    uint8_t gpio = bio2bufiopin[bio_pin];
    uint32_t irq_state = save_and_disable_interrupts();
    cap_gpio = gpio;
    cap_head = 0;
    cap_tail = 0;
    cap_overruns = 0;
    cap_intervals = 0;
    cap_start_level = gpio_get(gpio) != 0;
    restore_interrupts(irq_state);
    cap_active = true;
    gpio_add_raw_irq_handler_masked(1u << gpio, subghz_cap_handler);
    irq_set_enabled(IO_IRQ_BANK0, true);
    gpio_set_irq_enabled(gpio, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true);
}

void subghz_cap_stop(void) {
    if (cap_gpio != 0xff) {
        gpio_set_irq_enabled(cap_gpio, GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, false);
        gpio_remove_raw_irq_handler_masked(1u << cap_gpio, subghz_cap_handler);
        cap_gpio = 0xff;
    }
    cap_active = false;
}

bool subghz_cap_poll(bool *level, uint32_t *duration_us) {
    if (!level || !duration_us) {
        return false;
    }
    uint32_t irq_state = save_and_disable_interrupts();
    uint32_t tail = cap_tail;
    uint32_t head = cap_head;
    if (tail == head) {
        restore_interrupts(irq_state);
        return false; /* Empty. */
    }
    uint32_t t0 = cap_stamps[tail];
    uint32_t t1 = cap_stamps[(tail + 1u) % SUBGHZ_CAP_DEPTH];
    /* Need two edges to form one interval; the newest edge may still be
     * open (its pulse ongoing), so require a following edge. */
    uint32_t used = (head >= tail) ? head - tail : SUBGHZ_CAP_DEPTH - tail + head;
    if (used < 2u) {
        restore_interrupts(irq_state);
        return false;
    }
    cap_tail = (tail + 1u) % SUBGHZ_CAP_DEPTH;
    *duration_us = t1 - t0; /* Unsigned: wraps safely. */
    *level = (cap_intervals & 1u) ? !cap_start_level : (bool)cap_start_level;
    cap_intervals++;
    restore_interrupts(irq_state);
    return true;
}

uint32_t subghz_cap_overruns(void) {
    uint32_t irq_state = save_and_disable_interrupts();
    uint32_t n = cap_overruns;
    cap_overruns = 0;
    restore_interrupts(irq_state);
    return n;
}

uint32_t subghz_cap_pending(void) {
    uint32_t irq_state = save_and_disable_interrupts();
    uint32_t head = cap_head, tail = cap_tail;
    restore_interrupts(irq_state);
    return (head >= tail) ? head - tail : SUBGHZ_CAP_DEPTH - tail + head;
}
