// How modes work:
// Bus Pirate 5 uses a three step process to get tight timings between operations.
// This means it is no longer possible to just spit out data from printf directly from the mode.
// All messages and data must be handed back in the result struct to be shown later.
// Eventually I will make syntax processing optional for modes so you can opt for a more relaxed environment (ask me, I
// wont make it a priority until someone actually wants it)
// 1. The syntax system pre-processes the user input into a simple bytecode
// 2. A loop hands each user command to a mode function below for actual IO or other actions
// 3. A final loop post-processes the result and outputs to the user terminal
// To enable dummy mode open pirate.h and uncomment "#define BP_USE_DUMMY1"
// The dummy functions are implemented in mode.c. To create a new mode make a copy of the dummy portion of the mode
// struck and link to your new functions
#include <stdio.h>
#include "pico/stdlib.h"
#include "pirate.h"
#include "system_config.h"
#include "command_struct.h"
#include "bytecode.h"   // Bytecode structure for data IO
#include "pirate/bio.h" // Buffered pin IO functions
#include "ui/ui_help.h"
#include "infrared-struct.h"
#include "lib/pico_ir_nec/nec_transmit.h"
#include "lib/pico_ir_nec/nec_receive.h"
#include "mode/infrared.h"
#include "pirate/storage.h"
#include "ui/ui_term.h"
#include "pirate/rc5_pio.h"
#include "pirate/ir_protocols.h"
#include "commands/infrared/tvbgone.h"
#include "pirate/irio_pio.h"
#include "commands/infrared/irtxrx.h"
#include "commands/infrared/ir_send.h"
#include "commands/infrared/ir_receive.h"
#include "commands/infrared/ir_setprotocol.h"
#include "commands/infrared/ir_devicebgone.h"
#include "commands/infrared/ir_remote.h"
#include "commands/infrared/ir_record.h"
#include "commands/infrared/ir_load.h"
#include "commands/infrared/ir_replay.h"
#include "commands/infrared/ir_jam.h"
#include "commands/infrared/ir_config.h"
#include "commands/infrared/ir_common.h"
#include "lib/bp_args/bp_cmd.h"

static const char* const irtoy_test_usage[] = {
    "test",
    "Test IR Toy v3 hardware:%s test",
};

const bp_command_def_t irtoy_test_def = {
    .name         = "test",
    .description  = T_IR_CMD_TEST,
    .actions      = NULL,
    .action_count = 0,
    .opts         = NULL,
    .usage        = irtoy_test_usage,
    .usage_count  = count_of(irtoy_test_usage),
};

static struct _infrared_mode_config mode_config;
static uint8_t device_cleanup;

// an array of all the IR protocol functions
typedef struct _ir_protocols {
    int (*irtx_init)(uint pin_num, uint32_t mod_freq); // init    
    void (*irtx_deinit)(uint pin_num);   
    void (*irtx_write)(uint32_t *data);     // write
    bool (*irtx_wait_idle)(void);
    int (*irrx_init)(uint pin_num);
    void (*irrx_deinit)(uint pin_num); 
    ir_rx_status_t (*irrx_read)(uint32_t *rx_frame);      // read
    void (*irrx_drain_fifo)(void);
    uint32_t mod_freq;
    char display_name[4];
    uint8_t num_bits;
} ir_protocols;
static const ir_protocols ir_protocol[] = {
    {   .irtx_init = irio_pio_tx_init, 
        .irtx_deinit = irio_pio_tx_deinit,
        .irtx_write = irio_pio_tx_write,
        .irtx_wait_idle = irio_pio_tx_wait_idle,
        .irrx_init = irio_pio_rx_init,
        .irrx_deinit = irio_pio_rx_deinit,
        .irrx_read = irio_pio_rx_frame_printf, 
        .irrx_drain_fifo = irio_pio_rxtx_drain_fifo, 
        .mod_freq = 0,
        .display_name = "RAW",
        .num_bits = 32
    },
    {   .irtx_init = nec_tx_init, 
        .irtx_deinit = nec_tx_deinit,
        .irtx_write = nec_write,
        .irtx_wait_idle = nec_tx_wait_idle,
        .irrx_init = nec_rx_init,
        .irrx_deinit = nec_rx_deinit,
        .irrx_read = nec_get_frame,
        .irrx_drain_fifo = nec_rx_drain_fifo ,
        .mod_freq = 38000,
        .display_name = "NEC",
        .num_bits = 16
    },
    {   .irtx_init = rc5_tx_init,
        .irtx_deinit = rc5_tx_deinit,
        .irtx_write = rc5_send,
        .irtx_wait_idle = rc5_tx_wait_idle,
        .irrx_init = rc5_rx_init,
        .irrx_deinit = rc5_rx_deinit,
        .irrx_read = rc5_receive, 
        .irrx_drain_fifo = rc5_drain_fifo,
        .mod_freq = 36000,
        .display_name = "RC5",
        .num_bits = 16
    }
};  

