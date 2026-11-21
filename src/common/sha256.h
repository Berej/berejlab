#ifndef BEREJLAB_COMMON_SHA256_H
#define BEREJLAB_COMMON_SHA256_H

// Fast single-block SHA-256 for the worker hot loop (SPEC §11.3, speed-ups 1 and 2).
//
// Every word is at most 55 bytes, so it fits one 64-byte block together with its padding and
// length. The block is prepared once per word length; the caller then rewrites message bytes
// in place (e.g. an odometer increment) and calls sha256_block_compress(), which yields the
// raw state words. Detectors work on those words directly (SPEC §5.6); conversion to bytes or
// hex happens only outside the hot path.
//
// Byte order of the result: out[0] holds digest bytes 0..3 big-endian, so the first digest
// byte (the first two hex characters) is out[0] >> 24.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SHA256_BLOCK_BYTES   64
#define SHA256_MAX_MSG_BYTES 55  // longest message that fits one block with its padding
#define SHA256_DIGEST_BYTES  32
#define SHA256_STATE_WORDS   8
#define SHA256_HEX_CHARS     64  // without the terminating NUL

typedef struct {
    // bytes[0, len) is the message, the caller may rewrite it in place between compress
    // calls. bytes[len, 64) holds the padding and length field and must not be touched.
    uint8_t bytes[SHA256_BLOCK_BYTES];
    size_t len;
} sha256_block;

// Copies msg (len bytes, may be NULL when len is 0) and writes padding and length.
// Returns 0, or -1 if len > SHA256_MAX_MSG_BYTES (the block is then left unchanged).
int sha256_block_init(sha256_block *blk, const void *msg, size_t len);

// Hashes the block; writes the final state H(1) in host-order words.
void sha256_block_compress(const sha256_block *blk, uint32_t out[SHA256_STATE_WORDS]);

// Helpers for use outside the hot path.
void sha256_state_to_bytes(const uint32_t state[SHA256_STATE_WORDS],
                           uint8_t out[SHA256_DIGEST_BYTES]);
// Writes 64 lowercase hex characters plus a terminating NUL.
void sha256_bytes_to_hex(const uint8_t digest[SHA256_DIGEST_BYTES],
                         char out[SHA256_HEX_CHARS + 1]);
void sha256_state_to_hex(const uint32_t state[SHA256_STATE_WORDS],
                         char out[SHA256_HEX_CHARS + 1]);

// Checks the reference and the fast implementation against built-in vectors and against
// each other. The daemon must refuse to enumerate when this fails (SPEC §11.4).
bool sha256_selftest(void);

#endif
