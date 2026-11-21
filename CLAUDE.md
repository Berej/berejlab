# CLAUDE.md — berejlab

## What this is

berejlab is a hash-hunting laboratory for the Raspberry Pi Zero 2 W.
The daemon `berejlabd` enumerates SHA-256 hashes of templated inputs 24/7 and saves the hashes
that match user-defined targets. The client `berejlab` is an interactive terminal UI (a
Claude Code–style chat with slash commands) that talks to the daemon over a Unix socket.

**`docs/SPEC.md` is the single source of truth.** Read the sections relevant to your task
before writing code.

## Golden rules

1. **Zero external dependencies.** Allowed: libc (glibc), pthreads, Linux kernel interfaces
   (sysfs, procfs, termios, `ioctl` on `/dev/vcio`, Unix sockets, `poll`, signals) and compiler
   built-ins (`arm_neon.h`). Forbidden: ncurses, readline, any third-party library (header-only
   included), and invoking external programs at runtime. If you believe a dependency is needed —
   stop and ask.
2. **English only**: code, identifiers, comments, docs, commit messages, UI strings.
3. **C17**, zero warnings under `-Wall -Wextra -Werror` with **both gcc and clang**.
4. **The spec is the contract.** Do not change behaviour, command syntax, file formats or the
   protocol in ways that contradict `docs/SPEC.md`. If the spec is ambiguous, contradictory or
   looks wrong — stop and ask; never guess silently. Do not edit `docs/SPEC.md` unless explicitly
   asked.
5. **Tests ship with code.** Every parser, detector and template feature is added together with
   unit tests in the same commit. `make test` must pass before every commit.
6. **Protocol changes** are made together with `docs/protocol.md`.
7. **The reference SHA-256** (`src/common/sha256_ref.c`) stays simple and unoptimized. Every
   optimized implementation is verified against it.
8. **Performance claims need numbers**: run the benchmark before and after and put the results
   in the report (and in `docs/bench.md` when relevant).
9. **Stay in scope.** Do only the task you were given. Do not start the next stage, do not
   refactor unrelated code, do not add features that are not in the task.

## Environment

- Development host: x86_64 Linux desktop (CachyOS). Target: Raspberry Pi Zero 2 W, aarch64,
  Raspberry Pi OS Lite 64-bit, built natively with gcc on the Pi.
- Hardware-specific features (`/dev/vcio` mailbox, Pi thermal zones, `GET_THROTTLED`) must
  degrade gracefully: report "unavailable", never crash or refuse to start.
- Never require root to build, test or run in development mode. Dev mode:
  `berejlabd --foreground --state-dir ./.dev/state --socket ./.dev/berejlab.sock` and
  `berejlab --socket ./.dev/berejlab.sock` (or `make run-dev`).
- Hashrates measured on the desktop are NOT representative of the Pi.

## Commands

```
make                 release build (build/release/)
make debug           -O0 -g with ASan + UBSan (build/debug/)
make CC=clang        build with clang
make test            unit tests
make bench           SHA-256 micro-benchmark
make run-dev         daemon in the foreground, state in ./.dev/
make clean
```

## Code conventions

- Layout: `src/common` (shared), `src/daemon`, `src/client`, `tests`, `data`, `docs`.
- `snake_case`; public symbols carry a module prefix (`sha256_`, `tpl_`, `tgt_`, `proto_`,
  `conf_`, `term_`, …). Header guards: `BEREJLAB_<DIR>_<FILE>_H`.
- Keep files focused (roughly under 600 lines); internal functions are `static`.
- The worker hot loop has no allocation, locks, syscalls or formatted I/O.
- Check every syscall and library return value. Errors are reported, never ignored. The daemon
  never exits because of a bad client command.
- All external input (protocol lines, configs, user commands) goes into fixed-size,
  bounds-checked buffers (protocol line ≤ 4096 bytes, names ≤ 32 chars).
- 4-space indentation, braces on the same line, lines up to ~100 columns.
- Comments explain *why*, not *what*.

## Git workflow

- Work only on branch `dev`. Never commit to or push to `main`. Never force-push.
- One commit per completed task, conventional messages:
  `feat(daemon): …`, `fix(client): …`, `test: …`, `docs: …`, `build: …`, `refactor: …`.
- Commit only when `make`, `make CC=clang` and `make test` all pass with zero warnings.
- Do not push unless asked.

## End-of-task report (always)

Finish every task with a report in exactly this shape:

```
## Report
Task: <name>
Status: done | partial | blocked
Changes: <modules/files and what changed>
Build & tests: <make, make CC=clang, make test — results and test counts>
Benchmarks: <numbers, or "n/a">
Deviations from SPEC: <none | list with reasons>
Open questions / risks: <none | list>
Next suggested step: <one line>
```