bool irtoy_test_pullup(int bio) {
    if (bio_get(bio)) {
        printf("OK\r\n");
        return 0;
    } else {
        printf("FAIL\r\n");
        return 1;
    }
}

bool irtoy_test_rx(int bio) {
    // each protocol has its own write function
    uint32_t tx_frame = 0xffff;
    ir_protocol[mode_config.protocol].irtx_write(&tx_frame);
    //wait for idle
    ir_protocol[mode_config.protocol].irtx_wait_idle();
    uint32_t timeout = 0;
    bool fail = 0;
    while (true) {
        //wait for valid data
        if (ir_protocol[mode_config.protocol].irrx_read(&tx_frame) == IR_RX_FRAME_OK) {
            printf("\r\nOK\r\n");
            break;
        }
        timeout++;
        if (timeout > 10000) {
            printf("FAIL\r\n");
            fail = 1;
            break;
        }
    }
    return fail;
}
// irtoy_test function
void irtoy_test(struct command_result* res) {
    if (bp_cmd_help_check(&irtoy_test_def, res->help_flag)) {
        return;
    }

    uint8_t fails = 0;
    printf("Test pull-ups\r\n");
    printf("BIO1 / 20-60kHz learner: ");
    fails += irtoy_test_pullup(BIO1);
    printf("BIO3 / 38kHz barrier: ");
    fails += irtoy_test_pullup(BIO3);
    printf("BIO5 / 38kHz demodulator: ");
    fails += irtoy_test_pullup(BIO5);
    printf("BIO7 / 56kHz modulator: ");
    fails += irtoy_test_pullup(BIO7);

    printf("Test RX\r\n");
    printf("BIO1 / 20-60kHz learner: ");
    fails += irtoy_test_rx(BIO1);
    printf("BIO3 / 38kHz barrier: ");
    fails += irtoy_test_rx(BIO3);
    printf("BIO5 / 38kHz demodulator: ");
    fails += irtoy_test_rx(BIO5);
    printf("BIO7 / 56kHz modulator: ");
    fails += irtoy_test_rx(BIO7);

    if (fails) {
        printf("\r\n%d FAILS :(\r\n", fails);
    } else {
        printf("\r\nOK :)\r\n");
    }
}

// command configuration
const struct _mode_command_struct infrared_commands[] = {
    {   .func=&irtoy_test,
        .def=&irtoy_test_def,
        .supress_fala_capture=false
    },
    {   .func=&tvbgone_player,
        .def=&tvbgone_def,
        .supress_fala_capture=true
    },
    {
        .func=&irtx_handler,
        .def=&irtx_def,
        .supress_fala_capture=false
    },
    {
        .func=&irrx_handler,
        .def=&irrx_def,
        .supress_fala_capture=false
    },
    {
        .func=&ir_send_handler,
        .def=&ir_send_def,
        .supress_fala_capture=false
    },
    {
        .func=&ir_receive_handler,
        .def=&ir_receive_def,
        .supress_fala_capture=false
    },
    {
        .func=&ir_setprotocol_handler,
        .def=&ir_setprotocol_def,
        .supress_fala_capture=false
    },
    {
        .func=&ir_devicebgone_handler,
        .def=&ir_devicebgone_def,
        .supress_fala_capture=true
    },
    {
        .func=&ir_remote_handler,
        .def=&ir_remote_def,
        .supress_fala_capture=true
    },
    {
        .func=&ir_record_handler,
        .def=&ir_record_def,
        .supress_fala_capture=false
    },
    {
        .func=&ir_load_handler,
        .def=&ir_load_def,
        .supress_fala_capture=false
    },
    {
        .func=&ir_replay_handler,
        .def=&ir_replay_def,
        .supress_fala_capture=false
    },
    {
        .func=&ir_jam_handler,
        .def=&ir_jam_def,
        .supress_fala_capture=true
    },
    {
        .func=&ir_config_handler,
        .def=&ir_config_def,
        .supress_fala_capture=false
    }
};
const uint32_t infrared_commands_count = count_of(infrared_commands);

