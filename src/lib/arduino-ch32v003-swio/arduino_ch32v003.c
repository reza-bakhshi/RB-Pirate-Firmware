#include <stdio.h>
#include "pico/stdlib.h"
#include <stdint.h>
#include "pirate.h"
#include "swio.h"
#include "pirate/bio.h"
#include "swio.h"
#include "pirate/psu.h"
#include "system_config.h"

#include "usb_rx.h"
#include "usb_tx.h"
#include "tusb.h"
#include "lib/picorvd/picoswio.h"
#include "services/bp_bin_transport.h"

void target_power(int x){
    if (x)
        psu_enable(5.0f,0.0f,true, 100);
    else
        psu_disable();
}

#define PROTOCOL_START     '!'
#define PROTOCOL_ACK       '+'
#define PROTOCOL_TEST      '?'
#define PROTOCOL_POWER_ON  'p'
#define PROTOCOL_POWER_OFF 'P'
#define PROTOCOL_WRITE_REG 'w'
#define PROTOCOL_READ_REG  'r'

const char arduino_ch32v003_name[]="Arduino CH32V003 SWIO";

void arduino_ch32v003_cleanup(void){
    ch32vswio_cleanup();
}

void arduino_ch32v003(void) {
    uint8_t reg;
    uint32_t val = 0;

    if (!bp_bin_transport_is_connected()) return;  

    uint8_t in_c;
    if (bp_bin_transport_getc(&in_c) == BP_STATUS_OK) { //co-op multitask
        char c = (char)in_c;
        switch (c) {
            case PROTOCOL_TEST:
                ch32vswio_reset(bio2bufiopin[BIO0], bio2bufdirpin[BIO0]);
                bp_bin_transport_putc(PROTOCOL_ACK);
                bp_bin_transport_tx_flush();
                break;
            case PROTOCOL_POWER_ON:
                target_power(1);
                bp_bin_transport_putc(PROTOCOL_ACK);
                bp_bin_transport_tx_flush();
                break;
            case PROTOCOL_POWER_OFF:
                target_power(0);
                bp_bin_transport_putc(PROTOCOL_ACK);
                bp_bin_transport_tx_flush();
                break;
            case PROTOCOL_WRITE_REG:
                if (bp_bin_transport_getc_timeout(&reg, 1000) != BP_STATUS_OK) {
                    return;
                }
                for (uint8_t i = 0; i < 4; i++) {
                    val = (val >> 8);
                    if (bp_bin_transport_getc_timeout(&in_c, 1000) != BP_STATUS_OK) {
                        return;
                    }
                    val |= ((uint32_t)in_c << 24);
                }
                ch32vswio_put(reg, val);
                bp_bin_transport_putc(PROTOCOL_ACK);
                bp_bin_transport_tx_flush();
                break;
            case PROTOCOL_READ_REG:
                if (bp_bin_transport_getc_timeout(&reg, 1000) != BP_STATUS_OK) {
                    return;
                }
                val = ch32vswio_get(reg);
                for (uint8_t i = 0; i < 4; i++) {
                    uint8_t out_b = (uint8_t)(val & 0xff);
                    bp_bin_transport_putc(out_b);
                    val = val >> 8;
                }
                bp_bin_transport_tx_flush();
                break;
        }
    }
}
