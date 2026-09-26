/**
 * @file infrared.h
 * @brief Infrared protocol mode interface.
 * @details Provides infrared communication mode for remote control protocols.
 */

void infrared_write(struct _bytecode* result, struct _bytecode* next);
void infrared_read(struct _bytecode* result, struct _bytecode* next);
void infrared_start(struct _bytecode* result, struct _bytecode* next);
void infrared_stop(struct _bytecode* result, struct _bytecode* next);
void infrared_startr(void);
void infrared_stopr(void);
void infrared_macro(uint32_t macro);
void infrared_periodic(void);
uint32_t infrared_setup(void);
uint32_t infrared_setup_exc(void);
void infrared_cleanup(void);
bool infrared_preflight_sanity_check(void);
void infrared_settings(void);
const char* infrared_pins(void);
void infrared_help(void);
void infrared_wait_idle(void);

uint32_t infrared_get_speed(void);

// shared mode configuration accessors for INFRARED mode commands
uint8_t infrared_get_tx_pin(void);        ///< BIO pin used for IR TX (default BIO4)
uint8_t infrared_get_rx_pin(void);        ///< BIO pin used for IR RX (default BIO5)
uint8_t infrared_get_engine_protocol(void); ///< ir_protocols table index used by send/receive
bool infrared_set_engine_protocol(uint8_t idx); ///< select + persist protocol
bool infrared_set_pins(uint8_t tx_bio, uint8_t rx_bio); ///< select + persist TX/RX pins

//for pausing the PIO programs while other programs use the hardware
void infrared_setup_resume(void);
void infrared_cleanup_temp(void);

extern const struct _mode_command_struct infrared_commands[];
extern const uint32_t infrared_commands_count;
extern const struct bp_command_def infrared_setup_def;

typedef struct _infrared_mode_config {
    uint32_t rx_sensor;      ///< preset RX sensor index (setup menu)
    uint32_t protocol;       ///< mode protocol table index (RAW/NEC/RC5)
    uint32_t tx_freq;        ///< TX carrier frequency in Hz
    uint32_t tx_pin;         ///< BIO pin used for IR TX (default BIO4)
    uint32_t rx_pin;         ///< BIO pin used for IR RX (default BIO5)
    uint32_t engine_protocol; ///< ir_protocols table index (setprotocol)
} _infrared_mode_config;