// Pin labels shown on the display and in the terminal status bar
// No more than 4 characters long
static const char pin_labels[][5]={
    "LERN",
    "BARR",
    "IRTX",
    "38k",
    "56k",
    "IRRX"
};

static const uint8_t ir_rx_pins[] = {BIO3, BIO5, BIO7};

static const char infrared_config_file[] = "bpirrxtx.bp";

// mode configuration descriptors for load/save
static const mode_config_t infrared_config[] = {
    // clang-format off
    { "$.protocol", &mode_config.protocol, MODE_CONFIG_FORMAT_DECIMAL },
    { "$.sensor", &mode_config.rx_sensor, MODE_CONFIG_FORMAT_DECIMAL },
    { "$.freq", &mode_config.tx_freq, MODE_CONFIG_FORMAT_DECIMAL },
    { "$.txpin", &mode_config.tx_pin, MODE_CONFIG_FORMAT_DECIMAL },
    { "$.rxpin", &mode_config.rx_pin, MODE_CONFIG_FORMAT_DECIMAL },
    { "$.engine", &mode_config.engine_protocol, MODE_CONFIG_FORMAT_DECIMAL },
    // clang-format on
};

// save the mode configuration to flash
static void infrared_save_config(void) {
    storage_save_mode(infrared_config_file, infrared_config, count_of(infrared_config));
}

// Protocol — flag -p / --protocol
static const bp_val_choice_t infrared_protocol_choices[] = {
    { "raw", NULL, T_IR_PROTOCOL_MENU_RAW, 0 },
    { "nec", NULL, T_IR_PROTOCOL_MENU_NEC, 1 },
    { "rc5", NULL, T_IR_PROTOCOL_MENU_RC5, 2 },
};
static const bp_val_constraint_t infrared_protocol_choice = {
    .type = BP_VAL_CHOICE,
    .choice = { .choices = infrared_protocol_choices, .count = 3, .def = 0 },
    .prompt = T_IR_PROTOCOL_MENU,
};

// TX frequency — flag -f / --freq (kHz; stored as Hz)
static const bp_val_constraint_t infrared_freq_range = {
    .type = BP_VAL_UINT32,
    .u = { .min = 20, .max = 60, .def = 38 },
    .prompt = T_IR_TX_SPEED_MENU,
    .hint = T_IR_TX_SPEED_MENU_1,
};

// RX sensor — flag -r / --rxsensor
static const bp_val_choice_t infrared_rxsensor_choices[] = {
    { "barrier",   NULL, T_IR_RX_SENSOR_MENU_BARRIER,    0 },
    { "38k_demod", NULL, T_IR_RX_SENSOR_MENU_38K_DEMOD,  1 },
    { "56k_demod", NULL, T_IR_RX_SENSOR_MENU_56K_DEMOD,  2 },
};
static const bp_val_constraint_t infrared_rxsensor_choice = {
    .type = BP_VAL_CHOICE,
    .choice = { .choices = infrared_rxsensor_choices, .count = 3, .def = 1 },
    .prompt = T_IR_RX_SENSOR_MENU,
};

static const bp_command_opt_t infrared_setup_opts[] = {
    { "protocol", 'p', BP_ARG_REQUIRED, "raw|nec|rc5",             0, &infrared_protocol_choice },
    { "freq",     'f', BP_ARG_REQUIRED, "20-60",                   0, &infrared_freq_range },
    { "rxsensor", 'r', BP_ARG_REQUIRED, "barrier|38k_demod|56k_demod", 0, &infrared_rxsensor_choice },
    { 0 },
};

