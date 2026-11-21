#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "common/sha256.h"
#include "common/sha256_ref.h"
#include "test.h"

// FIPS 180-4 / NIST CSRC example messages.
#define MSG_448 "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"
#define MSG_896                                                                             \
    "abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmnhijklmnoijklmnopjklmnopqklmnopqr" \
    "lmnopqrsmnopqrstnopqrstu"

static void ref_hex(const void *msg, size_t len, char out[SHA256_HEX_CHARS + 1]) {
    uint8_t digest[SHA256_REF_DIGEST_BYTES];
    sha256_ref(msg, len, digest);
    sha256_bytes_to_hex(digest, out);
}

// Hashes msg with the fast path; returns 0, or -1 if the block cannot be prepared.
static int fast_digest(const void *msg, size_t len, uint8_t out[SHA256_DIGEST_BYTES]) {
    sha256_block blk;
    uint32_t state[SHA256_STATE_WORDS];
    if (sha256_block_init(&blk, msg, len) != 0) {
        return -1;
    }
    sha256_block_compress(&blk, state);
    sha256_state_to_bytes(state, out);
    return 0;
}

// xorshift64*: deterministic, good enough to spread lengths and byte values.
static uint64_t rng_next(uint64_t *s) {
    uint64_t x = *s;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    *s = x;
    return x * 0x2545f4914f6cdd1dULL;
}

// ---------------------------------------------------------------------------------------
// Reference: NIST vectors and streaming
// ---------------------------------------------------------------------------------------

