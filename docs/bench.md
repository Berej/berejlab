# berejlab — benchmark history

Performance measurements of the SHA-256 implementations (CLAUDE.md rule 8, SPEC §11.3).
Newest entries go at the bottom.

## Method

`make bench` builds `build/<mode>/bench_sha256` (never installed) and runs it. Options are
passed through `BENCH_ARGS`, e.g. `make bench BENCH_ARGS="--threads 4 --seconds 10"`.

- Every thread runs the worker hot loop: a 27-byte word with a constant prefix
  (`berejlab-bench-tNN-`, distinct per thread) whose last 8 ASCII digits are advanced as an
  odometer in place, one SHA-256 per word, **no target checks**.
- `reference` = `sha256_ref()` (streaming API, one call per word), single thread.
  `fast` = `sha256_block_compress()` on a block prepared once, on 1..N threads (default N = 4).
- Each run lasts `--seconds` (default 5). Threads start together on a barrier; the rate is
  hashes / wall time up to the last join. Threads are not pinned.
- All 8 state words of every hash are folded into a printed checksum, so the compiler cannot
  drop the work.
- Scaling = rate of N threads / (N × rate of 1 fast thread).
- The self-test runs first; a broken implementation is never benchmarked.

**Desktop numbers are not representative of the Raspberry Pi Zero 2 W.** They only show that
the benchmark works and compare variants on the same machine. Laptop CPUs in particular run a
lone thread at lower clocks than a fully loaded chip, which shows up as "scaling" above 100%;
the Pi numbers are the ones that count for §11.3.

## 1. 2026-11-21 — stage 2 baseline, development desktop

- CPU: AMD Ryzen 5 7535HS (6 cores / 12 threads), cpufreq driver `amd-pstate-epp`, governor
  `powersave`, EPP `balance_performance`, kernel 7.3.4-1-cachyos
- flags: `-std=c17 -Wall -Wextra -Werror -Isrc -O3` (release), 5 s per run
- implementation: portable C17, fully unrolled rounds, rolling 16-word message schedule

gcc 16.2.1:

| implementation | threads | total MH/s | per-thread MH/s | scaling |
| -------------- | ------: | ---------: | --------------: | ------: |
| reference      |       1 |      2.986 |           2.986 |     n/a |
| fast           |       1 |      3.575 |           3.575 |  100.0% |
| fast           |       2 |      8.096 |           4.048 |  113.2% |
| fast           |       3 |     16.225 |           5.408 |  151.3% |
| fast           |       4 |     24.250 |           6.062 |  169.6% |

fast / reference, 1 thread: 1.20×

clang 23.1.5:

| implementation | threads | total MH/s | per-thread MH/s | scaling |
| -------------- | ------: | ---------: | --------------: | ------: |
| reference      |       1 |      2.600 |           2.600 |     n/a |
| fast           |       1 |      3.537 |           3.537 |  100.0% |
| fast           |       2 |      7.411 |           3.706 |  104.8% |
| fast           |       3 |     14.690 |           4.897 |  138.5% |
| fast           |       4 |     22.519 |           5.630 |  159.2% |

fast / reference, 1 thread: 1.36×

Notes:

- The scaling above 100% is a clock effect, not the code: the same single-thread run (gcc
  build) gave 5.94 MH/s fast and 4.80 MH/s reference while three other cores were kept busy
  with idle loops, versus about 3.6 / 2.9 MH/s on an otherwise idle machine. Pinning the thread
  (`taskset`) made no difference. Read the 4-thread row as the closest to sustained full load.
- Message schedule, measured before choosing (2 s runs, 4 threads, two rounds, total MH/s):

  | compiler | rolling `w[16]` | precomputed `w[64]` |
  | -------- | --------------: | ------------------: |
  | gcc      |    24.07, 24.52 |        24.50, 24.55 |
  | clang    |    21.88, 22.85 |        19.78, 19.72 |

  Equal with gcc, rolling about 12% faster with clang, so the rolling window is the only
  variant kept. Re-check on the Cortex-A53 in stage 7.
- The reference is already compiled into fairly tight code at `-O3`, so the fast path gains
  little on this machine. The big wins planned in §11.3 (round precomputation, NEON) belong to
  stage 7.
- The Pi baseline (1 and 4 threads) is still to be recorded on the device.
