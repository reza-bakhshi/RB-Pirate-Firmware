#include <pico/stdlib.h>
#include <string.h>
#include "hardware/clocks.h"
#include "pirate.h"

#include "usb_rx.h"
#include "usb_tx.h"
#include "system_config.h"
// #include "bytecode.h" //needed because modes.h has some functions that use it TODO: move all the opt args and
// bytecode stuff to a single helper file #include "command_struct.h" //needed for same reason as bytecode and needs same fix
// #include "modes.h"
#include "binmode/binmodes.h"
#include "binmode/logicanalyzer.h"
#include "binmode/fala.h"
#include "tusb.h"
#include "ui/ui_term.h"
#include "services/bp_bin_transport.h"

#define MAX_UART_PKT 64
#define CDC_INTF 1

// binmode name to display
const char falaio_name[] = "Follow along logic analyzer";

// send notification packet at end of capture
void falaio_notify(void) {
    // get samples count
    uint32_t fala_samples = logic_analyzer_get_samples_from_zero();
    if(fala_samples > (LA_BUFFER_SIZE)) { //invalid sample count
        fala_samples = 0;
    }
    // send notification packet
    //$FALADATA;{pins};{trigger pins};{trigger mask};{edge trigger (bool)}; {capture speed in hz};{samples};{pre-samples
    //(for trigger line)};
    if (bp_bin_transport_is_connected()) {
        uint8_t buf[MAX_UART_PKT];
        uint8_t len = snprintf((char*)buf,
                               sizeof(buf),
                               "$FALADATA;%d;%d;%d;%c;%d;%d;%d;\n",
                               8,
                               0,
                               0,
                               'N',
                               fala_config.actual_sample_frequency,
                               fala_samples,
                               0);
        if (bp_bin_transport_tx_free() >= len) {
            bp_bin_transport_write(buf, len, NULL);
            bp_bin_transport_tx_flush();
        }
    }
}

// binmode setup on mode start
void falaio_setup(void) {
    if (!fala_notify_register(&falaio_notify)) {
        printf("Failed to register notify function\r\n");
        return;
    }

    system_config.binmode_usb_rx_queue_enable = false;
    system_config.binmode_usb_tx_queue_enable = false;

    //show the notification at setup
    //fala_mode_change_hook();
}

void falaio_setup_message(void){
#if ! BP_HW_FALA_BUFFER
    printf("%sWarning: What you see may not be what you get!%s\r\n", ui_term_color_error(), ui_term_color_reset());
    printf("%sThis hardware version captures samples from behind the IO buffer.%s\r\n",
            ui_term_color_info(),
            ui_term_color_reset());
    printf("%sWhen the buffers are outputs, the samples show the RP2040/RP2350 pin states.%s\r\n",
            ui_term_color_info(),
            ui_term_color_reset());
    printf("%sThis may not match the buffer output and could make debugging difficult.%s\r\n",
            ui_term_color_info(),
            ui_term_color_reset());
    printf("%sThis does not apply when the buffers are inputs or in HiZ mode.%s\r\n",
            ui_term_color_info(),
            ui_term_color_reset());
    printf("%sPlease keep this in mind when debugging.%s\r\n\r\n", ui_term_color_info(), ui_term_color_reset());
    // printf("%sContinue?%s", ui_term_color_error(), ui_term_color_reset());
    // if (!ui_yes_no()) {
    //     return false;
    // }
#endif
    fala_mode_change_hook();
}

// binmode cleanup on exit
void falaio_cleanup(void) {
    fala_notify_unregister(&falaio_notify);
    system_config.binmode_usb_rx_queue_enable = true;
    system_config.binmode_usb_tx_queue_enable = true;
}

static uint32_t fala_dump_count;

static uint falaio_tx8(uint8_t* buf, uint len) {
    uint32_t i, count;
    count = fala_dump_count;
    for (i = 0; i < len && count > 0; i++, count--) {
        logic_analyzer_dump(&buf[i]);
    }
    fala_dump_count -= i;
    return i;
}

enum fala_statemachine {
    FALA_IDLE = 0,
    FALA_DUMP
};

void falaio_service(void) {
    static enum fala_statemachine state = FALA_IDLE;

    if (!bp_bin_transport_is_connected()) {
        return;
    }

    switch (state) {
        case FALA_IDLE: {
            if (bp_bin_transport_rx_available() == 0) {
                break;
            }

            uint8_t buf[64];
            uint32_t len = 0;
            if (bp_bin_transport_read(buf, sizeof(buf), &len) == BP_STATUS_OK && len > 0) {
                for (uint32_t i = 0; i < len; i++) {
                    switch (buf[i]) {
                        case '?':
                            falaio_notify();
                            break;
                        case '+':
                            // dump the buffer
                            logic_analyzer_reset_ptr(); // put pointer back to end of data buffer (last sample first)
                            fala_dump_count = logic_analyzer_get_samples_from_zero();
                            state = FALA_DUMP;
                            break;
                    }
                }
            }
            break;
        }
        case FALA_DUMP: {
            uint8_t buf[64];
            if (bp_bin_transport_tx_free() >= sizeof(buf)) {
                uint8_t len = falaio_tx8(buf, sizeof(buf));
                bp_bin_transport_write(buf, len, NULL);
                bp_bin_transport_tx_flush();
            }
            if (fala_dump_count == 0) {
                state = FALA_IDLE;
            }
            break;
        }
    }
}
