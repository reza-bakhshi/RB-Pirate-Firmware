/* Exercise actual generated BPIO builders with embedded fixed-block allocation. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "services/bp_flatcc_memory.h"
#include "bpio_builder.h"
#include "bpio_verifier.h"

static unsigned mock_core;
unsigned get_core_num(void) { return mock_core; }
static void test_pool(void) {
    mock_core = 1;
    assert(bp_flatcc_alloc(1) == NULL);
    assert(bp_flatcc_live_blocks() == 0);
    mock_core = 0;
    void *blocks[14];
    for (unsigned i = 0; i < 14; i++) { blocks[i] = bp_flatcc_alloc(128); assert(blocks[i]); }
    assert(!bp_flatcc_alloc(1));
    memset(blocks[0], 0xa5, 128);
    assert(!bp_flatcc_realloc(blocks[0], 4097));
    assert(((uint8_t*)blocks[0])[127] == 0xa5);
    assert(!bp_flatcc_calloc(SIZE_MAX, 2));
    for (unsigned i = 0; i < 14; i++) bp_flatcc_free(blocks[i]);
    assert(bp_flatcc_live_blocks() == 0);
    for (size_t alignment = sizeof(void*); alignment <= 512; alignment *= 2) {
        void *aligned = bp_flatcc_aligned_alloc(alignment, 256);
        assert(aligned && !((uintptr_t)aligned & (alignment - 1)));
        memset(aligned, 0xa5, 256);
        bp_flatcc_aligned_free(aligned);
    }
    assert(!bp_flatcc_aligned_alloc(3, 10));
    assert(!bp_flatcc_aligned_alloc(16, SIZE_MAX));
    uint8_t *p = bp_flatcc_alloc(64); assert(p); memset(p, 0x55, 64);
    p = bp_flatcc_realloc(p, 600); assert(p && p[63] == 0x55);
    bp_flatcc_free(p);
}
static void test_bpio(void) {
    flatcc_builder_t builder;
    flatcc_builder_t *B = &builder;
    assert(flatcc_builder_init(B) == 0);
    uint32_t baseline = bp_flatcc_allocation_failures();
    for (unsigned cycle = 0; cycle < 1000; cycle++) {
        assert(flatcc_builder_reset(B) == 0);
        bpio_DataResponse_start(B);
        bpio_DataResponse_data_read_start(B);
        uint8_t *data = bpio_DataResponse_data_read_extend(B, 512); assert(data);
        memset(data, (uint8_t)cycle, 512);
        bpio_DataResponse_data_read_end(B);
        bpio_DataResponse_ref_t response = bpio_DataResponse_end(B); assert(response);
        bpio_ResponsePacket_start_as_root(B);
        bpio_ResponsePacket_contents_DataResponse_add(B, response);
        assert(bpio_ResponsePacket_end_as_root(B));
        _Alignas(16) uint8_t packet[640];
        size_t size = flatcc_builder_get_buffer_size(B); assert(size <= sizeof(packet));
        assert(flatcc_builder_copy_buffer(B, packet, sizeof(packet)));
        assert(bpio_ResponsePacket_verify_as_root(packet, size) == 0);
        assert(bp_flatcc_allocation_failures() == baseline);
    }
    size_t aligned_size;
    void *aligned_packet = flatcc_builder_finalize_aligned_buffer(B, &aligned_size);
    assert(aligned_packet && bpio_ResponsePacket_verify_as_root(aligned_packet, aligned_size) == 0);
    flatcc_builder_aligned_free(aligned_packet);
    flatcc_builder_clear(B);
    assert(bp_flatcc_live_blocks() == 0);
    puts("[PASS] 1000 maximum-data BPIO response builds/resets without heap or leaks");
}
static void test_status_and_exhaustion(void) {
    flatcc_builder_t builder, *B = &builder;
    assert(flatcc_builder_init(B) == 0);
    uint32_t failures = bp_flatcc_allocation_failures();
    for (unsigned cycle = 0; cycle < 100; cycle++) {
        assert(flatcc_builder_reset(B) == 0);
        bpio_StatusResponse_start(B);
        bpio_StatusResponse_version_firmware_git_hash_add(B, flatbuffers_string_create_str(B, "0123456789abcdef"));
        bpio_StatusResponse_version_firmware_date_add(B, flatbuffers_string_create_str(B, "Nov 11 2026 00:00:00"));
        bpio_StatusResponse_mode_current_add(B, flatbuffers_string_create_str(B, "INFRARED"));
        bpio_StatusResponse_modes_available_start(B);
        for (unsigned i = 0; i < 25; i++) {
            bpio_StatusResponse_modes_available_push(B, flatbuffers_string_create_str(B, "PROTOCOL"));
        }
        bpio_StatusResponse_modes_available_end(B);
        bpio_StatusResponse_mode_pin_labels_start(B);
        for (unsigned i = 0; i < 8; i++) bpio_StatusResponse_mode_pin_labels_push(B, flatbuffers_string_create_str(B, "LABEL"));
        bpio_StatusResponse_mode_pin_labels_end(B);
        bpio_StatusResponse_ref_t response = bpio_StatusResponse_end(B); assert(response);
        bpio_ResponsePacket_start_as_root(B);
        bpio_ResponsePacket_contents_StatusResponse_add(B, response);
        assert(bpio_ResponsePacket_end_as_root(B));
        _Alignas(16) uint8_t packet[2048];
        size_t size = flatcc_builder_get_buffer_size(B); assert(size <= sizeof(packet));
        assert(flatcc_builder_copy_buffer(B, packet, sizeof(packet)));
        assert(bpio_ResponsePacket_verify_as_root(packet, size) == 0);
        assert(bp_flatcc_allocation_failures() == failures);
    }
    flatcc_builder_clear(B);
    assert(bp_flatcc_live_blocks() == 0);
    assert(flatcc_builder_init(B) == 0);
    bpio_DataResponse_start(B);
    bpio_DataResponse_data_read_start(B);
    assert(bpio_DataResponse_data_read_extend(B, 32768) == NULL);
    flatcc_builder_clear(B);
    assert(bp_flatcc_live_blocks() == 0);
    puts("[PASS] Status vectors, repeated reinitialization, and oversized builder failure without leaks");
}

int main(void) { test_pool(); test_bpio(); test_status_and_exhaustion(); return 0; }
