#define _POSIX_C_SOURCE 199309L
#define __dmb() __sync_synchronize()
#define tight_loop_contents() ((void)0)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdbool.h>

#include "services/bp_terminal.h"
#include "kernel/bp_status.h"
#include "kernel/bp_job.h"

/* Mock helpers exported by bp_terminal.c in host test mode */
extern void bp_term_mock_inject_rx(char c);
extern bool bp_term_mock_pop_tx(char* c);

static bp_status_t dummy_slice_fn(void* ctx, uint8_t* out_percent) {
    (void)ctx;
    *out_percent = 50;
    return BP_STATUS_BUSY;
}

static void test_terminal_init(void) {
    printf("[TEST] Terminal service initialization...\n");
    bp_status_t st = bp_term_init();
    assert(st == BP_STATUS_OK);

    bp_term_stats_t stats;
    bp_term_get_stats(&stats);
    assert(stats.tx_level == 0);
    assert(stats.tx_free > 0);
    assert(stats.core1_text_level == 0);
    assert(stats.core1_text_dropped == 0);
    assert(stats.job_input_level == 0);
    printf("[PASS] Initialization state verified\n");
}

static void test_nonblocking_output(void) {
    printf("[TEST] Non-blocking output and buffer backpressure...\n");
    bp_term_init();

    /* Write characters until host TX queue is saturated */
    uint32_t written = 0;
    while (bp_term_putc('X') == BP_STATUS_OK) {
        written++;
        assert(written < 10000); /* Must not infinite loop */
    }
    assert(written > 0);

    /* Subsequent write returns BP_STATUS_QUEUE_FULL without blocking */
    bp_status_t st = bp_term_putc('Y');
    assert(st == BP_STATUS_QUEUE_FULL);

    const char* flood = "1234567890";
    st = bp_term_write(flood, strlen(flood));
    assert(st == BP_STATUS_QUEUE_FULL);

    bp_term_stats_t stats;
    bp_term_get_stats(&stats);
    assert(stats.tx_dropped > 0);

    /* Drain the TX queue */
    char c;
    uint32_t drained = 0;
    while (bp_term_mock_pop_tx(&c)) {
        drained++;
    }
    assert(drained == written);

    /* Buffer now drains empty */
    assert(bp_term_wait_drain(10));
    printf("[PASS] Non-blocking output verified (drained %u bytes, dropped accounted)\n", drained);
}

static void test_core1_text_streaming(void) {
    printf("[TEST] Core 1 text streaming and FIFO ordering...\n");
    bp_term_init();

    const char* msg = "Core1: Diagnostic test 12345\r\n";
    size_t len = strlen(msg);

    /* Core 1 produces output into the Core 1 text ring */
    for (size_t i = 0; i < len; i++) {
        bp_status_t st = bp_term_core1_putc(msg[i]);
        assert(st == BP_STATUS_OK);
    }

    bp_term_stats_t stats;
    bp_term_get_stats(&stats);
    assert(stats.core1_text_level == len);

    /* Core 0 services terminal, draining Core 1 ring into TX stream */
    bp_term_service();

    bp_term_get_stats(&stats);
    assert(stats.core1_text_level == 0);

    /* Verify TX stream receives the exact string in strict FIFO order */
    char received[64] = {0};
    uint32_t r_idx = 0;
    char c;
    while (bp_term_mock_pop_tx(&c) && r_idx < sizeof(received) - 1) {
        received[r_idx++] = c;
    }
    received[r_idx] = '\0';

    assert(strcmp(received, msg) == 0);
    printf("[PASS] Core 1 text arrived in exact FIFO order: \"%s\"\n", received);
}

static void test_ctrl_c_cancellation_interception(void) {
    printf("[TEST] Input demultiplexing: Ctrl+C aborts active protocol job...\n");
    bp_term_init();
    bp_job_subsystem_init();

    /* Submit simulated protocol job on Core 1 */
    uint32_t jid = 0;
    bp_status_t st = bp_job_submit("sim_job", 1, 10000, dummy_slice_fn, NULL, 0, &jid);
    assert(st == BP_STATUS_OK);
    assert(jid > 0);
    assert(bp_job_get_active_id() == jid);

    /* User presses Ctrl+C (0x03) */
    bp_term_mock_inject_rx(0x03);

    /* Core 0 runs terminal service */
    bp_term_service();

    /* 1. Job must be immediately cancelled */
    assert(bp_job_check_cancelled());

    /* 2. 0x03 must be consumed from input queue */
    char c;
    assert(bp_term_getc(&c) == BP_STATUS_QUEUE_EMPTY);

    /* 3. 0x03 must NOT be forwarded to the job input queue */
    assert(bp_term_job_getc(&c) == BP_STATUS_QUEUE_EMPTY);

    printf("[PASS] Ctrl+C intercepted and job cancelled cleanly without polluting buffers\n");
}

