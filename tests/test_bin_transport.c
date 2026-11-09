/**
 * @file test_bin_transport.c
 * @brief Host unit test suite for Bus Pirate binary transport service.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "services/bp_bin_transport.h"

static void test_init_and_stats(void) {
    printf("[TEST] Binary transport initialization and stats...\n");
    bp_status_t st = bp_bin_transport_init();
    assert(st == BP_STATUS_OK);

    bp_bin_transport_stats_t stats;
    bp_bin_transport_get_stats(&stats);
    assert(stats.tx_level == 0);
    assert(stats.tx_free == BP_BIN_TX_RING_SIZE - 1);
    assert(stats.tx_dropped == 0);
    assert(stats.rx_level == 0);
    assert(stats.rx_free == BP_BIN_RX_RING_SIZE - 1);
    assert(stats.rx_dropped == 0);
    assert(stats.tx_packets == 0);
    assert(stats.rx_packets == 0);
    assert(bp_bin_transport_is_connected() == true);
    printf("[PASS] Initialization and stats verified\n");
}

static void test_tx_packet_and_raw_bytes(void) {
    printf("[TEST] TX packet and raw byte transmission...\n");
    bp_bin_transport_init();

    /* 1. Put single bytes */
    assert(bp_bin_transport_putc('B') == BP_STATUS_OK);
    assert(bp_bin_transport_putc('P') == BP_STATUS_OK);

    /* 2. Write multi-byte framed chunk */
    const uint8_t chunk[] = { 0x01, 0x02, 0x03, 0x04, 0x05 };
    uint32_t written = 0;
    assert(bp_bin_transport_write(chunk, sizeof(chunk), &written) == BP_STATUS_OK);
    assert(written == sizeof(chunk));

    bp_bin_transport_stats_t stats;
    bp_bin_transport_get_stats(&stats);
    assert(stats.tx_level == 7);
    assert(stats.tx_free == (BP_BIN_TX_RING_SIZE - 1) - 7);
    assert(stats.tx_packets == 3);

    /* 3. Drain mock TX and verify FIFO order */
    uint8_t out_buf[16];
    uint32_t drained = bp_bin_transport_mock_drain_tx(out_buf, sizeof(out_buf));
    assert(drained == 7);
    assert(out_buf[0] == 'B');
    assert(out_buf[1] == 'P');
    assert(memcmp(&out_buf[2], chunk, sizeof(chunk)) == 0);

    /* Queue should now be empty */
    bp_bin_transport_get_stats(&stats);
    assert(stats.tx_level == 0);
    assert(stats.tx_free == BP_BIN_TX_RING_SIZE - 1);
    printf("[PASS] TX packet and raw byte transmission verified\n");
}

static void test_rx_packet_and_raw_bytes(void) {
    printf("[TEST] RX packet and raw byte reception...\n");
    bp_bin_transport_init();

    /* 1. Inject simulated host data */
    const uint8_t host_cmd[] = { 'S', 'U', 'M', 'P', 0x11, 0x22, 0x33, 0x00 };
    bp_bin_transport_mock_inject_rx(host_cmd, sizeof(host_cmd));

    assert(bp_bin_transport_rx_available() == sizeof(host_cmd));

    /* 2. Read single byte */
    uint8_t b = 0;
    assert(bp_bin_transport_getc(&b) == BP_STATUS_OK);
    assert(b == 'S');

    /* 3. Read chunk */
    uint8_t rx_buf[16];
    uint32_t read_cnt = 0;
    assert(bp_bin_transport_read(rx_buf, sizeof(rx_buf), &read_cnt) == BP_STATUS_OK);
    assert(read_cnt == sizeof(host_cmd) - 1);
    assert(memcmp(rx_buf, &host_cmd[1], sizeof(host_cmd) - 1) == 0);

    /* 4. Queue should now be empty */
    assert(bp_bin_transport_rx_available() == 0);
    assert(bp_bin_transport_getc(&b) == BP_STATUS_QUEUE_EMPTY);
    assert(bp_bin_transport_read(rx_buf, sizeof(rx_buf), &read_cnt) == BP_STATUS_QUEUE_EMPTY);
    printf("[PASS] RX packet and raw byte reception verified\n");
}

