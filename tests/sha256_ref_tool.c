// sha256_ref_tool: prints reference SHA-256 digests in sha256sum's output format, so
// tests/verify_sha256.sh can diff the two outputs directly. Test helper, never installed.
//
//   sha256_ref_tool [FILE...]     no FILE: read standard input (shown as "-")

#include <stdio.h>
#include <string.h>

#include "common/sha256.h"
#include "common/sha256_ref.h"

static int hash_stream(FILE *in, const char *name) {
    sha256_ref_ctx ctx;
    sha256_ref_init(&ctx);
    // Odd buffer size so reads do not line up with 64-byte blocks.
    unsigned char buf[4093];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) {
        sha256_ref_update(&ctx, buf, n);
    }
    if (ferror(in)) {
        fprintf(stderr, "sha256_ref_tool: %s: read error\n", name);
        return -1;
    }
    uint8_t digest[SHA256_REF_DIGEST_BYTES];
    char hex[SHA256_HEX_CHARS + 1];
    sha256_ref_final(&ctx, digest);
    sha256_bytes_to_hex(digest, hex);
    printf("%s  %s\n", hex, name);
    return 0;
}

int main(int argc, char **argv) {
    if (argc == 2 && (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)) {
        puts("usage: sha256_ref_tool [FILE...]");
        return 0;
    }
    int status = 0;
    if (argc < 2) {
        status = hash_stream(stdin, "-") == 0 ? 0 : 1;
    }
    for (int i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb");
        if (!f) {
            perror(argv[i]);
            status = 1;
            continue;
        }
        if (hash_stream(f, argv[i]) != 0) {
            status = 1;
        }
        if (fclose(f) != 0) {
            perror(argv[i]);
            status = 1;
        }
    }
    if (fflush(stdout) != 0 || ferror(stdout)) {
        perror("sha256_ref_tool: stdout");
        return 1;
    }
    return status;
}