const bp_command_def_t infrared_setup_def = {
    .name = "infrared",
    .description = 0,
    .opts = infrared_setup_opts,
};

// Pre-setup step. Show user menus for any configuration options.
// The Bus Pirate hardware is not "clean" and reset at this point.
// Any previous mode may still be running. This is only a configuration step,
// the user may cancel out of the menu and return to the previous mode.
// Don't touch hardware yet, save the settings in variables for later.
uint32_t infrared_setup(void) {
    uint32_t temp;

    // defaults for settings that may be missing in older configuration files
    mode_config.tx_pin = BIO4;
    mode_config.rx_pin = BIO5;
    mode_config.engine_protocol = 0; // NEC

    // Detect interactive vs CLI mode by checking the primary flag
    bp_cmd_status_t st = bp_cmd_flag(&infrared_setup_def, 'p', &temp);
    if (st == BP_CMD_INVALID) return 0;
    bool interactive = (st == BP_CMD_MISSING);

    if (interactive) {
        if (storage_load_mode(infrared_config_file, infrared_config, count_of(infrared_config))) {
            printf("\r\n\r\n%s%s%s\r\n", ui_term_color_info(), GET_T(T_USE_PREVIOUS_SETTINGS), ui_term_color_reset());
            infrared_settings();
            int r = bp_cmd_yes_no_exit("");
            if (r == BP_YN_EXIT) return 0; // exit
            if (r == BP_YN_YES)  return 1; // use saved settings
        }

        if (bp_cmd_prompt(&infrared_protocol_choice, &temp) != BP_CMD_OK) return 0;
        mode_config.protocol = temp;

        if (ir_protocol[mode_config.protocol].mod_freq) {
            mode_config.tx_freq = ir_protocol[mode_config.protocol].mod_freq;
            printf("\r\n\r\n%s%s TX modulation%s: %ukHz\r\n", ui_term_color_info(), ir_protocol[mode_config.protocol].display_name, ui_term_color_reset(), mode_config.tx_freq / 1000);
        } else {
            if (bp_cmd_prompt(&infrared_freq_range, &temp) != BP_CMD_OK) return 0;
            mode_config.tx_freq = temp * 1000;
        }

        if (bp_cmd_prompt(&infrared_rxsensor_choice, &temp) != BP_CMD_OK) return 0;
        mode_config.rx_sensor = temp;
    } else {
        mode_config.protocol = temp;

        if (ir_protocol[mode_config.protocol].mod_freq) {
            mode_config.tx_freq = ir_protocol[mode_config.protocol].mod_freq;
        } else {
            st = bp_cmd_flag(&infrared_setup_def, 'f', &temp);
            if (st == BP_CMD_INVALID) return 0;
            mode_config.tx_freq = temp * 1000;
        }

        st = bp_cmd_flag(&infrared_setup_def, 'r', &temp);
        if (st == BP_CMD_INVALID) return 0;
        mode_config.rx_sensor = temp;
    }

    // resolve the RX preset to the actual RX pin
    mode_config.rx_pin = ir_rx_pins[mode_config.rx_sensor];

    infrared_save_config();

    return 1;
}

// Setup execution. This is where we actually configure any hardware.
uint32_t infrared_setup_exc(void) {
    bio_input(BIO1); // 20-60kHz learner
    bio_input(BIO3); // 38kHz barrier
    bio_input(BIO5); // 36-40kHz demodulator
    bio_input(BIO7); // 56kHz modulator
    bio_input(mode_config.rx_pin); // configured RX sensor
    bio_buf_output(mode_config.tx_pin); // IR TX

    //claim and label pins
    system_bio_update_purpose_and_label(true, BIO1, BP_PIN_IO, pin_labels[0]);
    system_bio_update_purpose_and_label(true, BIO3, BP_PIN_IO, pin_labels[1]);
    system_bio_update_purpose_and_label(true, mode_config.tx_pin, BP_PIN_IO, pin_labels[2]);
    system_bio_update_purpose_and_label(true, BIO5, BP_PIN_IO, pin_labels[3]);
    system_bio_update_purpose_and_label(true, BIO7, BP_PIN_IO, pin_labels[4]);
    if (mode_config.rx_pin != BIO1 && mode_config.rx_pin != BIO3 && mode_config.rx_pin != BIO5 && mode_config.rx_pin != BIO7) {
        system_bio_update_purpose_and_label(true, mode_config.rx_pin, BP_PIN_IO, pin_labels[5]);
    }

    // configure and enable the state machines
    int status = ir_protocol[mode_config.protocol].irtx_init(bio2bufiopin[mode_config.tx_pin], mode_config.tx_freq); // uses two state machines, 16 instructions and one IRQ
    if (status < 0) {
        printf("Failed to initialize TX PIO\r\n");
    }
    status = ir_protocol[mode_config.protocol].irrx_init(bio2bufiopin[mode_config.rx_pin]);
    if (status < 0) {
        printf("Failed to initialize RX PIO\r\n");
    }
    device_cleanup = mode_config.protocol;
    system_config.subprotocol_name = ir_protocol[mode_config.protocol].display_name;
    system_config.num_bits=ir_protocol[mode_config.protocol].num_bits;
    return 1;
}

