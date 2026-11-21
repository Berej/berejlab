// bench_sha256: SHA-256 micro-benchmark without the daemon (`make bench`, SPEC §11.2).
//
// Each thread runs the hot loop the workers will run: a word with a constant prefix whose
// last bytes are advanced as an odometer, hashed in place, with no target checks. Results
// are folded into a checksum that is printed, so the work cannot be optimized away.

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "common/sha256.h"
#include "common/sha256_ref.h"

#ifndef BENCH_CFLAGS
#define BENCH_CFLAGS "unknown"
#endif

#if defined(__clang__)
#define BENCH_COMPILER "clang " __clang_version__
#elif defined(__GNUC__)
#define BENCH_COMPILER "gcc " __VERSION__
#else
#define BENCH_COMPILER "unknown"
#endif

#define BENCH_MAX_THREADS 64
#define BENCH_MAX_SECONDS 3600
// Same granularity as the worker counters (SPEC §10.2); also bounds the stop latency.
#define BENCH_BATCH 65536
#define ODO_DIGITS 8

typedef enum { IMPL_REF, IMPL_FAST } bench_impl;

// One cache line per worker so the counters do not false-share.
typedef struct {
    _Alignas(64) int id;
    bench_impl impl;
    uint64_t hashes;
    uint32_t checksum;
} bench_worker;

typedef struct {
    double total_rate;
    uint32_t checksum;
} bench_result;

static bench_worker workers[BENCH_MAX_THREADS];
static pthread_t threads[BENCH_MAX_THREADS];
static pthread_barrier_t start_barrier;
static atomic_bool stop_flag;

static double now_seconds(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        perror("bench_sha256: clock_gettime");
        exit(1);
    }
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

// "berejlab-bench-tNN-" + ODO_DIGITS digits: distinct words per thread, 27 bytes.
static size_t make_word(char *buf, size_t cap, int id) {
    int n = snprintf(buf, cap, "berejlab-bench-t%02d-%0*d", id, ODO_DIGITS, 0);
    if (n < 0 || (size_t)n >= cap) {
        fputs("bench_sha256: word buffer too small\n", stderr);
        exit(1);
    }
    return (size_t)n;
}

static inline void odometer_step(uint8_t *digits) {
    for (int i = ODO_DIGITS - 1; i >= 0; i--) {
        if (digits[i] != '9') {
            digits[i]++;
            return;
        }
        digits[i] = '0';
    }
}

static uint64_t loop_fast(const char *word, size_t len, uint32_t *checksum) {
    sha256_block blk;
    if (sha256_block_init(&blk, word, len) != 0) {
        fputs("bench_sha256: word too long for one block\n", stderr);
        exit(1);
    }
    uint8_t *digits = blk.bytes + len - ODO_DIGITS;
    uint32_t sum = 0;
    uint64_t n = 0;
    while (!atomic_load_explicit(&stop_flag, memory_order_relaxed)) {
        for (int i = 0; i < BENCH_BATCH; i++) {
            uint32_t st[SHA256_STATE_WORDS];
            sha256_block_compress(&blk, st);
            sum += st[0] ^ st[1] ^ st[2] ^ st[3] ^ st[4] ^ st[5] ^ st[6] ^ st[7];
            odometer_step(digits);
        }
        n += BENCH_BATCH;
    }
    *checksum = sum;
    return n;
}

static uint64_t loop_ref(char *word, size_t len, uint32_t *checksum) {
    uint8_t *digits = (uint8_t *)word + len - ODO_DIGITS;
    uint32_t sum = 0;
    uint64_t n = 0;
    while (!atomic_load_explicit(&stop_flag, memory_order_relaxed)) {
        for (int i = 0; i < BENCH_BATCH; i++) {
            uint8_t digest[SHA256_REF_DIGEST_BYTES];
            sha256_ref(word, len, digest);
            for (int j = 0; j < SHA256_REF_DIGEST_BYTES; j += 4) {
                uint32_t v;
                memcpy(&v, digest + j, sizeof v);
                sum ^= v;
            }
            odometer_step(digits);
        }
        n += BENCH_BATCH;
    }
    *checksum = sum;
    return n;
}

