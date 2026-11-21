#ifndef BEREJLAB_COMMON_SHA256_REF_H
#define BEREJLAB_COMMON_SHA256_REF_H

// Reference SHA-256 (FIPS 180-4), streaming, any input length.
//
// Deliberately simple and unoptimized (CLAUDE.md rule 7): it is the yardstick every fast
// implementation is checked against, and the daemon re-verifies every find with it before
// writing it (SPEC §11.4).

#include <stddef.h>
#include <stdint.h>

#define SHA256_REF_DIGEST_BYTES 32

typedef struct {
    uint32_t h[8];       // intermediate hash value H(i)
    uint64_t total_len;  // message bytes consumed so far
    uint8_t buf[64];     // pending bytes of the current, not yet full block
    size_t buf_len;
} sha256_ref_ctx;

void sha256_ref_init(sha256_ref_ctx *ctx);
// data may be NULL when len is 0.
void sha256_ref_update(sha256_ref_ctx *ctx, const void *data, size_t len);
// Writes the digest; the context must be re-initialised before reuse.
void sha256_ref_final(sha256_ref_ctx *ctx, uint8_t out[SHA256_REF_DIGEST_BYTES]);

// One-shot convenience wrapper.
void sha256_ref(const void *data, size_t len, uint8_t out[SHA256_REF_DIGEST_BYTES]);

#endif