static void test_tx_backpressure_and_drops(void) {
    printf("[TEST] TX backpressure drop policy without blocking...\n");
    bp_bin_transport_init();

    /* Fill TX ring to capacity (BP_BIN_TX_RING_SIZE - 1 = 4095 bytes) */
    uint8_t filler[1024];
    memset(filler, 0xAA, sizeof(filler));
    for (int i = 0; i < 3; i++) {
        uint32_t wr = 0;
        assert(bp_bin_transport_write(filler, sizeof(filler), &wr) == BP_STATUS_OK);
        assert(wr == sizeof(filler));
    }
    /* Write 1023 bytes to reach 4095 */
    uint32_t wr_last = 0;
    assert(bp_bin_transport_write(filler, 1023, &wr_last) == BP_STATUS_OK);
    assert(wr_last == 1023);

    bp_bin_transport_stats_t stats;
    bp_bin_transport_get_stats(&stats);
    assert(stats.tx_level == BP_BIN_TX_RING_SIZE - 1);
    assert(stats.tx_free == 0);
    assert(stats.tx_dropped == 0);

    /* Writing more data MUST NOT block and MUST drop excess bytes */
    uint32_t overflow_written = 999;
    bp_status_t st = bp_bin_transport_write(filler, 256, &overflow_written);
    assert(st == BP_STATUS_QUEUE_FULL);
    assert(overflow_written == 0);

    /* Single byte putc MUST also return queue full */
    assert(bp_bin_transport_putc('X') == BP_STATUS_QUEUE_FULL);

    /* Timeout putc MUST return timeout */
    assert(bp_bin_transport_putc_timeout('Y', 5) == BP_STATUS_TIMEOUT);

    bp_bin_transport_get_stats(&stats);
    assert(stats.tx_dropped == 256 + 1 + 1);
    printf("[PASS] TX backpressure drop policy verified (accounted %lu drops)\n", (unsigned long)stats.tx_dropped);
}

static void test_rx_backpressure_and_flush(void) {
    printf("[TEST] RX backpressure and queue flush...\n");
    bp_bin_transport_init();

    /* Inject 2047 bytes (BP_BIN_RX_RING_SIZE - 1) */
    uint8_t dummy[512];
    memset(dummy, 0x55, sizeof(dummy));
    for (int i = 0; i < 3; i++) {
        bp_bin_transport_mock_inject_rx(dummy, sizeof(dummy));
    }
    bp_bin_transport_mock_inject_rx(dummy, 511);

    assert(bp_bin_transport_rx_available() == BP_BIN_RX_RING_SIZE - 1);

    /* Injecting beyond capacity drops without crash */
    bp_bin_transport_mock_inject_rx(dummy, 100);
    bp_bin_transport_stats_t stats;
    bp_bin_transport_get_stats(&stats);
    assert(stats.rx_dropped == 100);

    /* Flush queue */
    bp_bin_transport_rx_flush();
    assert(bp_bin_transport_rx_available() == 0);
    printf("[PASS] RX backpressure and queue flush verified\n");
}

static void test_timeout_and_connection(void) {
    printf("[TEST] Timeout handling and connection state...\n");
    bp_bin_transport_init();

    /* Bounded timeout getc on empty queue */
    uint8_t c = 0;
    assert(bp_bin_transport_getc_timeout(&c, 10) == BP_STATUS_TIMEOUT);

    /* Connection state */
    bp_bin_transport_mock_set_connected(false);
    assert(bp_bin_transport_is_connected() == false);

    bp_bin_transport_mock_set_connected(true);
    assert(bp_bin_transport_is_connected() == true);
    printf("[PASS] Timeout handling and connection state verified\n");
}

int main(void) {
    printf("=== Running Binary Transport Unit Tests ===\n");
    test_init_and_stats();
    test_tx_packet_and_raw_bytes();
    test_rx_packet_and_raw_bytes();
    test_tx_backpressure_and_drops();
    test_rx_backpressure_and_flush();
    test_timeout_and_connection();
    printf("=== ALL BINARY TRANSPORT TESTS PASSED ===\n");
    return 0;
}