bool infrared_preflight_sanity_check(void){
    return ui_help_sanity_check(true, 0);
}

void infrared_setup_resume(void){
    bio_input(mode_config.rx_pin); // configured RX sensor
    bio_buf_output(mode_config.tx_pin); // IR TX
    // configure and enable the state machines
    int status = ir_protocol[mode_config.protocol].irtx_init(bio2bufiopin[mode_config.tx_pin], mode_config.tx_freq); // uses two state machines, 16 instructions and one IRQ
    if (status < 0) {
        printf("Failed to initialize TX PIO\r\n");
    }
    status = ir_protocol[mode_config.protocol].irrx_init(bio2bufiopin[mode_config.rx_pin]);
    if (status < 0) {
        printf("Failed to initialize RX PIO\r\n");
    }
}

void infrared_cleanup_temp(void) {
    ir_protocol[device_cleanup].irtx_deinit(bio2bufiopin[mode_config.tx_pin]);
    ir_protocol[device_cleanup].irrx_deinit(bio2bufiopin[mode_config.rx_pin]);
}

// Cleanup any configuration on exit.
void infrared_cleanup(void) {
    ir_protocol[device_cleanup].irtx_deinit(bio2bufiopin[mode_config.tx_pin]);
    ir_protocol[device_cleanup].irrx_deinit(bio2bufiopin[mode_config.rx_pin]);
    // unclaim pins
    system_bio_update_purpose_and_label(false, BIO1, BP_PIN_IO, pin_labels[0]);
    system_bio_update_purpose_and_label(false, BIO3, BP_PIN_IO, pin_labels[1]);
    system_bio_update_purpose_and_label(false, mode_config.tx_pin, BP_PIN_IO, pin_labels[2]);
    system_bio_update_purpose_and_label(false, BIO5, BP_PIN_IO, pin_labels[3]);
    system_bio_update_purpose_and_label(false, BIO7, BP_PIN_IO, pin_labels[4]);
    if (mode_config.rx_pin != BIO1 && mode_config.rx_pin != BIO3 && mode_config.rx_pin != BIO5 && mode_config.rx_pin != BIO7) {
        system_bio_update_purpose_and_label(false, mode_config.rx_pin, BP_PIN_IO, pin_labels[5]);
    }
    // 1. Disable any hardware you used
    bio_init();
    system_config.subprotocol_name = 0x00;
    system_config.num_bits=8;
}



// Handler for any numbers the user enters (1, 0x01, 0b1) or string data "string"
// This function generally writes data out to the IO pins or a peripheral
void infrared_write(struct _bytecode* result, struct _bytecode* next) {
    // each protocol has its own write function
    ir_protocol[mode_config.protocol].irtx_write(&result->out_data);
    //TODO: frame delay if next command is a write or something?
    //ir_protocol[mode_config.protocol].irtx_wait_idle();
}

// This function is called when the user enters 'r' to read data
void infrared_read(struct _bytecode* result, struct _bytecode* next) {
    // your code
    /*uint8_t data=0;
    for(uint8_t i=0; i<8; i++){
        data |= bio_get(i) << i;
    }
    result->in_data=data; //put the read value in in_data (up to 32 bits)*/
    //rc5_test();
}