TEST(sha256_ref_empty) {
    char hex[SHA256_HEX_CHARS + 1];
    ref_hex("", 0, hex);
    ASSERT_EQ_STR(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    ref_hex(NULL, 0, hex);
    ASSERT_EQ_STR(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST(sha256_ref_abc) {
    char hex[SHA256_HEX_CHARS + 1];
    ref_hex("abc", 3, hex);
    ASSERT_EQ_STR(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST(sha256_ref_448_bits) {
    char hex[SHA256_HEX_CHARS + 1];
    ASSERT_EQ_INT(strlen(MSG_448), 56);
    ref_hex(MSG_448, strlen(MSG_448), hex);
    ASSERT_EQ_STR(hex, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
}

TEST(sha256_ref_896_bits) {
    char hex[SHA256_HEX_CHARS + 1];
    ASSERT_EQ_INT(strlen(MSG_896), 112);
    ref_hex(MSG_896, strlen(MSG_896), hex);
    ASSERT_EQ_STR(hex, "cf5b16a778af8380036ce59e7b0492370b249b11e8f07a51afac45037afee9d1");
}

TEST(sha256_ref_million_a_streamed) {
    // Uneven piece sizes so updates cross block boundaries at many different offsets.
    static const size_t pieces[] = {1, 3, 55, 56, 63, 64, 65, 127, 1000, 4096};
    char chunk[4096];
    memset(chunk, 'a', sizeof chunk);

    sha256_ref_ctx ctx;
    sha256_ref_init(&ctx);
    size_t left = 1000000;
    for (size_t i = 0; left > 0; i++) {
        size_t n = pieces[i % (sizeof pieces / sizeof pieces[0])];
        if (n > left) {
            n = left;
        }
        sha256_ref_update(&ctx, chunk, n);
        left -= n;
    }
    uint8_t digest[SHA256_REF_DIGEST_BYTES];
    char hex[SHA256_HEX_CHARS + 1];
    sha256_ref_final(&ctx, digest);
    sha256_bytes_to_hex(digest, hex);
    ASSERT_EQ_STR(hex, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(sha256_ref_split_points_match_one_shot) {
    uint8_t msg[200];
    for (size_t i = 0; i < sizeof msg; i++) {
        msg[i] = (uint8_t)(i * 7 + 1);
    }
    for (size_t len = 0; len <= sizeof msg; len += 13) {
        uint8_t whole[SHA256_REF_DIGEST_BYTES];
        sha256_ref(msg, len, whole);
        for (size_t cut1 = 0; cut1 <= len; cut1++) {
            size_t cut2 = cut1 + (len - cut1) / 2;
            uint8_t parts[SHA256_REF_DIGEST_BYTES];
            sha256_ref_ctx ctx;
            sha256_ref_init(&ctx);
            sha256_ref_update(&ctx, msg, cut1);
            sha256_ref_update(&ctx, NULL, 0);
            sha256_ref_update(&ctx, msg + cut1, cut2 - cut1);
            sha256_ref_update(&ctx, msg + cut2, len - cut2);
            sha256_ref_final(&ctx, parts);
            ASSERT_MEM_EQ(parts, whole, sizeof whole);
        }
    }
}

TEST(sha256_ref_padding_boundaries) {
    // 55/56 and 63/64/65 bytes are where padding needs one block, two blocks, or starts a
    // new block. Digests from coreutils sha256sum.
    static const struct {
        size_t len;
        const char *hex;
    } cases[] = {
        {55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
        {56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
        {63, "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"},
        {64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
        {65, "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"},
    };
    char msg[65];
    memset(msg, 'a', sizeof msg);
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        char hex[SHA256_HEX_CHARS + 1];
        ref_hex(msg, cases[i].len, hex);
        ASSERT_EQ_STR(hex, cases[i].hex);
    }
}

// ---------------------------------------------------------------------------------------
// Fast single-block implementation
// ---------------------------------------------------------------------------------------

TEST(sha256_fast_known_vectors) {
    static const struct {
        const char *msg;
        const char *hex;
    } cases[] = {
        {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {"The quick brown fox jumps over the lazy dog",
         "d7a8fbb307d7809469ca9abcb0082e4f8d5651e46d3cdb762d02d0bf37c9e592"},
        {"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
         "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
    };
    for (size_t i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        sha256_block blk;
        uint32_t state[SHA256_STATE_WORDS];
        char hex[SHA256_HEX_CHARS + 1];
        ASSERT_EQ_INT(sha256_block_init(&blk, cases[i].msg, strlen(cases[i].msg)), 0);
        sha256_block_compress(&blk, state);
        sha256_state_to_hex(state, hex);
        ASSERT_EQ_STR(hex, cases[i].hex);
    }
}

TEST(sha256_fast_init_rejects_too_long) {
    uint8_t msg[64] = {0};
    sha256_block blk;
    memset(&blk, 0x5a, sizeof blk);
    ASSERT_EQ_INT(sha256_block_init(&blk, msg, 56), -1);
    ASSERT_EQ_INT(sha256_block_init(&blk, msg, 64), -1);
    ASSERT_EQ_INT(sha256_block_init(&blk, msg, (size_t)-1), -1);
    // A rejected call must not leave a half-written block behind.
    for (size_t i = 0; i < sizeof blk.bytes; i++) {
        ASSERT_EQ_INT(blk.bytes[i], 0x5a);
    }
    ASSERT_EQ_INT(sha256_block_init(&blk, msg, 55), 0);
    ASSERT_EQ_INT(sha256_block_init(&blk, NULL, 0), 0);
}

TEST(sha256_fast_block_layout) {
    sha256_block blk;
    ASSERT_EQ_INT(sha256_block_init(&blk, "abc", 3), 0);
    ASSERT_EQ_INT(blk.len, 3);
    ASSERT_MEM_EQ(blk.bytes, "abc", 3);
    ASSERT_EQ_INT(blk.bytes[3], 0x80);
    for (size_t i = 4; i < 62; i++) {
        ASSERT_EQ_INT(blk.bytes[i], 0);
    }
    ASSERT_EQ_INT(blk.bytes[62], 0x00);
    ASSERT_EQ_INT(blk.bytes[63], 0x18);  // 24 bits

    uint8_t msg[55];
    memset(msg, 'x', sizeof msg);
    ASSERT_EQ_INT(sha256_block_init(&blk, msg, 55), 0);
    ASSERT_EQ_INT(blk.bytes[55], 0x80);
    for (size_t i = 56; i < 62; i++) {
        ASSERT_EQ_INT(blk.bytes[i], 0);
    }
    ASSERT_EQ_INT(blk.bytes[62], 0x01);  // 440 bits = 0x01b8
    ASSERT_EQ_INT(blk.bytes[63], 0xb8);
}

TEST(sha256_fast_matches_ref_every_length) {
    enum { PATTERNS = 7 };
    uint8_t msg[SHA256_MAX_MSG_BYTES];
    for (int p = 0; p < PATTERNS; p++) {
        for (size_t i = 0; i < sizeof msg; i++) {
            switch (p) {
            case 0: msg[i] = 0x00; break;
            case 1: msg[i] = 0xff; break;
            case 2: msg[i] = 'a'; break;
            case 3: msg[i] = (uint8_t)i; break;
            case 4: msg[i] = (uint8_t)(255 - i); break;
            case 5: msg[i] = (uint8_t)('0' + i % 10); break;
            default: msg[i] = (uint8_t)(i * 37 + 11); break;
            }
        }
        for (size_t len = 0; len <= SHA256_MAX_MSG_BYTES; len++) {
            uint8_t fast[SHA256_DIGEST_BYTES];
            uint8_t ref[SHA256_REF_DIGEST_BYTES];
            ASSERT_EQ_INT(fast_digest(msg, len, fast), 0);
            sha256_ref(msg, len, ref);
            ASSERT_MEM_EQ(fast, ref, sizeof ref);
        }
    }
}

TEST(sha256_fast_in_place_odometer) {
    // The hot-loop usage: prepare once, then rewrite message bytes and compress again.
    char word[] = "berejlab-0000";
    size_t len = strlen(word);
    sha256_block blk;
    ASSERT_EQ_INT(sha256_block_init(&blk, word, len), 0);
    for (int n = 0; n < 10000; n++) {
        snprintf(word + 9, 5, "%04d", n);
        memcpy(blk.bytes + 9, word + 9, 4);

        uint32_t state[SHA256_STATE_WORDS];
        uint8_t fast[SHA256_DIGEST_BYTES];
        uint8_t ref[SHA256_REF_DIGEST_BYTES];
        sha256_block_compress(&blk, state);
        sha256_state_to_bytes(state, fast);
        sha256_ref(word, len, ref);
        ASSERT_MEM_EQ(fast, ref, sizeof ref);
    }
}

TEST(sha256_fast_matches_ref_1m_random) {
    uint64_t seed = 0x9e3779b97f4a7c15ULL;
    uint8_t msg[SHA256_MAX_MSG_BYTES + 8];
    for (long iter = 0; iter < 1000000; iter++) {
        size_t len = (size_t)(rng_next(&seed) % (SHA256_MAX_MSG_BYTES + 1));
        for (size_t i = 0; i < len; i += 8) {
            uint64_t r = rng_next(&seed);
            memcpy(msg + i, &r, sizeof r);
        }
        uint8_t fast[SHA256_DIGEST_BYTES];
        uint8_t ref[SHA256_REF_DIGEST_BYTES];
        ASSERT_EQ_INT(fast_digest(msg, len, fast), 0);
        sha256_ref(msg, len, ref);
        if (memcmp(fast, ref, sizeof ref) != 0) {
            fprintf(stderr, "    mismatch at iteration %ld, length %zu\n", iter, len);
            ASSERT_MEM_EQ(fast, ref, sizeof ref);
        }
    }
}

// ---------------------------------------------------------------------------------------
// Byte order and hex helpers
// ---------------------------------------------------------------------------------------

TEST(sha256_state_to_bytes_is_big_endian) {
    static const uint32_t state[SHA256_STATE_WORDS] = {
        0x01234567, 0x89abcdef, 0xdeadbeef, 0x00000000,
        0xffffffff, 0x80000001, 0x7f00ff00, 0x0a0b0c0d,
    };
    static const uint8_t expected[SHA256_DIGEST_BYTES] = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef, 0xde, 0xad, 0xbe,
        0xef, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0x80, 0x00,
        0x00, 0x01, 0x7f, 0x00, 0xff, 0x00, 0x0a, 0x0b, 0x0c, 0x0d,
    };
    uint8_t bytes[SHA256_DIGEST_BYTES];
    sha256_state_to_bytes(state, bytes);
    ASSERT_MEM_EQ(bytes, expected, sizeof expected);

    char hex[SHA256_HEX_CHARS + 1];
    sha256_state_to_hex(state, hex);
    ASSERT_EQ_STR(hex, "0123456789abcdefdeadbeef00000000ffffffff800000017f00ff000a0b0c0d");
}

TEST(sha256_bytes_to_hex_every_byte_value) {
    // Eight digests cover all 256 byte values; compare against printf's %02x.
    for (int base = 0; base < 256; base += SHA256_DIGEST_BYTES) {
        uint8_t bytes[SHA256_DIGEST_BYTES];
        char expected[SHA256_HEX_CHARS + 1];
        for (int i = 0; i < SHA256_DIGEST_BYTES; i++) {
            bytes[i] = (uint8_t)(base + i);
            snprintf(expected + 2 * i, 3, "%02x", (unsigned)bytes[i]);
        }
        char hex[SHA256_HEX_CHARS + 2];
        memset(hex, '#', sizeof hex);
        sha256_bytes_to_hex(bytes, hex);
        ASSERT_EQ_STR(hex, expected);
        ASSERT_EQ_INT(hex[SHA256_HEX_CHARS + 1], '#');  // nothing written past the NUL
    }
}

TEST(sha256_raw_words_match_digest_prefix) {
    // Detectors read raw words (SPEC §5.6): out[0] >> 24 must be the first digest byte.
    sha256_block blk;
    uint32_t state[SHA256_STATE_WORDS];
    ASSERT_EQ_INT(sha256_block_init(&blk, "abc", 3), 0);
    sha256_block_compress(&blk, state);
    ASSERT_EQ_INT(state[0], 0xba7816bf);
    ASSERT_EQ_INT(state[7], 0xf20015ad);
    ASSERT_EQ_INT(state[0] >> 24, 0xba);
}

TEST(sha256_selftest_passes) {
    ASSERT_TRUE(sha256_selftest());
}
