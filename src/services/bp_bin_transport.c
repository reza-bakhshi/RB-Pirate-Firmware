/**
 * @file bp_bin_transport.c
 * @brief Binary transport service implementation for Bus Pirate.
 */

#include "services/bp_bin_transport.h"
#include "spsc_queue.h"
#include <string.h>

#if !defined(BP_HOST_TEST)
#include "pico/stdlib.h"
#include "pico/time.h"
#include "tusb.h"
#include "pirate/watchdog.h"
#endif

/* Statically allocated ring buffers */
static uint8_t s_bin_tx_buf[BP_BIN_TX_RING_SIZE] __attribute__((aligned(4)));
static uint8_t s_bin_rx_buf[BP_BIN_RX_RING_SIZE] __attribute__((aligned(4)));

static spsc_queue_t s_bin_tx_q;
static spsc_queue_t s_bin_rx_q;

static volatile uint32_t s_tx_dropped = 0;
static volatile uint32_t s_rx_dropped = 0;
static volatile uint32_t s_tx_packets = 0;
static volatile uint32_t s_rx_packets = 0;
static volatile bool     s_tx_flush_pending = false;

static bool s_initialized = false;

#ifdef BP_HOST_TEST
static bool s_mock_connected = true;
#endif

bp_status_t bp_bin_transport_init(void) {
    spsc_queue_init(&s_bin_tx_q, s_bin_tx_buf, BP_BIN_TX_RING_SIZE);
    spsc_queue_init(&s_bin_rx_q, s_bin_rx_buf, BP_BIN_RX_RING_SIZE);
    s_tx_dropped = 0;
    s_rx_dropped = 0;
    s_tx_packets = 0;
    s_rx_packets = 0;
    s_tx_flush_pending = false;
    s_initialized = true;
    return BP_STATUS_OK;
}

bool bp_bin_transport_is_connected(void) {
    if (!s_initialized) bp_bin_transport_init();

#if !defined(BP_HOST_TEST)
    return tud_cdc_n_connected(1);
#else
    return s_mock_connected;
#endif
}

bp_status_t bp_bin_transport_read(uint8_t* buf, uint32_t max_len, uint32_t* out_read) {
    if (!buf || max_len == 0 || !out_read) return BP_STATUS_INVALID_PARAM;
    if (!s_initialized) bp_bin_transport_init();

    uint32_t count = 0;
    uint8_t b;
    while (count < max_len && spsc_queue_try_remove(&s_bin_rx_q, &b)) {
        buf[count++] = b;
    }

    *out_read = count;
    if (count == 0) return BP_STATUS_QUEUE_EMPTY;
    s_rx_packets++;
    return BP_STATUS_OK;
}

bp_status_t bp_bin_transport_getc(uint8_t* c) {
    if (!c) return BP_STATUS_INVALID_PARAM;
    if (!s_initialized) bp_bin_transport_init();

    if (spsc_queue_try_remove(&s_bin_rx_q, c)) {
        return BP_STATUS_OK;
    }
    return BP_STATUS_QUEUE_EMPTY;
}

bp_status_t bp_bin_transport_getc_timeout(uint8_t* c, uint32_t timeout_ms) {
    if (!c) return BP_STATUS_INVALID_PARAM;
    if (!s_initialized) bp_bin_transport_init();

#if !defined(BP_HOST_TEST)
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    do {
        if (spsc_queue_try_remove(&s_bin_rx_q, c)) {
            return BP_STATUS_OK;
        }
        bp_watchdog_heartbeat(get_core_num());
        bp_watchdog_service();
        tight_loop_contents();
    } while (!time_reached(deadline));
    return BP_STATUS_TIMEOUT;
#else
    if (spsc_queue_try_remove(&s_bin_rx_q, c)) {
        return BP_STATUS_OK;
    }
    (void)timeout_ms;
    return BP_STATUS_TIMEOUT;
#endif
}

uint32_t bp_bin_transport_rx_available(void) {
    if (!s_initialized) bp_bin_transport_init();
    return spsc_queue_level(&s_bin_rx_q);
}

void bp_bin_transport_rx_flush(void) {
    if (!s_initialized) bp_bin_transport_init();
    uint8_t dummy;
    while (spsc_queue_try_remove(&s_bin_rx_q, &dummy)) {
        /* discard */
    }
}

bp_status_t bp_bin_transport_write(const uint8_t* buf, uint32_t len, uint32_t* out_written) {
    if (!buf) return BP_STATUS_INVALID_PARAM;
    if (len == 0) {
        if (out_written) *out_written = 0;
        return BP_STATUS_OK;
    }
    if (!s_initialized) bp_bin_transport_init();

    uint32_t free_space = spsc_queue_free(&s_bin_tx_q);
    uint32_t to_write = (len < free_space) ? len : free_space;

    for (uint32_t i = 0; i < to_write; i++) {
        (void)spsc_queue_try_add(&s_bin_tx_q, buf[i]);
    }

    if (out_written) {
        *out_written = to_write;
    }

    s_tx_packets++;

    if (to_write < len) {
        s_tx_dropped += (len - to_write);
        return BP_STATUS_QUEUE_FULL;
    }

    return BP_STATUS_OK;
}

bp_status_t bp_bin_transport_putc(uint8_t c) {
    if (!s_initialized) bp_bin_transport_init();

    if (spsc_queue_try_add(&s_bin_tx_q, c)) {
        s_tx_packets++;
        return BP_STATUS_OK;
    }

    s_tx_dropped++;
    return BP_STATUS_QUEUE_FULL;
}