static void *worker_main(void *arg) {
    bench_worker *w = arg;
    char word[64];
    size_t len = make_word(word, sizeof word, w->id);

    int rc = pthread_barrier_wait(&start_barrier);
    if (rc != 0 && rc != PTHREAD_BARRIER_SERIAL_THREAD) {
        fprintf(stderr, "bench_sha256: pthread_barrier_wait: %s\n", strerror(rc));
        exit(1);
    }
    if (w->impl == IMPL_FAST) {
        w->hashes = loop_fast(word, len, &w->checksum);
    } else {
        w->hashes = loop_ref(word, len, &w->checksum);
    }
    return NULL;
}

static void sleep_seconds(int seconds) {
    struct timespec req = {.tv_sec = seconds, .tv_nsec = 0};
    struct timespec rem;
    while (nanosleep(&req, &rem) != 0) {
        if (errno != EINTR) {
            perror("bench_sha256: nanosleep");
            exit(1);
        }
        req = rem;
    }
}

static bench_result run_bench(bench_impl impl, int nthreads, int seconds) {
    int rc = pthread_barrier_init(&start_barrier, NULL, (unsigned)nthreads + 1);
    if (rc != 0) {
        fprintf(stderr, "bench_sha256: pthread_barrier_init: %s\n", strerror(rc));
        exit(1);
    }
    atomic_store(&stop_flag, false);
    for (int i = 0; i < nthreads; i++) {
        workers[i] = (bench_worker){.id = i, .impl = impl};
        rc = pthread_create(&threads[i], NULL, worker_main, &workers[i]);
        if (rc != 0) {
            // Threads already created are parked on the barrier; exiting ends them too.
            fprintf(stderr, "bench_sha256: pthread_create: %s\n", strerror(rc));
            exit(1);
        }
    }

    rc = pthread_barrier_wait(&start_barrier);
    if (rc != 0 && rc != PTHREAD_BARRIER_SERIAL_THREAD) {
        fprintf(stderr, "bench_sha256: pthread_barrier_wait: %s\n", strerror(rc));
        exit(1);
    }
    double t0 = now_seconds();
    sleep_seconds(seconds);
    atomic_store(&stop_flag, true);

    bench_result res = {0};
    uint64_t total = 0;
    for (int i = 0; i < nthreads; i++) {
        rc = pthread_join(threads[i], NULL);
        if (rc != 0) {
            fprintf(stderr, "bench_sha256: pthread_join: %s\n", strerror(rc));
            exit(1);
        }
        total += workers[i].hashes;
        res.checksum ^= workers[i].checksum;
    }
    // Measured up to the last join, so hashes of the batch finished after the stop flag
    // are matched by the time they took.
    double elapsed = now_seconds() - t0;

    rc = pthread_barrier_destroy(&start_barrier);
    if (rc != 0) {
        fprintf(stderr, "bench_sha256: pthread_barrier_destroy: %s\n", strerror(rc));
        exit(1);
    }
    res.total_rate = (double)total / elapsed;
    return res;
}

// The CPU line on x86 is "model name"; Raspberry Pi kernels provide "Model" or "Hardware".
static void cpu_model(char *out, size_t cap) {
    static const char *const keys[] = {"model name", "Model", "Hardware", "CPU part"};
    snprintf(out, cap, "unknown");
    FILE *f = fopen("/proc/cpuinfo", "r");
    if (!f) {
        return;
    }
    char found[sizeof keys / sizeof keys[0]][256] = {{0}};
    char line[512];
    while (fgets(line, sizeof line, f)) {
        for (size_t k = 0; k < sizeof keys / sizeof keys[0]; k++) {
            size_t klen = strlen(keys[k]);
            if (found[k][0] || strncmp(line, keys[k], klen) != 0) {
                continue;
            }
            const char *p = line + klen;
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            if (*p != ':') {
                continue;
            }
            p++;
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            snprintf(found[k], sizeof found[k], "%.*s", (int)strcspn(p, "\n"), p);
        }
    }
    if (fclose(f) != 0) {
        perror("bench_sha256: /proc/cpuinfo");
    }
    for (size_t k = 0; k < sizeof keys / sizeof keys[0]; k++) {
        if (found[k][0]) {
            snprintf(out, cap, "%s", found[k]);
            return;
        }
    }
}

