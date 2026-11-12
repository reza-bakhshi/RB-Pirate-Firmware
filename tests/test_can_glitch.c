#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

#include "pirate.h"
#include "system_config.h"
#include "bytecode.h"
#include "command_struct.h"
#include "pirate/bio.h"
#include "pirate/storage.h"
#include "pirate/hwcan_pio.h"
#include "mode/can.h"
#include "commands/global/cmd_glitch.h"
#include "services/bp_protocol_call.h"
#include "kernel/bp_resource.h"
#include "translation/base.h"

// Stubs for host test
struct _system_config system_config;
const uint8_t bio2bufiopin[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };

void system_bio_update_purpose_and_label(bool enable, uint8_t bio_pin, enum bp_pin_func func, const char* label) {
    (void)enable; (void)bio_pin; (void)func; (void)label;
}
bool ui_help_sanity_check(bool b, uint8_t u) { (void)b; (void)u; return true; }
const char* ui_term_color_info(void) { return ""; }
const char* ui_term_color_reset(void) { return ""; }
const char* ui_term_color_error(void) { return ""; }
const char* ui_term_color_notice(void) { return ""; }
const char* ui_term_color_prompt(void) { return ""; }
const char* ui_term_color_warning(void) { return ""; }

bool ui_prompt_user_input(char *buf, size_t buflen) { (void)buf; (void)buflen; return false; }
bool ui_parse_get_bool(bool *val) { (void)val; return false; }
bool ui_parse_get_float(float *val) { (void)val; return false; }
bool ui_parse_get_uint32(uint32_t *val) { (void)val; return false; }

static const char *s_cmdln_buf = "";
const char *ln_cmdln_current(void) { return s_cmdln_buf; }
size_t ln_cmdln_remaining(void) { return strlen(s_cmdln_buf); }

const char *GET_T(enum T_translations index) {
    (void)index;
    return "Unknown";
}

uint32_t storage_load_mode(const char* f, const mode_config_t* c, uint8_t n) { (void)f; (void)c; (void)n; return 0; }
uint32_t storage_save_mode(const char* f, const mode_config_t* c, uint8_t n) { (void)f; (void)c; (void)n; return 0; }
void bio_init(void) {}
bool rx_fifo_try_get(char* c) { (void)c; return false; }
bool button_get(uint8_t b) { (void)b; return false; }

uint32_t bp_job_get_active_id(void) { return 0; }
bp_status_t bp_term_job_getc(char *key) { (void)key; return BP_STATUS_QUEUE_EMPTY; }
static bool mock_cancel;
static unsigned mock_worker_calls;
bool bp_job_check_cancelled(void) { return mock_cancel; }
bp_status_t bp_protocol_call(const char *name, bp_job_slice_fn_t step, void *ctx) {
    (void)name;
    mock_worker_calls++;
    uint8_t percent;
    for (unsigned i = 0; i < 1001; ++i) {
        bp_status_t status = step(ctx, &percent);
        if (status != BP_STATUS_BUSY) return status;
    }
    return BP_STATUS_TIMEOUT;
}
static void test_glitch_limits_and_cleanup(void) {
    assert(bp_res_init() == BP_STATUS_OK);
    for (unsigned i = 1; i <= 8; ++i) system_config.pin_func[i] = BP_PIN_IO;
    const char *invalid[] = { "glitch -p", "glitch -c 8", "glitch -c 2 -t 2",
        "glitch -c 2 -w 0", "glitch -c 2 -w 100001", "glitch -c 2 -d 100001",
        "glitch -c 2 -n 0", "glitch -c 2 -n 1001", "glitch -c 2 -w garbage" };
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        s_cmdln_buf = invalid[i];
        struct command_result result = {0};
        cmd_glitch_handler(&result);
        assert(result.error);
        assert(mock_worker_calls == 0);
    }
    s_cmdln_buf = "glitch -c 2 -n 3";
    struct command_result result = {0};
    cmd_glitch_handler(&result);
    assert(!result.error && mock_worker_calls == 1);
    assert(bp_res_count_owned(12) == 0);
    mock_cancel = true;
    cmd_glitch_handler(&result);
    assert(result.error && bp_res_count_owned(12) == 0);
    mock_cancel = false;
    system_config.mode = 1;
    unsigned before = mock_worker_calls;
    cmd_glitch_handler(&result);
    assert(result.error && mock_worker_calls == before);
    system_config.mode = 0;
}

static void test_can_init_validation(void) {
    printf("[TEST] CAN init parameter validation...\n");
    // Invalid: same RX and TX pin
    assert(!hwcan_init(500000, 1, 1));
    // Invalid: bitrate 0
    assert(!hwcan_init(0, 1, 0));
    // Invalid: out-of-range pin
    assert(!hwcan_init(500000, 8, 0));
    // Valid
    assert(hwcan_init(500000, 1, 0));
    hwcan_cleanup();
    printf("  -> PASS\n");
}

