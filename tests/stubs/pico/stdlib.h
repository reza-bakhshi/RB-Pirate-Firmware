/* Stub: pico/stdlib.h for host-side testing */
#ifndef _PICO_STDLIB_H
#define _PICO_STDLIB_H

#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>

#ifndef count_of
#define count_of(a) (sizeof(a) / sizeof((a)[0]))
#endif

static inline char* strlwr(char* str) {
    if (!str) return (void*)0;
    for (char* p = str; *p; p++) {
        *p = (char)tolower((unsigned char)*p);
    }
    return str;
}

#define busy_wait_us(x) ((void)0)
#define busy_wait_ms(x) ((void)0)
#define tight_loop_contents() ((void)0)

#define GPIO_FUNC_SIO 5
#define GPIO_FUNC_PWM 4
#define GPIO_OUT 1
#define GPIO_IN 0

typedef unsigned int uint;
typedef void (*gpio_irq_callback_t)(uint gpio, uint32_t event_mask);

#ifndef _BP_GPIO_GET_DEFINED
#define _BP_GPIO_GET_DEFINED
__attribute__((weak)) bool s_mock_gpio_fuse_tripped = false;
static inline bool gpio_get(uint g) {
    if (g == 17) {
        return !s_mock_gpio_fuse_tripped;
    }
    return true;
}
#endif
static inline void gpio_pull_up(uint g) { (void)g; }
static inline void gpio_disable_pulls(uint g) { (void)g; }
static inline void gpio_set_input_enabled(uint g, bool enabled) { (void)g; (void)enabled; }
static inline void gpio_put(uint g, bool val) { (void)g; (void)val; }
static inline void gpio_set_function(uint g, uint f) { (void)g; (void)f; }
static inline void gpio_set_dir(uint g, bool out) { (void)g; (void)out; }
static inline uint pwm_gpio_to_slice_num(uint g) { (void)g; return 0; }
static inline uint pwm_gpio_to_channel(uint g) { (void)g; return 0; }
static inline void pwm_set_clkdiv_int_frac(uint s, uint i, uint f) { (void)s; (void)i; (void)f; }
static inline void pwm_set_wrap(uint s, uint w) { (void)s; (void)w; }

unsigned int get_core_num(void);
void sleep_ms(uint32_t ms);
static inline void sleep_us(uint64_t us) { (void)us; }
static inline void busy_wait_us_32(uint32_t us) { (void)us; }
static inline uint32_t time_us_32(void) { return 0; }
typedef uint64_t absolute_time_t;

#ifdef BP_CUSTOM_TICK_MOCK
uint32_t get_host_tick_ms(void);
static inline absolute_time_t get_absolute_time(void) { return (absolute_time_t)get_host_tick_ms(); }
#else
static inline absolute_time_t get_absolute_time(void) { return 0; }
#endif

static inline absolute_time_t make_timeout_time_ms(uint32_t ms) { return (absolute_time_t)ms; }
static inline bool time_reached(absolute_time_t t) { (void)t; return true; }
static inline uint32_t to_ms_since_boot(absolute_time_t t) { return (uint32_t)t; }

#endif
