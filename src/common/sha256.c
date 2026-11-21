// Fast single-block SHA-256. Portable C17: fully unrolled rounds, no intrinsics.
// Round precomputation and NEON belong to stage 7 (SPEC §12.2).

#include "common/sha256.h"

#include <string.h>

#include "common/sha256_ref.h"

static const uint32_t K[64] = {
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

#define H0 0x6a09e667u
#define H1 0xbb67ae85u
#define H2 0x3c6ef372u
#define H3 0xa54ff53au
#define H4 0x510e527fu
#define H5 0x9b05688cu
#define H6 0x1f83d9abu
#define H7 0x5be0cd19u

#define ROTR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))
// Ch and Maj in their reduced forms: one operation fewer each than the FIPS definitions.
#define CH(e, f, g)  ((g) ^ ((e) & ((f) ^ (g))))
#define MAJ(a, b, c) ((b) ^ (((a) ^ (b)) & ((b) ^ (c))))
#define BSIG0(x) (ROTR(x, 2) ^ ROTR(x, 13) ^ ROTR(x, 22))
#define BSIG1(x) (ROTR(x, 6) ^ ROTR(x, 11) ^ ROTR(x, 25))
#define SSIG0(x) (ROTR(x, 7) ^ ROTR(x, 18) ^ ((x) >> 3))
#define SSIG1(x) (ROTR(x, 17) ^ ROTR(x, 19) ^ ((x) >> 10))

// Explicit shifts keep this endian-neutral; gcc and clang turn them into load + bswap/rev.
static inline uint32_t load_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

// Rolling 16-word window: the schedule word for round i >= 16 is computed in place of
// w[i - 16] just before it is consumed, so the live schedule state stays at 16 words.
// Measured against a precomputed w[64] (docs/bench.md): equal with gcc, ~12% faster with
// clang on x86_64.
#define W(i) (w[(i) & 15])
#define SCHED(i)                                                                            \
    (w[(i) & 15] += SSIG1(w[((i) - 2) & 15]) + w[((i) - 7) & 15] + SSIG0(w[((i) - 15) & 15]))

// One round with the working variables renamed instead of shifted: only d and h change.
#define ROUND_BODY(a, b, c, d, e, f, g, h, i, wv)                                           \
    do {                                                                                    \
        uint32_t t1_ = (h) + BSIG1(e) + CH(e, f, g) + K[i] + (wv);                          \
        uint32_t t2_ = BSIG0(a) + MAJ(a, b, c);                                             \
        (d) += t1_;                                                                         \
        (h) = t1_ + t2_;                                                                    \
    } while (0)

#define R(a, b, c, d, e, f, g, h, i) ROUND_BODY(a, b, c, d, e, f, g, h, i, W(i))
#define RS(a, b, c, d, e, f, g, h, i) ROUND_BODY(a, b, c, d, e, f, g, h, i, SCHED(i))

#define ROUNDS8(RX, i)                                                                      \
    RX(a, b, c, d, e, f, g, h, (i) + 0);                                                    \
    RX(h, a, b, c, d, e, f, g, (i) + 1);                                                    \
    RX(g, h, a, b, c, d, e, f, (i) + 2);                                                    \
    RX(f, g, h, a, b, c, d, e, (i) + 3);                                                    \
    RX(e, f, g, h, a, b, c, d, (i) + 4);                                                    \
    RX(d, e, f, g, h, a, b, c, (i) + 5);                                                    \
    RX(c, d, e, f, g, h, a, b, (i) + 6);                                                    \
    RX(b, c, d, e, f, g, h, a, (i) + 7)

int sha256_block_init(sha256_block *blk, const void *msg, size_t len) {
    if (len > SHA256_MAX_MSG_BYTES) {
        return -1;
    }
    memset(blk->bytes, 0, sizeof blk->bytes);
    if (len > 0) {
        memcpy(blk->bytes, msg, len);
    }
    blk->bytes[len] = 0x80;
    // Length in bits, big-endian; len <= 55 so it fits in the last two bytes.
    uint32_t bits = (uint32_t)len * 8;
    blk->bytes[62] = (uint8_t)(bits >> 8);
    blk->bytes[63] = (uint8_t)bits;
    blk->len = len;
    return 0;
}

void sha256_block_compress(const sha256_block *blk, uint32_t out[SHA256_STATE_WORDS]) {
    uint32_t w[16];
    for (int i = 0; i < 16; i++) {
        w[i] = load_be32(blk->bytes + 4 * i);
    }

    uint32_t a = H0, b = H1, c = H2, d = H3, e = H4, f = H5, g = H6, h = H7;

    ROUNDS8(R, 0);
    ROUNDS8(R, 8);
    ROUNDS8(RS, 16);
    ROUNDS8(RS, 24);
    ROUNDS8(RS, 32);
    ROUNDS8(RS, 40);
    ROUNDS8(RS, 48);
    ROUNDS8(RS, 56);

    out[0] = a + H0;
    out[1] = b + H1;
    out[2] = c + H2;
    out[3] = d + H3;
    out[4] = e + H4;
    out[5] = f + H5;
    out[6] = g + H6;
    out[7] = h + H7;
}

void sha256_state_to_bytes(const uint32_t state[SHA256_STATE_WORDS],
                           uint8_t out[SHA256_DIGEST_BYTES]) {
    for (int i = 0; i < SHA256_STATE_WORDS; i++) {
        out[4 * i + 0] = (uint8_t)(state[i] >> 24);
        out[4 * i + 1] = (uint8_t)(state[i] >> 16);
        out[4 * i + 2] = (uint8_t)(state[i] >> 8);
        out[4 * i + 3] = (uint8_t)state[i];
    }
}

void sha256_bytes_to_hex(const uint8_t digest[SHA256_DIGEST_BYTES],
                         char out[SHA256_HEX_CHARS + 1]) {
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < SHA256_DIGEST_BYTES; i++) {
        out[2 * i] = hex[digest[i] >> 4];
        out[2 * i + 1] = hex[digest[i] & 0x0f];
    }
    out[SHA256_HEX_CHARS] = '\0';
}

