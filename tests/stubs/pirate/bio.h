#ifndef _PIRATE_BIO_H
#define _PIRATE_BIO_H

#include <stdint.h>
#include <stdbool.h>

#ifndef BP_HARDWARE_VERSION
#include "platform/rb-pirate.h"
#endif

#define GPIO_FUNC_UART 2
#define GPIO_FUNC_SIO  5
#define GPIO_OVERRIDE_NORMAL 0

extern const uint8_t bio2bufiopin[8];

void bio_init(void);
static inline void bio_buf_output(uint8_t bio) { (void)bio; }
static inline void bio_buf_input(uint8_t bio) { (void)bio; }
static inline void bio_set_function(uint8_t bio, uint8_t function) { (void)bio; (void)function; }
static inline void bio_output(uint8_t bio) { (void)bio; }
static inline void bio_input(uint8_t bio) { (void)bio; }
static inline void bio_put(uint8_t bio, bool val) { (void)bio; (void)val; }
static inline bool bio_get(uint8_t bio) { (void)bio; return false; }
static inline void gpio_set_outover(unsigned int pin, unsigned int val) { (void)pin; (void)val; }
static inline void gpio_set_inover(unsigned int pin, unsigned int val) { (void)pin; (void)val; }
#ifndef _BP_GPIO_GET_DEFINED
#define _BP_GPIO_GET_DEFINED
static inline bool gpio_get(unsigned int pin) { (void)pin; return true; }
#endif

#endif
