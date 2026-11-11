/**
 * @file bp_terminal.c
 * @brief Terminal service implementation for Bus Pirate (Core 0 owned).
 */

#include "services/bp_terminal.h"
#include "spsc_queue.h"
#include "kernel/bp_job.h"
#include <string.h>

#if !defined(BP_HOST_TEST) && __has_include("usb_tx.h") && __has_include("usb_rx.h")
#include "usb_tx.h"
#include "usb_rx.h"
#define HAVE_USB_FIFOS 1
#endif

/* Core 1 text output queue */
static uint8_t s_core1_text_buf[BP_TERM_CORE1_TEXT_RING_SIZE];
static spsc_queue_t s_core1_text_q;
static volatile uint32_t s_core1_text_dropped = 0;

/* Core 0 -> Core 1 job interactive input queue */
static uint8_t s_job_input_buf[BP_TERM_JOB_INPUT_RING_SIZE];
static spsc_queue_t s_job_input_q;

#ifndef HAVE_USB_FIFOS
/* Host mock queues for standalone unit testing */
static uint8_t s_host_tx_buf[4096];
static spsc_queue_t s_host_tx_q;
static volatile uint32_t s_host_tx_dropped = 0;

static uint8_t s_host_rx_buf[256];
static spsc_queue_t s_host_rx_q;
#endif

static bool s_initialized = false;

static void reset_job_input(void) {
    /* Submission runs on Core 0 after the previous worker acknowledged completion. */
    spsc_queue_flush(&s_job_input_q);
}

bp_status_t bp_term_init(void) {
    spsc_queue_init(&s_core1_text_q, s_core1_text_buf, BP_TERM_CORE1_TEXT_RING_SIZE);
    spsc_queue_init(&s_job_input_q, s_job_input_buf, BP_TERM_JOB_INPUT_RING_SIZE);
    bp_job_set_input_reset_hook(reset_job_input);
    s_core1_text_dropped = 0;

#ifndef HAVE_USB_FIFOS
    spsc_queue_init(&s_host_tx_q, s_host_tx_buf, sizeof(s_host_tx_buf));
    spsc_queue_init(&s_host_rx_q, s_host_rx_buf, sizeof(s_host_rx_buf));
    s_host_tx_dropped = 0;
#endif

    s_initialized = true;
    return BP_STATUS_OK;
}

bp_status_t bp_term_putc(char c) {
    if (!s_initialized) bp_term_init();

#ifdef HAVE_USB_FIFOS
    bp_tx_status_t st = tx_fifo_put(&c);
    if (st == BP_TX_OK) return BP_STATUS_OK;
    return BP_STATUS_QUEUE_FULL;
#else
    if (spsc_queue_try_add(&s_host_tx_q, (uint8_t)c)) {
        return BP_STATUS_OK;
    }
    s_host_tx_dropped++;
    return BP_STATUS_QUEUE_FULL;
#endif
}

bp_status_t bp_term_write(const char* buf, uint32_t len) {
    if (!buf) return BP_STATUS_INVALID_PARAM;
    if (!s_initialized) bp_term_init();

#ifdef HAVE_USB_FIFOS
    bp_tx_status_t st = tx_fifo_write(buf, len);
    if (st == BP_TX_OK) return BP_STATUS_OK;
    return BP_STATUS_QUEUE_FULL;
#else
    bp_status_t overall = BP_STATUS_OK;
    for (uint32_t i = 0; i < len; i++) {
        if (bp_term_putc(buf[i]) != BP_STATUS_OK) {
            overall = BP_STATUS_QUEUE_FULL;
        }
    }
    return overall;
#endif
}

bp_status_t bp_term_getc(char* c) {
    if (!c) return BP_STATUS_INVALID_PARAM;
    if (!s_initialized) bp_term_init();

#ifdef HAVE_USB_FIFOS
    if (rx_fifo_try_get(c)) return BP_STATUS_OK;
    return BP_STATUS_QUEUE_EMPTY;
#else
    uint8_t b;
    if (spsc_queue_try_remove(&s_host_rx_q, &b)) {
        *c = (char)b;
        return BP_STATUS_OK;
    }
    return BP_STATUS_QUEUE_EMPTY;
#endif
}