bp_status_t bp_bin_transport_putc_timeout(uint8_t c, uint32_t timeout_ms) {
    if (!s_initialized) bp_bin_transport_init();

#if !defined(BP_HOST_TEST)
    absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
    do {
        if (spsc_queue_try_add(&s_bin_tx_q, c)) {
            s_tx_packets++;
            return BP_STATUS_OK;
        }
        bp_watchdog_heartbeat(get_core_num());
        bp_watchdog_service();
        tight_loop_contents();
    } while (!time_reached(deadline));
    s_tx_dropped++;
    return BP_STATUS_TIMEOUT;
#else
    if (spsc_queue_try_add(&s_bin_tx_q, c)) {
        s_tx_packets++;
        return BP_STATUS_OK;
    }
    (void)timeout_ms;
    s_tx_dropped++;
    return BP_STATUS_TIMEOUT;
#endif
}

void bp_bin_transport_tx_flush(void) {
    if (!s_initialized) bp_bin_transport_init();
    s_tx_flush_pending = true;
}

uint32_t bp_bin_transport_tx_free(void) {
    if (!s_initialized) bp_bin_transport_init();
    return spsc_queue_free(&s_bin_tx_q);
}

void bp_bin_transport_get_stats(bp_bin_transport_stats_t* stats) {
    if (!stats) return;
    if (!s_initialized) bp_bin_transport_init();

    stats->rx_level = spsc_queue_level(&s_bin_rx_q);
    stats->rx_free  = spsc_queue_free(&s_bin_rx_q);
    stats->rx_dropped = s_rx_dropped;
    stats->tx_level = spsc_queue_level(&s_bin_tx_q);
    stats->tx_free  = spsc_queue_free(&s_bin_tx_q);
    stats->tx_dropped = s_tx_dropped;
    stats->rx_packets = s_rx_packets;
    stats->tx_packets = s_tx_packets;
    stats->cdc1_connected = bp_bin_transport_is_connected();
}

void bp_bin_transport_service(void) {
    if (!s_initialized) bp_bin_transport_init();

    /* Queue consumption belongs exclusively to the USB service on Core 1. */
}

void bp_bin_transport_usb_service(void) {
    if (!s_initialized) bp_bin_transport_init();

    /* Disconnect cleanup uses the same consumer as normal USB transmission. */
    if (!bp_bin_transport_is_connected()) {
        uint8_t dummy;
        while (spsc_queue_try_remove(&s_bin_tx_q, &dummy)) s_tx_dropped++;
        s_tx_flush_pending = false;
    }

#if !defined(BP_HOST_TEST)
    /* 1. Pull inbound bytes from CDC1 into RX ring */
    if (tud_cdc_n_connected(1)) {
        uint32_t avail = tud_cdc_n_available(1);
        if (avail > 0) {
            uint8_t rx_tmp[64];
            uint32_t free_sp = spsc_queue_free(&s_bin_rx_q);
            uint32_t to_read = (avail < sizeof(rx_tmp)) ? avail : sizeof(rx_tmp);
            if (to_read > free_sp) {
                /* Fill remaining queue space */
                uint32_t can_read = free_sp;
                if (can_read > 0) {
                    uint32_t n = tud_cdc_n_read(1, rx_tmp, can_read);
                    for (uint32_t i = 0; i < n; i++) {
                        (void)spsc_queue_try_add(&s_bin_rx_q, rx_tmp[i]);
                    }
                }
                /* Consume and account dropped remainder to prevent USB stall */
                uint32_t drop_to_read = to_read - can_read;
                uint32_t dropped = tud_cdc_n_read(1, rx_tmp, drop_to_read);
                s_rx_dropped += dropped;
            } else {
                uint32_t n = tud_cdc_n_read(1, rx_tmp, to_read);
                for (uint32_t i = 0; i < n; i++) {
                    (void)spsc_queue_try_add(&s_bin_rx_q, rx_tmp[i]);
                }
            }
        }
    }

    /* 2. Drain TX ring into CDC1 */
    if (tud_cdc_n_connected(1)) {
        uint32_t wr_avail = tud_cdc_n_write_available(1);
        if (wr_avail >= 64 || s_tx_flush_pending) {
            uint32_t q_lvl = spsc_queue_level(&s_bin_tx_q);
            if (q_lvl > 0 && wr_avail > 0) {
                uint8_t tx_tmp[64];
                uint32_t chunk = (q_lvl < sizeof(tx_tmp)) ? q_lvl : sizeof(tx_tmp);
                if (chunk > wr_avail) chunk = wr_avail;
                for (uint32_t i = 0; i < chunk; i++) {
                    (void)spsc_queue_try_remove(&s_bin_tx_q, &tx_tmp[i]);
                }
                tud_cdc_n_write(1, tx_tmp, chunk);
                tud_cdc_n_write_flush(1);
                s_tx_flush_pending = false;
            }
        }
    }
#endif
}

#ifdef BP_HOST_TEST
void bp_bin_transport_mock_inject_rx(const uint8_t* data, uint32_t len) {
    if (!s_initialized) bp_bin_transport_init();
    if (!data) return;
    for (uint32_t i = 0; i < len; i++) {
        if (!spsc_queue_try_add(&s_bin_rx_q, data[i])) {
            s_rx_dropped++;
        }
    }
}

uint32_t bp_bin_transport_mock_drain_tx(uint8_t* out_buf, uint32_t max_len) {
    if (!s_initialized) bp_bin_transport_init();
    if (!out_buf || max_len == 0) return 0;
    uint32_t count = 0;
    uint8_t b;
    while (count < max_len && spsc_queue_try_remove(&s_bin_tx_q, &b)) {
        out_buf[count++] = b;
    }
    return count;
}

void bp_bin_transport_mock_set_connected(bool connected) {
    s_mock_connected = connected;
}
#endif
