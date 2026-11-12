#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "pico/stdlib.h"
#include "pirate.h"
#include "system_config.h"
#include "command_struct.h"
#include "ui/ui_term.h"
#include "ui/ui_help.h"
#include "lib/bp_args/bp_cmd.h"
#include "fatfs/ff.h"
#include "pirate/storage.h"
#include "pirate/file.h"
#include "commands/global/cmd_sha256.h"

#if !defined(BP_HOST_TEST)
#include "pico/sha256.h"
#else
// Standalone software SHA-256 for host unit tests
typedef struct {
    uint32_t state[8];
    uint64_t count;
    uint8_t buffer[64];
} host_sha256_ctx_t;

static inline uint32_t ror32(uint32_t val, uint32_t shift) {
    return (val >> shift) | (val << (32 - shift));
}

static const uint32_t K256[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
    0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
    0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
    0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
    0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2
};

static void host_sha256_transform(uint32_t state[8], const uint8_t data[64]) {
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    uint32_t w[64];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)data[i*4] << 24) | ((uint32_t)data[i*4+1] << 16) |
               ((uint32_t)data[i*4+2] << 8) | ((uint32_t)data[i*4+3]);
    }
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ror32(w[i-15], 7) ^ ror32(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = ror32(w[i-2], 17) ^ ror32(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    for (int i = 0; i < 64; i++) {
        uint32_t S1 = ror32(e, 6) ^ ror32(e, 11) ^ ror32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + S1 + ch + K256[i] + w[i];
        uint32_t S0 = ror32(a, 2) ^ ror32(a, 13) ^ ror32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;
        h = g; g = f; f = e; e = d + temp1;
        d = c; c = b; b = a; a = temp1 + temp2;
    }
    state[0] += a; state[1] += b; state[2] += c; state[3] += d;
    state[4] += e; state[5] += f; state[6] += g; state[7] += h;
}

static void host_sha256_init(host_sha256_ctx_t* ctx) {
    ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
    ctx->count = 0;
}

static void host_sha256_update(host_sha256_ctx_t* ctx, const uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        ctx->buffer[ctx->count % 64] = data[i];
        ctx->count++;
        if ((ctx->count % 64) == 0) {
            host_sha256_transform(ctx->state, ctx->buffer);
        }
    }
}

static void host_sha256_final(host_sha256_ctx_t* ctx, uint8_t hash[32]) {
    uint64_t total_bits = ctx->count * 8;
    ctx->buffer[ctx->count % 64] = 0x80;
    ctx->count++;
    if ((ctx->count % 64) > 56 || (ctx->count % 64) == 0) {
        while ((ctx->count % 64) != 0) {
            ctx->buffer[ctx->count % 64] = 0;
            ctx->count++;
        }
        host_sha256_transform(ctx->state, ctx->buffer);
    }
    while ((ctx->count % 64) != 56) {
        ctx->buffer[ctx->count % 64] = 0;
        ctx->count++;
    }
    for (int i = 7; i >= 0; i--) {
        ctx->buffer[56 + (7 - i)] = (uint8_t)(total_bits >> (i * 8));
    }
    host_sha256_transform(ctx->state, ctx->buffer);
    for (int i = 0; i < 8; i++) {
        hash[i*4]   = (uint8_t)(ctx->state[i] >> 24);
        hash[i*4+1] = (uint8_t)(ctx->state[i] >> 16);
        hash[i*4+2] = (uint8_t)(ctx->state[i] >> 8);
        hash[i*4+3] = (uint8_t)(ctx->state[i]);
    }
}
#endif

static const char* const usage[] = {
    "sha256 <filename>",
    "Calculate SHA-256 hash of a file on TF card:%s sha256 firmware.bin",
};

static const bp_command_positional_t sha256_positionals[] = {
    { "filename", NULL, 0x00, true },
    { 0 }
};

const bp_command_def_t cmd_sha256_def = {
    .name         = "sha256",
    .description  = 0x00,
    .positionals  = sha256_positionals,
    .usage        = usage,
    .usage_count  = count_of(usage),
};

void cmd_sha256_handler(struct command_result* res) {
    if (bp_cmd_help_check(&cmd_sha256_def, res->help_flag)) {
        return;
    }

    char location[64];
    if (!bp_file_get_name_positional(&cmd_sha256_def, 1, location, sizeof(location))) {
        printf("%sError:%s Missing filename. Usage: sha256 <filename>\r\n",
               ui_term_color_error(), ui_term_color_reset());
        res->error = true;
        return;
    }

    FIL fil;
    FRESULT fr = f_open(&fil, location, FA_READ);
    if (fr != FR_OK) {
        storage_file_error(fr);
        res->error = true;
        return;
    }

    uint8_t chunk_buf[512];
    UINT bytes_read = 0;
    uint8_t hash[32];

#if !defined(BP_HOST_TEST)
    pico_sha256_state_t state;
    if (pico_sha256_try_start(&state, SHA256_BIG_ENDIAN, false) != PICO_OK) {
        printf("%sError:%s SHA-256 hardware accelerator busy\r\n",
               ui_term_color_error(), ui_term_color_reset());
        f_close(&fil);
        res->error = true;
        return;
    }

    while ((fr = f_read(&fil, chunk_buf, sizeof(chunk_buf), &bytes_read)) == FR_OK && bytes_read > 0) {
        pico_sha256_update(&state, chunk_buf, bytes_read);
    }

    sha256_result_t result;
    pico_sha256_finish(&state, &result);
    memcpy(hash, result.bytes, 32);
#else
    host_sha256_ctx_t ctx;
    host_sha256_init(&ctx);
    while ((fr = f_read(&fil, chunk_buf, sizeof(chunk_buf), &bytes_read)) == FR_OK && bytes_read > 0) {
        host_sha256_update(&ctx, chunk_buf, bytes_read);
    }
    host_sha256_final(&ctx, hash);
#endif

    f_close(&fil);

    if (fr != FR_OK) {
        storage_file_error(fr);
        res->error = true;
        return;
    }

    // Print standard sha256sum formatted output
    for (int i = 0; i < 32; i++) {
        printf("%02x", hash[i]);
    }
    printf("  %s\r\n", location);
}