bp_status_t bp_term_peekc(char* c) {
    if (!c) return BP_STATUS_INVALID_PARAM;
    if (!s_initialized) bp_term_init();

#ifdef HAVE_USB_FIFOS
    if (rx_fifo_try_peek(c)) return BP_STATUS_OK;
    return BP_STATUS_QUEUE_EMPTY;
#else
    uint8_t b;
    if (spsc_queue_try_peek(&s_host_rx_q, &b)) {
        *c = (char)b;
        return BP_STATUS_OK;
    }
    return BP_STATUS_QUEUE_EMPTY;
#endif
}

bp_status_t bp_term_core1_putc(char c) {
    if (!s_initialized) bp_term_init();

    if (spsc_queue_try_add(&s_core1_text_q, (uint8_t)c)) {
        return BP_STATUS_OK;
    }

    s_core1_text_dropped++;
    return BP_STATUS_QUEUE_FULL;
}

bp_status_t bp_term_job_read(char* buf, uint32_t max_len, uint32_t* out_len) {
    if (!buf || max_len == 0 || !out_len) return BP_STATUS_INVALID_PARAM;
    if (!s_initialized) bp_term_init();

    uint32_t count = 0;
    uint8_t b;
    while (count < max_len && spsc_queue_try_remove(&s_job_input_q, &b)) {
        buf[count++] = (char)b;
    }
    *out_len = count;
    if (count == 0) return BP_STATUS_QUEUE_EMPTY;
    return BP_STATUS_OK;
}

bp_status_t bp_term_job_getc(char* c) {
    if (!c) return BP_STATUS_INVALID_PARAM;
    if (!s_initialized) bp_term_init();

    uint8_t b;
    if (spsc_queue_try_remove(&s_job_input_q, &b)) {
        *c = (char)b;
        return BP_STATUS_OK;
    }
    return BP_STATUS_QUEUE_EMPTY;
}

void bp_term_service(void) {
    if (!s_initialized) bp_term_init();

    /* 1. Drain Core 1 text ring into TX output stream */
    uint8_t c1_byte;
    for (unsigned budget = 0; budget < 256 && spsc_queue_try_remove(&s_core1_text_q, &c1_byte); budget++) {
        (void)bp_term_putc((char)c1_byte);
    }

    /* 2. Process inbound characters and handle Ctrl+C / job routing */
    uint32_t active_jid = bp_job_get_active_id();
    if (active_jid > 0) {
        char in_c;
        while (bp_term_peekc(&in_c) == BP_STATUS_OK) {
            if (in_c == 0x03) { /* Ctrl+C */
                /* Consume Ctrl+C from input queue without forwarding to job */
                (void)bp_term_getc(&in_c);
                /* Immediately request cooperative cancellation */
                bp_job_cancel(active_jid);
                break;
            } else {
                /* Route user keystroke to Core 1 job input ring */
                if (spsc_queue_try_add(&s_job_input_q, (uint8_t)in_c)) {
                    (void)bp_term_getc(&in_c);
                } else {
                    /* Job input ring is full; leave remaining in input queue */
                    break;
                }
            }
        }
    }
}

bool bp_term_wait_drain(uint32_t timeout_ms) {
    if (!s_initialized) bp_term_init();

#ifdef HAVE_USB_FIFOS
    return tx_fifo_wait_drain_timeout(timeout_ms);
#else
    (void)timeout_ms;
    return spsc_queue_is_empty(&s_host_tx_q);
#endif
}

void bp_term_get_stats(bp_term_stats_t* stats) {
    if (!stats) return;
    if (!s_initialized) bp_term_init();

    memset(stats, 0, sizeof(*stats));
#ifdef HAVE_USB_FIFOS
    stats->tx_level = tx_fifo_level();
    stats->tx_free = tx_fifo_free();
#else
    stats->tx_level = spsc_queue_level(&s_host_tx_q);
    stats->tx_free = spsc_queue_free(&s_host_tx_q);
    stats->tx_dropped = s_host_tx_dropped;
#endif
    stats->core1_text_level = spsc_queue_level(&s_core1_text_q);
    stats->core1_text_dropped = s_core1_text_dropped;
    stats->job_input_level = spsc_queue_level(&s_job_input_q);
}

#ifndef HAVE_USB_FIFOS
/* Host mock injection helpers for test suites */
void bp_term_mock_inject_rx(char c) {
    if (!s_initialized) bp_term_init();
    spsc_queue_try_add(&s_host_rx_q, (uint8_t)c);
}

bool bp_term_mock_pop_tx(char* c) {
    if (!s_initialized) bp_term_init();
    uint8_t b;
    if (spsc_queue_try_remove(&s_host_tx_q, &b)) {
        *c = (char)b;
        return true;
    }
    return false;
}
#endif
