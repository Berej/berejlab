# berejlab

A hash-hunting laboratory for the Raspberry Pi Zero 2 W.

You define **targets** — conditions on a SHA-256 hash, like "starts with seven zeros" or
"contains `c0ffee`". The device enumerates hashes of templated inputs around the clock, keeps
all four cores busy, and saves every hash that matches, together with the input that proves it.
A terminal UI in the style of Claude Code, with a small mascot, shows the finds, the hashrate and
how hot the little board is getting.

It is a hobby and research project: not mining, not cryptanalysis.

```
› /project new kirill-01
› /input kirill-{b62:6}-pi
› /target add prefix 0000000 label="seven zeros"
› /target add prefix 00 and contains cafe
› /session new time 10m x6 rest 5m
› /start
```

Every find can be verified without berejlab:

```
printf '%s' 'kirill-0Gx7Qa-pi' | sha256sum
```

## Status

Early development. See [docs/SPEC.md](docs/SPEC.md) for the full specification and the
development plan.

## Highlights

- Pure C17, zero dependencies beyond libc, pthreads and the Linux kernel.
- Daemon (`berejlabd`, systemd service) + client (`berejlab`, interactive TUI or one-shot
  commands), connected through a Unix socket — close your SSH session, the search goes on.
- Templates with variable parts (`user{b62:8}`), sessions by word range or by time, a coverage
  map so no word is ever hashed twice.
- Targets built from detectors (`prefix`, `contains`, `run`, `palindrome`, `zerobits`, `self`,
  `record`, …) combined with `and` / `or` / `not`.
- Hardware telemetry: hashrate, temperature, real CPU frequency and throttling.

## License

MIT — see [LICENSE](LICENSE).