// Frequency policy affects the numbers a lot (e.g. a lone thread clocked lower than a busy
// chip), so it is printed with the results.
static void cpu_governor(char *out, size_t cap) {
    snprintf(out, cap, "unavailable");
    FILE *f = fopen("/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor", "r");
    if (!f) {
        return;
    }
    char line[64];
    if (fgets(line, sizeof line, f)) {
        snprintf(out, cap, "%.*s", (int)strcspn(line, "\n"), line);
    }
    if (fclose(f) != 0) {
        perror("bench_sha256: scaling_governor");
    }
}

static int parse_int(const char *s, int lo, int hi, int *out) {
    char *end;
    errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || v < lo || v > hi) {
        return -1;
    }
    *out = (int)v;
    return 0;
}

static void usage(FILE *out) {
    fprintf(out,
            "usage: bench_sha256 [--threads N] [--seconds S]\n"
            "  --threads N   fast implementation on 1..N threads (default 4, max %d)\n"
            "  --seconds S   duration of each run (default 5, max %d)\n",
            BENCH_MAX_THREADS, BENCH_MAX_SECONDS);
}

static void print_row(const char *impl, int nthreads, double rate, double base_rate) {
    printf("| %-14s | %7d | %16.3f | %19.3f |", impl, nthreads, rate / 1e6,
           rate / nthreads / 1e6);
    if (base_rate > 0) {
        printf(" %8.1f%% |\n", 100.0 * rate / (nthreads * base_rate));
    } else {
        printf(" %9s |\n", "n/a");
    }
    fflush(stdout);
}

int main(int argc, char **argv) {
    int max_threads = 4;
    int seconds = 5;
    int i = 1;
    while (i < argc) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage(stdout);
            return 0;
        }
        int *dst = NULL;
        int hi = 0;
        if (strcmp(argv[i], "--threads") == 0) {
            dst = &max_threads;
            hi = BENCH_MAX_THREADS;
        } else if (strcmp(argv[i], "--seconds") == 0) {
            dst = &seconds;
            hi = BENCH_MAX_SECONDS;
        }
        if (!dst || i + 1 >= argc || parse_int(argv[i + 1], 1, hi, dst) != 0) {
            fprintf(stderr, "bench_sha256: bad argument '%s'\n", argv[i]);
            usage(stderr);
            return 2;
        }
        i += 2;
    }

    // Never benchmark a broken implementation.
    if (!sha256_selftest()) {
        fputs("bench_sha256: SHA-256 self-test failed\n", stderr);
        return 1;
    }

    char cpu[256];
    char governor[64];
    cpu_model(cpu, sizeof cpu);
    cpu_governor(governor, sizeof governor);
    char word[64];
    size_t word_len = make_word(word, sizeof word, 0);
    printf("- CPU: %s (%ld logical CPUs online)\n", cpu, sysconf(_SC_NPROCESSORS_ONLN));
    printf("- cpufreq governor (cpu0): %s\n", governor);
    printf("- compiler: %s\n", BENCH_COMPILER);
    printf("- flags: %s\n", BENCH_CFLAGS);
    printf("- input: %zu-byte word \"%s\", odometer over the last %d bytes, no targets\n",
           word_len, word, ODO_DIGITS);
    printf("- duration: %d s per run\n\n", seconds);

    printf("| implementation | threads | total MH/s       | per-thread MH/s     | scaling   |\n");
    printf("| -------------- | ------: | ---------------: | ------------------: | --------: |\n");
    fflush(stdout);

    uint32_t checksum = 0;
    bench_result ref = run_bench(IMPL_REF, 1, seconds);
    checksum ^= ref.checksum;
    print_row("reference", 1, ref.total_rate, 0);

    double fast1 = 0;
    for (int n = 1; n <= max_threads; n++) {
        bench_result r = run_bench(IMPL_FAST, n, seconds);
        checksum ^= r.checksum;
        if (n == 1) {
            fast1 = r.total_rate;
        }
        print_row("fast", n, r.total_rate, fast1);
    }

    printf("\nfast / reference, 1 thread: %.2fx\n", fast1 / ref.total_rate);
    printf("checksum: %08x\n", (unsigned)checksum);
    return 0;
}