static void test_can_tx_rx_loopback(void) {
    printf("[TEST] CAN standard & extended frame TX/RX loopback...\n");
    assert(hwcan_init(500000, 1, 0));

    // Send standard frame
    uint8_t payload[4] = { 0xDE, 0xAD, 0xBE, 0xEF };
    assert(hwcan_tx_frame(0x123, 4, payload, false, false));
    assert(hwcan_rx_available() == 1);

    hwcan_frame_t frame;
    assert(hwcan_pop_frame(&frame));
    assert(frame.id == 0x123);
    assert(frame.dlc == 4);
    assert(!frame.is_extended);
    assert(!frame.is_rtr);
    assert(memcmp(frame.data, payload, 4) == 0);
    assert(hwcan_rx_available() == 0);

    // Send extended frame
    uint8_t ext_payload[8] = { 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 };
    assert(hwcan_tx_frame(0x18DAF110, 8, ext_payload, true, false));
    assert(hwcan_rx_available() == 1);

    assert(hwcan_pop_frame(&frame));
    assert(frame.id == 0x18DAF110);
    assert(frame.dlc == 8);
    assert(frame.is_extended);
    assert(!frame.is_rtr);
    assert(memcmp(frame.data, ext_payload, 8) == 0);

    hwcan_cleanup();
    printf("  -> PASS\n");
}

static void test_can_filter(void) {
    printf("[TEST] CAN acceptance filtering...\n");
    assert(hwcan_init(500000, 1, 0));

    // Filter to ID 0x7DF exact match
    hwcan_set_filter(0x7DF, 0x7FF, true);

    // Send non-matching frame (0x123)
    assert(hwcan_tx_frame(0x123, 2, (uint8_t[]){1, 2}, false, false));
    assert(hwcan_rx_available() == 0); // Filtered out

    // Send matching frame (0x7DF)
    assert(hwcan_tx_frame(0x7DF, 2, (uint8_t[]){3, 4}, false, false));
    assert(hwcan_rx_available() == 1);

    hwcan_frame_t frame;
    assert(hwcan_pop_frame(&frame));
    assert(frame.id == 0x7DF);

    // Disable filter
    hwcan_set_filter(0, 0, false);
    assert(hwcan_tx_frame(0x123, 2, (uint8_t[]){1, 2}, false, false));
    assert(hwcan_rx_available() == 1);
    assert(hwcan_pop_frame(&frame));
    assert(frame.id == 0x123);

    hwcan_cleanup();
    printf("  -> PASS\n");
}

static void test_can_syntax_execution(void) {
    printf("[TEST] CAN syntax mode start/write/stop handlers...\n");
    assert(hwcan_init(500000, 1, 0));

    struct _bytecode bc_start = { 0 };
    can_start(&bc_start, NULL);
    assert(bc_start.error == SERR_NONE);

    // Send ID 0x321
    struct _bytecode bc_id = { .out_data = 0x321 };
    can_write(&bc_id, NULL);
    assert(bc_id.error == SERR_NONE);

    // Send payload byte 0xAA
    struct _bytecode bc_b0 = { .out_data = 0xAA };
    can_write(&bc_b0, NULL);
    assert(bc_b0.error == SERR_NONE);

    // Send payload byte 0x55
    struct _bytecode bc_b1 = { .out_data = 0x55 };
    can_write(&bc_b1, NULL);
    assert(bc_b1.error == SERR_NONE);

    // Stop and transmit
    struct _bytecode bc_stop = { 0 };
    can_stop(&bc_stop, NULL);
    assert(bc_stop.error == SERR_NONE);

    // Verify looped-back frame
    assert(hwcan_rx_available() == 1);
    hwcan_frame_t f;
    assert(hwcan_pop_frame(&f));
    assert(f.id == 0x321);
    assert(f.dlc == 2);
    assert(f.data[0] == 0xAA);
    assert(f.data[1] == 0x55);

    hwcan_cleanup();
    printf("  -> PASS\n");
}

static void test_can_stats(void) {
    printf("[TEST] CAN statistics accounting...\n");
    assert(hwcan_init(500000, 1, 0));
    hwcan_reset_stats();

    uint32_t rx = 0, tx = 0, err = 0;
    hwcan_get_stats(&rx, &tx, &err);
    assert(rx == 0 && tx == 0 && err == 0);

    assert(hwcan_tx_frame(0x100, 1, (uint8_t[]){0xFF}, false, false));
    hwcan_get_stats(&rx, &tx, &err);
    assert(tx == 1);
    assert(rx == 1);
    assert(err == 0);

    hwcan_cleanup();
    printf("  -> PASS\n");
}

int main(void) {
    printf("=== Running CAN Bus and Fault Injection Host Tests ===\n");
    test_glitch_limits_and_cleanup();
    test_can_init_validation();
    test_can_tx_rx_loopback();
    test_can_filter();
    test_can_syntax_execution();
    test_can_stats();
    printf("=== ALL CAN AND GLITCH TESTS PASSED ===\n");
    return 0;
}