static void test_interactive_job_input_routing(void) {
    printf("[TEST] Input demultiplexing: user keystrokes routed to active job...\n");
    bp_term_init();
    bp_job_subsystem_init();

    /* Submit active job */
    uint32_t jid = 0;
    bp_status_t st = bp_job_submit("interactive_job", 2, 10000, dummy_slice_fn, NULL, 0, &jid);
    assert(st == BP_STATUS_OK);

    /* User types commands meant for the active protocol: "run\n" */
    const char* user_in = "run\n";
    for (size_t i = 0; i < strlen(user_in); i++) {
        bp_term_mock_inject_rx(user_in[i]);
    }

    /* Core 0 services terminal */
    bp_term_service();

    /* Characters should now be queued in the job input ring */
    char job_buf[16] = {0};
    uint32_t read_bytes = 0;
    st = bp_term_job_read(job_buf, sizeof(job_buf), &read_bytes);
    assert(st == BP_STATUS_OK);
    assert(read_bytes == strlen(user_in));
    assert(strcmp(job_buf, user_in) == 0);

    /* Input queue for shell prompt must be empty (job consumed it) */
    char c;
    assert(bp_term_getc(&c) == BP_STATUS_QUEUE_EMPTY);

    printf("[PASS] User keystrokes successfully routed to active Core 1 job: \"%s\"\n", job_buf);
}

static void test_idle_input_preservation(void) {
    printf("[TEST] Input demultiplexing: idle input preserved for shell prompt...\n");
    bp_term_init();
    bp_job_subsystem_init();

    /* Ensure no job active */
    assert(bp_job_get_active_id() == 0);

    /* User types command at prompt: "help\n" */
    const char* prompt_cmd = "help\n";
    for (size_t i = 0; i < strlen(prompt_cmd); i++) {
        bp_term_mock_inject_rx(prompt_cmd[i]);
    }

    /* Core 0 services terminal */
    bp_term_service();

    /* Job input ring must be empty */
    char c;
    assert(bp_term_job_getc(&c) == BP_STATUS_QUEUE_EMPTY);

    /* Keystrokes must remain in Core 0 input stream for prompt/linenoise */
    char shell_in[16] = {0};
    uint32_t idx = 0;
    while (bp_term_getc(&c) == BP_STATUS_OK && idx < sizeof(shell_in) - 1) {
        shell_in[idx++] = c;
    }
    shell_in[idx] = '\0';

    assert(strcmp(shell_in, prompt_cmd) == 0);
    printf("[PASS] Idle shell input preserved: \"%s\"\n", shell_in);
}

static void test_job_input_does_not_leak(void) {
    bp_term_init(); bp_job_subsystem_init();
    uint32_t id;
    assert(bp_job_submit("old", 0, 0, dummy_slice_fn, NULL, 1, &id) == BP_STATUS_OK);
    bp_term_mock_inject_rx('q'); bp_term_service();
    assert(bp_job_cancel(id) == BP_STATUS_OK); bp_job_service(2);
    assert(bp_job_submit("next", 0, 0, dummy_slice_fn, NULL, 3, &id) == BP_STATUS_OK);
    char key;
    assert(bp_term_job_getc(&key) == BP_STATUS_QUEUE_EMPTY);
    puts("[PASS] A stale stop key cannot cancel the next protocol transaction");
}

int main(void) {
    printf("=== Bus Pirate Terminal Service Test Suite ===\n");
    test_terminal_init();
    test_nonblocking_output();
    test_core1_text_streaming();
    test_ctrl_c_cancellation_interception();
    test_interactive_job_input_routing();
    test_job_input_does_not_leak();
    test_idle_input_preservation();

    printf("\n=== ALL TERMINAL SERVICE TESTS PASSED ===\n");
    return 0;
}