void sha256_state_to_hex(const uint32_t state[SHA256_STATE_WORDS],
                         char out[SHA256_HEX_CHARS + 1]) {
    uint8_t digest[SHA256_DIGEST_BYTES];
    sha256_state_to_bytes(state, digest);
    sha256_bytes_to_hex(digest, out);
}

// ---------------------------------------------------------------------------------------
// Self-test
// ---------------------------------------------------------------------------------------

typedef struct {
    const char *msg;
    const char *hex;
} selftest_vector;

// FIPS 180-4 / NIST CSRC examples plus single-block boundary cases (lengths 43 and 55)
// whose digests were taken from coreutils sha256sum.
static const selftest_vector vectors[] = {
    {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
    {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
    {"The quick brown fox jumps over the lazy dog",
     "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592"},
    {"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
     "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
    {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
     "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
    {"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqr"
     "lmnopqrsmnopqrstnopqrstu",
     "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1"},
};

static bool selftest_vector_ok(const selftest_vector *v) {
    size_t len = strlen(v->msg);
    uint8_t digest[SHA256_DIGEST_BYTES];
    char hex[SHA256_HEX_CHARS + 1];

    sha256_ref(v->msg, len, digest);
    sha256_bytes_to_hex(digest, hex);
    if (strcmp(hex, v->hex) != 0) {
        return false;
    }

    if (len > SHA256_MAX_MSG_BYTES) {
        return true;  // multi-block vectors exercise the reference only
    }
    sha256_block blk;
    uint32_t state[SHA256_STATE_WORDS];
    if (sha256_block_init(&blk, v->msg, len) != 0) {
        return false;
    }
    sha256_block_compress(&blk, state);
    sha256_state_to_hex(state, hex);
    return strcmp(hex, v->hex) == 0;
}

bool sha256_selftest(void) {
    for (size_t i = 0; i < sizeof vectors / sizeof vectors[0]; i++) {
        if (!selftest_vector_ok(&vectors[i])) {
            return false;
        }
    }

    // Cover every single-block length, which is cheap and catches padding mistakes that
    // fixed vectors could miss.
    uint8_t msg[SHA256_MAX_MSG_BYTES];
    for (size_t i = 0; i < sizeof msg; i++) {
        msg[i] = (uint8_t)(i * 131 + 7);
    }
    for (size_t len = 0; len <= SHA256_MAX_MSG_BYTES; len++) {
        sha256_block blk;
        uint32_t state[SHA256_STATE_WORDS];
        uint8_t fast[SHA256_DIGEST_BYTES];
        uint8_t ref[SHA256_DIGEST_BYTES];
        if (sha256_block_init(&blk, msg, len) != 0) {
            return false;
        }
        sha256_block_compress(&blk, state);
        sha256_state_to_bytes(state, fast);
        sha256_ref(msg, len, ref);
        if (memcmp(fast, ref, sizeof fast) != 0) {
            return false;
        }
    }
    return true;
}
