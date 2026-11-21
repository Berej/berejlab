// Reference SHA-256, a direct transcription of FIPS 180-4 §4.1.2, §5.1.1, §6.2.
// Keep it this way: readability over speed (CLAUDE.md rule 7).

#include "common/sha256_ref.h"

#include <string.h>

static const uint32_t k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
    0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
    0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
    0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
    0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
    0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
    0xc67178f2,
};

static const uint32_t h_init[8] = {
    0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
    0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
};

static uint32_t rotr(uint32_t x, unsigned n) {
    return (x >> n) | (x << (32 - n));
}

static uint32_t ch(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (~x & z);
}

static uint32_t maj(uint32_t x, uint32_t y, uint32_t z) {
    return (x & y) ^ (x & z) ^ (y & z);
}

static uint32_t big_sigma0(uint32_t x) {
    return rotr(x, 2) ^ rotr(x, 13) ^ rotr(x, 22);
}

static uint32_t big_sigma1(uint32_t x) {
    return rotr(x, 6) ^ rotr(x, 11) ^ rotr(x, 25);
}

static uint32_t small_sigma0(uint32_t x) {
    return rotr(x, 7) ^ rotr(x, 18) ^ (x >> 3);
}

static uint32_t small_sigma1(uint32_t x) {
    return rotr(x, 17) ^ rotr(x, 19) ^ (x >> 10);
}

// Explicit shifts make the byte order independent of the host's endianness.
static uint32_t load_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

static void store_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

static void compress(uint32_t h[8], const uint8_t block[64]) {
    uint32_t w[64];
    for (int t = 0; t < 16; t++) {
        w[t] = load_be32(block + 4 * t);
    }
    for (int t = 16; t < 64; t++) {
        w[t] = small_sigma1(w[t - 2]) + w[t - 7] + small_sigma0(w[t - 15]) + w[t - 16];
    }

    uint32_t a = h[0], b = h[1], c = h[2], d = h[3];
    uint32_t e = h[4], f = h[5], g = h[6], hh = h[7];

    for (int t = 0; t < 64; t++) {
        uint32_t t1 = hh + big_sigma1(e) + ch(e, f, g) + k[t] + w[t];
        uint32_t t2 = big_sigma0(a) + maj(a, b, c);
        hh = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }

    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
    h[5] += f;
    h[6] += g;
    h[7] += hh;
}

void sha256_ref_init(sha256_ref_ctx *ctx) {
    memcpy(ctx->h, h_init, sizeof ctx->h);
    ctx->total_len = 0;
    ctx->buf_len = 0;
}

void sha256_ref_update(sha256_ref_ctx *ctx, const void *data, size_t len) {
    const uint8_t *p = data;
    ctx->total_len += len;
    while (len > 0) {
        size_t take = sizeof ctx->buf - ctx->buf_len;
        if (take > len) {
            take = len;
        }
        memcpy(ctx->buf + ctx->buf_len, p, take);
        ctx->buf_len += take;
        p += take;
        len -= take;
        if (ctx->buf_len == sizeof ctx->buf) {
            compress(ctx->h, ctx->buf);
            ctx->buf_len = 0;
        }
    }
}

void sha256_ref_final(sha256_ref_ctx *ctx, uint8_t out[SHA256_REF_DIGEST_BYTES]) {
    // FIPS 180-4 §5.1.1: append bit '1', zeros, then the 64-bit message length in bits.
    // Messages of 2^61 bytes or more are not supported (the length would wrap).
    uint64_t bit_len = ctx->total_len * 8;

    ctx->buf[ctx->buf_len++] = 0x80;
    if (ctx->buf_len > 56) {
        memset(ctx->buf + ctx->buf_len, 0, sizeof ctx->buf - ctx->buf_len);
        compress(ctx->h, ctx->buf);
        ctx->buf_len = 0;
    }
    memset(ctx->buf + ctx->buf_len, 0, 56 - ctx->buf_len);
    store_be32(ctx->buf + 56, (uint32_t)(bit_len >> 32));
    store_be32(ctx->buf + 60, (uint32_t)bit_len);
    compress(ctx->h, ctx->buf);

    for (int i = 0; i < 8; i++) {
        store_be32(out + 4 * i, ctx->h[i]);
    }
}

void sha256_ref(const void *data, size_t len, uint8_t out[SHA256_REF_DIGEST_BYTES]) {
    sha256_ref_ctx ctx;
    sha256_ref_init(&ctx);
    sha256_ref_update(&ctx, data, len);
    sha256_ref_final(&ctx, out);
}