void infrared_wait_idle(void) {
    ir_protocol[mode_config.protocol].irtx_wait_idle();
}

// modes can have useful macros activated by (1) (eg macro 1)
// macros are passed from the command line directly, not through the syntax system
void infrared_macro(uint32_t macro) {
    printf("-DUMMY1- macro(%d)\r\n", macro);
    // your code
    switch (macro) {
        // macro (0) is always a menu of macros
        case 0:
            printf(" 0. This menu\r\n");
            break;
    }
}


// The Bus Pirate will make a periodic call to this function (if linked in modes.c)
// Useful for checking async stuff like bytes in a UART
void infrared_periodic(void){
    uint32_t rx_frame;
    ir_protocol[mode_config.protocol].irrx_read(&rx_frame);
    return;
}

void infrared_help(void) {
    ui_help_mode_commands(infrared_commands, infrared_commands_count);
}

uint32_t infrared_get_speed(void) {
    return mode_config.tx_freq;
}

// shared configuration accessors used by the INFRARED mode commands
uint8_t infrared_get_tx_pin(void) {
    return (uint8_t)mode_config.tx_pin;
}

uint8_t infrared_get_rx_pin(void) {
    return (uint8_t)mode_config.rx_pin;
}

uint8_t infrared_get_engine_protocol(void) {
    return (uint8_t)mode_config.engine_protocol;
}

bool infrared_set_engine_protocol(uint8_t idx) {
    if (idx >= ir_protocols_count()) {
        return false;
    }
    mode_config.engine_protocol = idx;
    infrared_save_config();
    return true;
}

bool infrared_set_pins(uint8_t tx_bio, uint8_t rx_bio) {
    if (tx_bio > 7 || rx_bio > 7) {
        return false;
    }
    // tear down the mode PIO programs (they run on the old pins)
    if (!ir_cmd_hw_active()) {
        infrared_cleanup_temp();
    }
    // release the pin labels of the previous pins
    if (tx_bio != mode_config.tx_pin) {
        system_bio_update_purpose_and_label(false, mode_config.tx_pin, BP_PIN_IO, pin_labels[2]);
    }
    if (rx_bio != mode_config.rx_pin && rx_bio != BIO1 && rx_bio != BIO3 && rx_bio != BIO5 && rx_bio != BIO7) {
        system_bio_update_purpose_and_label(false, mode_config.rx_pin, BP_PIN_IO, pin_labels[5]);
    }
    mode_config.tx_pin = tx_bio;
    mode_config.rx_pin = rx_bio;
    // keep the RX sensor preset in sync when the pin is a known sensor
    for (uint8_t i = 0; i < count_of(ir_rx_pins); i++) {
        if (ir_rx_pins[i] == rx_bio) {
            mode_config.rx_sensor = i;
            break;
        }
    }
    infrared_save_config();
    if (!ir_cmd_hw_active()) {
        // update labels and restart the mode PIO programs on the new pins
        system_bio_update_purpose_and_label(true, tx_bio, BP_PIN_IO, pin_labels[2]);
        if (rx_bio != BIO1 && rx_bio != BIO3 && rx_bio != BIO5 && rx_bio != BIO7) {
            system_bio_update_purpose_and_label(true, rx_bio, BP_PIN_IO, pin_labels[5]);
        }
        infrared_setup_resume();
    }
    return true;
}

void infrared_settings(void) {
    ui_help_setting_string(GET_T(T_IR_PROTOCOL_MENU), GET_T(infrared_protocol_choices[mode_config.protocol].label), 0x00);
    ui_help_setting_string(GET_T(T_IR_RX_SENSOR_MENU), GET_T(infrared_rxsensor_choices[mode_config.rx_sensor].label), 0x00);
    ui_help_setting_int(GET_T(T_IR_TX_SPEED_MENU), mode_config.tx_freq/1000, GET_T(T_KHZ));
    ui_help_setting_string(GET_T(T_IR_CMD_SETPROTOCOL), ir_protocols_get(mode_config.engine_protocol)->display, 0x00);
    ui_help_setting_int(GET_T(T_IR_PIN_TX), mode_config.tx_pin, 0x00);
    ui_help_setting_int(GET_T(T_IR_PIN_RX), mode_config.rx_pin, 0x00);
}