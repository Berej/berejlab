# berejlab — Specification

Version: v1 (2026-11-21). This document is the single source of truth for development.
Language of the project (code, docs, UI, commits): **English**.

---

## 1. Overview

berejlab is a virtual laboratory for the Raspberry Pi Zero 2 W. The user defines **targets**
(conditions on a hash). The device enumerates SHA-256 hashes of templated inputs around the
clock and saves every hash that matches a target. It is a hobby / research project: not mining,
not cryptanalysis.

### Goals

1. Keep all 4 cores of the Pi Zero 2 W busy 24/7 and get the maximum hashes per second out of
   plain C.
2. Let the user decide what an interesting hash is, through flexible targets.
3. Save every find with proof (input + hash) so it can be verified with one command:
   `printf '%s' '<input>' | sha256sum`.
4. Record hardware telemetry over time: hashrate, temperature, CPU frequency, throttling.
5. Present all of this in a beautiful, readable, interactive terminal UI inspired by Claude Code,
   with a small pwnagotchi-style mascot.

### Non-goals

- No built-in value / rarity scoring. The program never ranks finds.
- No networking, cloud, cryptocurrency mining or distributed work. One device.
- No graphical UI: terminal only, locally or over SSH.

### Hardware and environments

| Role | Machine |
| --- | --- |
| Target | Raspberry Pi Zero 2 W: 4 × Cortex-A53 @ 1 GHz, 512 MB RAM, no SHA hardware acceleration. Raspberry Pi OS Lite 64-bit (aarch64). Headless, accessed over SSH. |
| Development | x86_64 Linux desktop (CachyOS), gcc and clang. |

The code must build and run on both. Pi-specific hardware features (`/dev/vcio` mailbox,
Pi thermal zones) must degrade gracefully when absent: report "unavailable", never crash.

---

## 2. Glossary

| Term | Meaning |
| --- | --- |
| Project | Template + targets + coverage map + session queue + journal + finds. One project runs at a time. |
| Template | Text with variable parts that defines the space of words: `kirill-{b62:6}-pi`. |
| Word | One concrete string that gets hashed: `kirill-0Gx7Qa-pi`. |
| Position | Index of a word inside the template's space, starting at 0. |
| Coverage map | List of position intervals that have already been hashed. |
| Session | A set of enumeration parameters (type, period, count, range, rest, label). Sessions run one after another from a queue. |
| Period | A session parameter: the size of one unit of work — N words or T time. Every completed period gets one journal record. |
| Detector | A built-in hash check: `prefix`, `run`, `contains`, … |
| Target | A condition on the hash built from one or more detectors (`and`, `or`, `not`): what the project is looking for. Has a number and an optional label. |
| Find | A hash that matched a target: word, hash, target number, time, telemetry snapshot. |
| Mileage | Total hashes computed over the device's lifetime, across all projects. |
| Sandbox | Input without a leading `/`: the text is hashed immediately and checked against the current project's targets. |
| Mascot | Character in the UI header that reacts to the device state. |
| Chunk | Internal mechanism, invisible to the user: 2^20 consecutive positions a worker thread takes at once. |

---

## 3. Architecture

Two binaries on one device:

```
laptop ──SSH──▶ berejlab (client, TUI) ◀── Unix socket, text protocol ──▶ berejlabd (daemon)
                                                                            ├─ engine     projects, sessions, coverage
                                                                            ├─ workers ×4 SHA-256 + target checks
                                                                            ├─ server     socket, events, mascot
                                                                            ├─ telemetry  sysfs, /dev/vcio
                                                                            └─ store      /var/lib/berejlab
```

The client and the daemon talk only through the socket, so closing the laptop or dropping SSH
never affects the search.

### berejlabd — daemon

- Started by systemd at boot, runs as user `berejlab`, restarted on failure.
- **Main thread:** `poll()` loop over the socket, 1 s telemetry timer, 60 s checkpoint timer,
  command handling. No heavy computation.
- **Worker threads** (1–4, one per core, pinned with `pthread_setaffinity_np`, scheduled with
  `SCHED_IDLE`): take a chunk, hash, check targets, push matches into a per-worker lock-free
  single-producer/single-consumer ring buffer. The main thread drains the rings, re-verifies
  each match with the reference SHA-256, stores it and broadcasts an event.
- Owns all data: projects, finds, journals, telemetry, and an in-memory ring of the last 200
  events for newly connected clients.
- Computes the mascot state and phrase, so every client shows the same thing.

### berejlab — client

- UI only: renders, reads input, turns it into protocol commands. Stores only its own input
  history and settings.
- If the daemon is not running: prints `daemon is not running: sudo systemctl start berejlab`
  and exits.
- One-shot mode: `berejlab /command args` runs one command, prints the reply as plain text and
  exits with 0 or 1.

### Shared code (`src/common`)

SHA-256 (fast + reference), config parser, protocol line parser/builder, the command table
(single source for client hints and daemon parsing), UTF-8 helpers.

### The path of one word

```
session gives the next uncovered word → SHA-256 → check every enabled target
        → match?  yes → re-verify with reference SHA-256 → finds.log + ★ event in the feed
                  no  → next word
```

Without targets nothing is saved (see §5.4).

---

## 4. Projects, templates and sessions

Project = template + targets + coverage map. The template defines the space of words; sessions
define how it is walked. Words are enumerated strictly in order — no randomness, no shuffling —
and every word is hashed at most once.

### 4.1 Template syntax

A template is plain text with one or more variable parts in curly braces. Text before the first
brace is the prefix, text after the last one is the suffix.

```
kirill-{b62:6}-pi
└──┬──┘└──┬──┘└┬┘
 prefix  vary  suffix

kirill-000000-pi → kirill-000001-pi → … → kirill-00000z-pi → kirill-000010-pi → …
```

A variable part is written `{alphabet:length}`:

| Syntax | Characters, in enumeration order | Example |
| --- | --- | --- |
| `{num:N}` | `0-9` | `{num:8}` → `00004217` |
| `{hex:N}` | `0-9 a-f` | `{hex:8}` → `00a3f1c2` |
| `{abc:N}` | `a-z` | `{abc:5}` → `aaqzk` |
| `{b62:N}` | `0-9 A-Z a-z` (Base62) | `{b62:6}` → `00Gx7Q` |
| `{[set]:N}` | custom set, ranges with `-` | `{[aeiou]:5}`, `{[a-f0-3]:6}` |

Rules:

- Length `N` is fixed, 1–20. Word length is constant for the whole template.
- Characters of every alphabet are ordered by ASCII code; a custom set is sorted automatically
  (order of writing does not matter), duplicates are an error. Words therefore follow natural
  sort order, and "from … to" bounds are intuitive.
- Up to 4 variable parts, e.g. `{num:4}-{abc:3}`. Together they act as one odometer; the
  rightmost changes fastest: `0000-aaa`, `0000-aab`, … `0000-zzz`, `0001-aaa`.
- Prefix, suffix and text between parts: printable ASCII only (codes 32–126). Literal braces are
  written `{{` and `}}`. UTF-8 is not allowed (byte length must be predictable).
- Word length ≤ 55 bytes, so each hash is a single SHA-256 block. Longer templates are rejected.
- Space size (product of alphabet sizes to the power of lengths) ≤ 2^64, so a position fits in a
  `uint64_t`. Not a practical limit: 2^64 hashes at 3 MH/s is ~195,000 years.
- `b62` is the default suggested alphabet: ~5.95 bits per character versus ~3.3 for `num`.

`/preview` shows before start: first and last word, byte length, space size, time for a full
pass at the current hashrate, and how many SHA-256 rounds are saved thanks to the constant
prefix. Order of magnitude at an assumed ~3 MH/s (the real figure comes from the benchmark):

| Template | Words | Full pass |
| --- | --- | --- |
| `user{num:8}` | 10^8 | ~30 s |
| `user{b62:6}` | 5.7·10^10 | ~5 h |
| `user{num:12}` | 10^12 | ~4 days |
| `user{b62:7}` | 3.5·10^12 | ~2 weeks |
| `user{b62:8}` | 2.2·10^14 | ~2.3 years |

### 4.2 Positions and the coverage map

- Every word has a position from 0 to space size − 1. Word ↔ position conversion works both
  ways, so bounds can be given as words or as numbers.
- The project keeps a **coverage map**: the list of position intervals already hashed. Every
  session takes only uncovered positions; nothing is ever hashed twice.
- `/project info` shows coverage as a bar and a percentage:
  `covered 3.2%  [██░░░░░░░░░░░░░░░░░░]  2 intervals`.
- When every position is covered, the project becomes `finished`.

### 4.3 Sessions

Work inside a project runs in sessions. Sessions run one after another from a queue; exactly
one is active; all of them write to the shared coverage map. A session is a set of enumeration
parameters, the main one being the **period**.

| Parameter | Meaning | Example |
| --- | --- | --- |
| type | what the period is measured in: words (`range`) or time (`time`). Set at creation, never changes. | `range`, `time` |
| period | size of one unit of work | `1e10` words, `10m`, `1h30m` |
| count | how many periods to run; default: until the end of the range for `range`, forever for `time` | `x6`, `forever` |
| range | `range` only: where to search. Default: from the first uncovered position to the end of the space | `kirill-000000-pi .. kirill-0zzzzz-pi` |
| rest | pause between periods; worker threads are stopped | `rest 5m` |
| label | caption in the journal | `label="heating, no heatsink"` |

```
/session new <type> <period> [count] [range] [rest <time>] [label="…"]

/session new time 10m x6 rest 5m label="heating, no heatsink"
/session new range 1e10
/session new range 1e9 kirill-000000-pi .. kirill-0zzzzz-pi
```

How a session runs:

1. A period starts at the first uncovered position: a `range` period hashes N words, a `time`
   period runs for T.
2. After the period a journal record is written, then the rest pause (if any), then the next
   period.
3. The session ends when all periods are done, the range is exhausted, or `/session stop` is
   issued. The next queued session then starts automatically. If the queue is empty, the project
   pauses and says so in the feed.

Rules:

- Sessions can be created in advance; they wait in the queue (`planned`) and run in order.
- `/session set` changes parameters within the same type (`time 10m` → `time 30m`); changes
  apply from the next period.
- The type cannot be changed; such a command is rejected with a hint. Instead use
  `/session clone <#> <type> <period> …`: it creates a session of the other type with the same
  rest and label, placed right after the current one; the current session ends.
- A `time` period counts only actual working time: if the Pi reboots in the middle of a 10-minute
  period, the period finishes the remaining minutes after restart. Rest counts real (wall) time —
  cooling continues while the workers are off.
- If a range is partially covered, covered pieces are skipped. If it is fully covered, the
  session is rejected with a message.
- On creation the session shows an estimate: words and time per period and for the whole session
  at the current hashrate.
- `/start` with no sessions creates a default session `time 1h forever`, so an hourly journal is
  kept even without a plan.

Session journal (`/session show <#>`): one row per completed period — first and last word,
duration, hashes, finds, average hashrate, average and max temperature, share of time throttled.

### 4.4 Lifecycle

- Project: `draft` → `running` ⇄ `paused` → `finished`.
- Session: `planned` (queued) → `running` ⇄ `paused` → `done`. While `running`, a session is
  either in a period or resting.
- In `draft` the template and targets are freely editable. The template is frozen at the first
  `/start` (positions only make sense together with it). A different template means a new
  project.
- Targets can be added, disabled and removed at any time except in `finished`. Every change is
  written to the journal with the current position.
- Starting another project pauses the current one.
- A project becomes `finished` when the whole space is covered or the user runs `/stop`. It is
  read-only afterwards.

### 4.5 Run conditions

- `cores` — number of worker threads, 1–4, default 4. Can be changed on the fly.
- `thermal max <°C> resume <°C>` — thermal guard: at `max` the daemon pauses the workers, at
  `resume` it resumes them. Off by default: firmware throttling is itself a research subject.

Time and volume limits are expressed through sessions; there is no `/limit` command.

### 4.6 Chunks (internal)

- A worker takes a chunk of 2^20 consecutive positions (~1 s of work for one core) from a shared
  atomic cursor and processes it without any synchronization.
- Chunks are clipped to the period boundary and to already covered intervals.
- The checkpoint (every 60 s) stores the coverage map. In the current period everything below
  the start of the earliest unfinished chunk counts as covered, so after a crash at most a few
  chunks are hashed again.
- Duplicate finds after recovery are dropped by the pair (word, target).

---

## 5. Targets and detectors

A target is what the project looks for: a condition on the hash defined by the user. Every hash
is checked against all enabled targets; every match becomes a find. Without targets the program
only computes and saves nothing. The program never judges targets or ranks finds.

### 5.1 Detectors

Detectors are built-in checks with parameters; targets are built from them. Detectors work on the
hash as 64 lowercase hex characters, except `zerobits`, which works on bits.

| Detector | Syntax | Matches when | Example |
| --- | --- | --- | --- |
| `prefix` | `prefix <mask>` | hash starts with the mask; `?` = any character | `prefix 0000000`, `prefix 00??00` |
| `suffix` | `suffix <mask>` | hash ends with the mask | `suffix ffffff` |
| `contains` | `contains <hex>` | substring anywhere | `contains deadbeef` |
| `dict` | `dict <name>` | hash contains any word from a dictionary (text file, one word per line) | `dict hexwords` |
| `zerobits` | `zerobits <N>` | at least N leading zero bits | `zerobits 30` |
| `run` | `run <N> [char] [at=start\|any]` | N identical characters in a row; default `at=any` | `run 9`, `run 7 f at=start` |
| `seq` | `seq <N> [asc\|desc\|any]` | N adjacent characters in sequence (±1) | `seq 8 asc` → `3456789a` |
| `palindrome` | `palindrome <N> [at=start\|any]` | a fragment of length N reads the same both ways; default `at=start` | `palindrome 14` |
| `charset` | `charset <set> [N]` | the first N characters (default 64) are all from the set: `digits`, `letters`, or explicit like `0-7`, `0f` | `charset digits 20` |
| `self` | `self <N>` | hash starts with the first N characters of the word itself (lowercased) | `self 6` |
| `record` | `record <metric>` | hash beats the project's best so far by metric: `zerobits`, `run`, `seq`, `palindrome`, `lowest` (numerically smallest hash) | `record zerobits` |

For `self` the daemon warns if the first N characters of the template cannot be hex characters
(the target could never match). A `hexwords` dictionary ships with the project (deadbeef,
cafebabe, c0ffee, badf00d, facade, decade, …).

### 5.2 Combinations

A target may combine detectors with `and`, `or`, `not` and parentheses:

```
/target add prefix 00 and contains cafe
/target add (run 8 or seq 8) and not prefix f
/target add contains dead and contains beef label="meat"
```

- Precedence: `not` > `and` > `or`; parentheses override.
- Up to 8 detectors per target.
- Short-circuit evaluation; the daemon reorders operands inside `and`/`or` so the cheapest checks
  (`prefix`, `zerobits`) run first. Writing order does not affect the result.
- `record` cannot be combined (it is a comparison with the previous best, not a condition); it is
  always a standalone target.
- The sandbox and `/target list` show which parts of each target matched.

### 5.3 Common target parameters

- `label="text"` — custom name shown in the feed instead of the condition.
- `save=N` — maximum number of finds to save (default 1000). Beyond it the target only counts
  matches. `save=0` — count only.
- `quiet` — finds are saved but not shown in the feed.
- Every target counts all its matches, including unsaved ones.
- One hash can match several targets; it is then recorded as a find for each of them.

### 5.4 Starting without targets

If the project has no enabled targets, `/start` does not start right away. It warns and asks for
confirmation:

```
› /start
  ⚠ no targets set: the search will only load the CPU, no finds will be saved.
    Start anyway? [y/N]
```

- `y` starts the search and writes a "started without targets" journal event. Any other answer
  cancels.
- In the protocol: the daemon replies with error `confirm`; the client asks the user and repeats
  the command with `--yes`. `/stop` and `/project delete` work the same way.
- In one-shot mode (`berejlab /start`) `--yes` is required; otherwise the command fails with a
  hint.
- If the last target is disabled or removed while running, the search continues and the same
  warning appears in the feed.

### 5.5 Flood protection

A user can set a target that is too weak (e.g. `run 4`). If a target matches more than 10 times
per second, the daemon switches it to `save=0`, writes a journal event and reports it in the feed.
This protects the SD card; it is not a value judgement.

### 5.6 Check performance

Target checks must cost ≤ 10% of hashing time with 10 enabled targets, combinations included.

- Targets are compiled into structures with precomputed masks before workers start.
- `prefix`, `zerobits`, `charset` are first checked on raw 32-bit words of the SHA-256 state,
  without hex conversion.
- The hex string is built at most once per hash and only if some target needs it.
- Short-circuit: `prefix 00 and contains cafe` stops at the first check in 255 of 256 cases.
- `dict` with a large dictionary uses Aho–Corasick.

### 5.7 Examples

```
/target add prefix 0000000 label="seven zeros"
/target add contains c0ffee
/target add run 9 save=200
/target add record zerobits
/target add self 6 label="mirror"
/target add prefix 00 and (contains cafe or contains babe)
```

---

## 6. Client UI

The client is a full-screen terminal UI modelled on Claude Code: a header with the mascot and
telemetry, an event feed, and an input line with slash commands. Keyboard first; the mouse is a
helper. All UI text is English.

### 6.1 Layout

```
╭─ bit-muncher · day 12 ─────────────────── kirill-01 · session 2 · period 3/6 ▶ ─╮
│  ╭────────╮  "four cores at work,      hashrate  3.12 MH/s  ▅▆▆▇▆▆▅▆▇▇▆▇     │
│  │ (•̀ᴗ•́)  │   none of them bored"     temp      61°C       ▃▃▄▄▅▅▅▆▆▆▆▆     │
│  ╰────────╯                           freq      1000 MHz   cores 4/4        │
│              mileage 4.1T             finds     3          uptime 2d 4h     │
╰─────────────────────────────────────────────────────────────────────────────╯

  › /session new time 10m x6 rest 5m
    ✓ session 2 · time 10m × 6 · rest 5m · ≈ 1.8B words per period

  ▶ session 2 · period 3/6 · from kirill-0G7a1c-pi

  ★ find · #1 seven zeros · 14:02:17
    kirill-0Gx7Qa-pi
    00000003f1a9c2e7…b41d

  ⚠ 72°C · frequency reduced to 800 MHz

  hello world
    b94d27b9934d3e08…cde9 · #1 ✗  #2 ✗

╭─────────────────────────────────────────────────────────────────────────────╮
│ › /se█                                                                      │
╰─────────────────────────────────────────────────────────────────────────────╯
  /session new     create a session
  /session list    sessions of the project
  /session clone   clone a session with another type
  Tab complete · ↑↓ select · Esc close
```

- **Header** (5 rows) always on top. Collapses to one line when the terminal is shorter than 24
  rows or on `/compact`.
- **Feed** fills the space between header and input.
- **Input** in a frame at the bottom; under it the completion list, the parameter hint or the
  key-hint line.
- Minimum size 80×24. Smaller: simplified mode without frames.

### 6.2 Header and telemetry

Left: the mascot — a face in a frame and a 1–2 line phrase. Right, updated once per second:

- hashrate and temperature with sparklines (`▁▂▃▄▅▆▇█`) for the last minutes;
- CPU frequency, active cores, throttling flag;
- mileage, finds of the current project, daemon uptime;
- in the frame title: device name, age in days, project, session and period number.

### 6.3 Mascot

The mascot reacts to the real device state, chosen by priority (top wins):

| Priority | Condition | Face | Example phrase |
| --- | --- | --- | --- |
| 1 | temperature ≥ 75°C | `(×﹏×)` | "74°C, I'm melting" |
| 2 | throttling | `(≖_≖)` | "slowing down a bit" |
| 3 | find in the last 30 s | `(◕‿‿◕)` | "look what I found!" |
| 4 | round number in mileage | `(ﾉ◕ヮ◕)ﾉ` | "a trillion hashes in my life!" |
| 5 | paused, resting between periods, or no project running | `(-_-) zZ` | "wake me up when you need me" |
| 6 | more than 3 h without finds | `(–_–)` | "are the targets too strict?" |
| 7 | normal work | `(•̀ᴗ•́)` | "crunching, crunching" |

- The phrase changes at most once per 60 s, except for states 1–4, which switch immediately.
- Phrases live in `phrases.txt` (sections per state); the user can add their own. Placeholders:
  `{temp}`, `{rate}`, `{idle}`, `{total}`.
- Rare phrases (find, mileage milestone) are also posted to the feed; ordinary ones stay in the
  header.
- Mileage milestones: 10^9, 10^10, 10^11, …
- Default device/mascot name: `bit-muncher` (changeable with `/name`).

### 6.4 Feed

Entry kinds: command (`›`), success (`✓`), error (`✗`), find (`★`), warning (`⚠`), info (`ℹ`),
session/period (`▶`), mascot phrase, sandbox result.

- On connect the client receives the last 200 daemon events and shows a line above them:
  "while you were away: N finds, M warnings".
- Scrolling: PgUp/PgDn, mouse wheel. If the user scrolled up, new events do not move the view;
  a "↓ N new" marker appears at the bottom.
- Hashes are shortened to `first 16…last 4`, matched part highlighted. If the match is in the
  middle, the full hash is shown.
- `/clear` clears the feed in this client only.

### 6.5 Input and keys

| Key | Action |
| --- | --- |
| Enter | run the command or hash the text |
| Tab | complete a command, detector, value, project name |
| ↑ / ↓ | command history; in an open list — select item |
| ← → Home End | move cursor |
| Ctrl+A / Ctrl+E | start / end of line |
| Ctrl+W / Ctrl+U | delete word / line |
| Esc | close list or panel |
| PgUp / PgDn | scroll the feed |
| Ctrl+C | clear input; twice on an empty line — quit |
| Ctrl+D | quit |

- Typing `/` opens the command list with descriptions, filtered as you type.
- History persists in `~/.local/state/berejlab/history` (last 1000 lines).
- Quitting the client never stops the daemon.

**Parameter hints.** While a command is being typed, the remaining parameters are shown to the
right of the cursor in a dim colour, as a placeholder. The parameter being typed now is
highlighted, and its description is shown under the input:

```
╭─────────────────────────────────────────────────────────────────────────────╮
│ › /session new time 10m █[count] [rest <time>] [label="…"]                  │
╰─────────────────────────────────────────────────────────────────────────────╯
  count — how many periods to run · optional · default: forever
  Tab: x1 · x6 · forever                    ≈ 10 min · 1.8B words per period
```

- The placeholder is never inserted into the line and disappears as you type. Required
  parameters are shown as `<name>`, optional ones as `[name]`.
- The hint line shows: parameter name, what it sets, required/optional, default, allowed values
  or an example.
- Hints depend on what is already typed: after `range` a `[range]` parameter appears, after
  `time` it does not. In `/target add`, after a detector name its parameters are shown
  (`prefix <mask>`); after a complete condition — what can follow: `and` · `or` · `label=` ·
  `save=`.
- Tab inserts and cycles values: fixed lists from the command table (`range`/`time`, detectors,
  `on`/`off`) and daemon data (project names, session and target numbers).
- Values are validated live: an invalid value (e.g. `10q` for a time) is underlined and the hint
  line shows why. Enter does not send a command with an error.
- For sessions and templates the hint line shows a live estimate: time and words at the current
  hashrate.
- Source of all hints: one command table in `src/common` (name, parameters, types, descriptions,
  defaults). The client builds hints from it, the daemon parses commands with it — they cannot
  diverge.
- One-shot mode has no hints; on error it prints the command syntax.

### 6.6 Sandbox

Text without a leading `/` is sent to the daemon as `/hash <text>`. Reply: the input, its byte
length, the full SHA-256, and ✓/✗ for every target of the current project (with matched parts).
Nothing is saved, counters are not affected. The 55-byte limit does not apply here.

### 6.7 Panels

A panel temporarily replaces the feed; header and input stay. Esc returns to the feed.

- `/stats` — hashrate per core and total, chart for 1 h / 24 h / 7 days, match counters per
  target.
- `/temp` — temperature and frequency on one time axis, throttled periods highlighted.
- `/finds` — table of finds with a target filter; ↑↓ to select, Enter for details.
- `/targets` — targets with on/off state and counters.

### 6.8 Mouse

- xterm SGR mouse mode (1006); works over SSH in modern terminals.
- Click a find → details. Click a completion item → select it. Click hashrate or temperature in
  the header → `/stats` or `/temp`. Wheel → scroll.
- Panels have buttons like `[ pause ]` and `[ copy ]`. Copying to the clipboard uses OSC 52,
  which works over SSH.
- While mouse mode is on, terminals select text only with Shift. `/mouse off` disables the mouse.

### 6.9 Rendering

- Alternate screen, raw mode via termios. The terminal is restored on exit and on crash
  (SIGINT, SIGTERM, SIGSEGV handlers).
- Double buffer: only changed cells are written, one `write()` per frame, at most 10 frames per
  second, no flicker.
- Window resize via SIGWINCH.
- UTF-8 required. Colours: 256-colour by default, truecolor when `COLORTERM=truecolor`, none
  when `NO_COLOR` is set.
- Own character-width table (like `wcwidth`) so frames stay aligned with box-drawing and mascot
  characters (including combining marks).
- The client uses ≤ 2% of one core and sleeps in `poll()` when idle.

---

## 7. Command reference

Commands work the same in the TUI and in one-shot mode. Destructive commands (`/stop`,
`/project delete`) ask for confirmation; one-shot mode needs `--yes`.

### Projects

| Command | Action |
| --- | --- |
| `/project new <name>` | create a project in `draft` and make it current |
| `/project list` | projects: state, coverage, finds |
| `/project open <name>` | make it current for viewing and editing; does not change what is running |
| `/project info` | current project: template, targets, conditions, sessions, coverage map |
| `/project delete <name>` | delete a project and all its finds (not the running one) |
| `/start [name]` | start or resume a project; with no sessions creates `time 1h forever`; without targets — warning and confirmation (§5.4); another running project is paused |
| `/pause` | pause the running project |
| `/stop` | move the project to `finished` |

### Sessions

| Command | Action |
| --- | --- |
| `/session new <type> <period> [count] [range] [rest <time>] [label="…"]` | create a session (§4.3); if one is active, the new one is queued |
| `/session list` | sessions and queue: type, parameters, state, progress |
| `/session show <#>` | session journal: one row per completed period |
| `/session set <#> <params>` | change parameters within the type, from the next period; a type change is rejected with a hint about clone |
| `/session clone <#> <type> <period> …` | new session of another type with the same rest and label, placed right after the current one; the current one ends |
| `/session stop` | end the active session and move to the next one in the queue |

### Input and targets

| Command | Action |
| --- | --- |
| `/input <template>` | set the template, e.g. `kirill-{b62:6}-pi` (draft only) |
| `/preview` | first and last word, byte length, space size, full-pass time |
| `/target add <condition> [label=…] [save=N] [quiet]` | add a target: one detector or a combination with `and`, `or`, `not` |
| `/target list` | targets with numbers, state and counters |
| `/target rm <#>` | remove a target (its finds stay) |
| `/target on <#>` / `/target off <#>` | enable / disable a target |
| `/detectors [name]` | help on detectors and their parameters |
| `/hash <text>` | sandbox; same as typing text without `/` |

### Conditions

| Command | Action |
| --- | --- |
| `/cores <1-4>` | number of worker threads, on the fly |
| `/thermal max <°C> resume <°C>` · `/thermal off` | thermal guard |

### Results

| Command | Action |
| --- | --- |
| `/finds [#target]` | finds panel |
| `/show <#find>` | details: word, full hash, target, time, temperature, verify command |
| `/verify <word>` | recompute the hash and check whether it is a saved find |
| `/export [txt\|csv]` | export finds of the current project to `export/` |
| `/journal [N]` | last N journal events of the project |

### Hardware

| Command | Action |
| --- | --- |
| `/stats` | performance panel |
| `/temp` | temperature and frequency panel |
| `/bench [seconds]` | SHA-256 benchmark on 1, 2, 3 and 4 threads; the running project is paused meanwhile |
| `/hw` | board model, frequencies, throttling state, free space, kernel version |

### Other

| Command | Action |
| --- | --- |
| `/status` | short summary in 1–3 lines |
| `/name <name>` | device and mascot name |
| `/help [command]` | command list or help on one command |
| `/compact` | collapse / expand the header |
| `/mouse on\|off` | enable / disable the mouse |
| `/clear` | clear the feed in this client |
| `/quit` | quit the client (the daemon keeps running) |

`/compact`, `/mouse`, `/clear` and `/quit` are handled locally by the client; everything else
goes to the daemon.

---

## 8. Client–daemon protocol

A line-based text protocol over a Unix socket. The daemon sends data, the client formats it.
Manual debugging: `socat - UNIX-CONNECT:/run/berejlab/berejlab.sock` (socat is a debugging tool,
not a dependency).

### 8.1 Transport

- `SOCK_STREAM`, path `/run/berejlab/berejlab.sock`, mode `0660`, group `berejlab` (the user is
  added to the group at install). Overridable with `--socket` for development.
- UTF-8, lines end with `\n`, max 4096 bytes per line.
- Up to 8 clients at once; all receive the same events.
- The daemon never blocks on a client: non-blocking sockets and `poll()`. A client whose send
  buffer exceeds 1 MB is disconnected.

### 8.2 Line format

| Direction | Line | Meaning |
| --- | --- | --- |
| daemon → client | `HELLO berejlab <version> proto=1` | first line after connect |
| client → daemon | `REQ <id> <command>` | request; `id` is a number chosen by the client |
| daemon → client | `DATA <id> <key=value …>` | one line of a multi-line reply |
| daemon → client | `OK <id> [text]` | request completed |
| daemon → client | `ERR <id> <code> <text>` | error: `usage`, `state`, `notfound`, `type`, `covered`, `confirm`, `limit`, `internal`. On `confirm` the client asks the user and repeats the command with `--yes` |
| daemon → client | `EV <type> <key=value …>` | async event: `find`, `state`, `session`, `period`, `target`, `warn`, `info`, `mascot`, `milestone` |
| daemon → client | `TEL <key=value …>` | telemetry, once per second |

- Values without spaces are written as is; values with spaces in double quotes with escapes
  `\"`, `\\`, `\n`.
- `EV` and `TEL` may arrive between `DATA` and `OK` of any request; the client dispatches by
  prefix.
- Right after `HELLO` the daemon sends the last 200 events with `replay=1` and the current `TEL`.
  For the "while you were away" line the client sends the time of its previous connection:
  `REQ <id> /since <unix-time>`.
- Unknown keys and event types are ignored by the client, so the protocol can grow without a
  version bump.

### 8.3 Telemetry keys

`hr` — hashes/s (integer), `hr0`…`hr3` — per core, `temp` — millidegrees °C, `freq` — MHz,
`thr` — throttling bits, `cores`, `total` — mileage, `proj` — project name, `state`, `ses` —
active session number, `per` — current period number, `phase` — `work` or `rest`, `pos` —
current position, `cov` — coverage in parts per million, `finds`, `up` — daemon uptime in
seconds, `mood` — mascot state.

### 8.4 Example

```
< HELLO berejlab 0.1.0 proto=1
< TEL hr=3120544 temp=61200 freq=1000 thr=0x0 cores=4 total=4100221993012 proj=kirill-01 state=running ses=2 per=3 phase=work pos=1844672231 cov=32476 finds=3 up=187233 mood=work
> REQ 1 /target add prefix 00 and contains c0ffee label="coffee"
< OK 1 target=4
< EV target action=add proj=kirill-01 target=4 spec="prefix 00 and contains c0ffee" label="coffee" pos=1844690112
> REQ 2 /session set 2 range 1e10
< ERR 2 type "session 2 has type time; for range use /session clone 2 range 1e10"
> REQ 3 /hash hello world
< DATA 3 input="hello world" len=11 hash=b94d27b9934d3e08a52e52d7da7dabfac484efe37a5380ee9088f7ace2efcde9
< DATA 3 target=1 match=0
< DATA 3 target=4 match=0 parts="prefix 00:0 contains c0ffee:0"
< OK 3
< EV find proj=kirill-01 target=1 n=4 input="kirill-0Gx7Qa-pi" hash=0000000c… t=1763733737 temp=61500
< EV period proj=kirill-01 ses=2 per=3 action=end from="kirill-0G7a1c-pi" to="kirill-0H2bQx-pi" hashes=1803221504

# starting another project without targets
> REQ 4 /start load-test
< ERR 4 confirm "no targets set: the search will only load the CPU"
> REQ 5 /start load-test --yes
< OK 5
< EV state proj=load-test state=running targets=0
```

(The find hash is shortened here; in the protocol it is always full.) The exact reply format of
every command is documented in `docs/protocol.md`, written together with the daemon (stage 4).

---

## 9. Storage

All data are plain text files, readable and verifiable without berejlab. A power loss loses at
most 60 s of enumeration and never a find.

### 9.1 Directories

```
/etc/berejlab/
  berejlab.conf            daemon config
  phrases.txt              (optional) custom mascot phrases, override the defaults
  dict/                    (optional) custom dictionaries for dict
/usr/local/share/berejlab/
  phrases.txt              default phrases
  dict/hexwords.txt        default dictionary
/var/lib/berejlab/
  device.conf              name, "birth" date, mileage
  projects/<name>/
    project.conf           template, targets, conditions, sessions and queue, state
    checkpoint             coverage map, current period, match counters
    finds.log              finds, append-only
    periods.log            session journals, one row per period, append-only
    journal.log            events, append-only
  telemetry/YYYY-MM.log    per-minute aggregates
  export/                  /export output
~/.config/berejlab/client.conf     client settings (mouse, compact)
~/.local/state/berejlab/history    command history
```

All paths are overridable for development: `berejlabd --state-dir <dir> --socket <path>
--config <file>`.

### 9.2 Config format

One format for every `.conf`: `[section]` headers, `key = value` lines, `#` comments, values
with spaces in double quotes. Own parser, ~200 lines.

```
# projects/kirill-01/project.conf
[project]
name     = kirill-01
state    = running
created  = 2026-11-21T14:00:03Z
template = "kirill-{b62:6}-pi"
frozen   = yes
cores    = 4

[target 1]
spec    = "prefix 0000000"
label   = "seven zeros"
enabled = yes
save    = 1000

[session 1]
type    = range
period  = 1e10
count   = end              # until the end of the space
state   = done
ended   = clone            # ended via /session clone → session 2

[session 2]
type    = time
period  = 10m
count   = 6
rest    = 5m
label   = "heating, no heatsink"
state   = running

[session 3]
type    = range
period  = 1e9
range   = "kirill-100000-pi .. kirill-1zzzzz-pi"
state   = planned          # queued after session 2
```

```
# projects/kirill-01/checkpoint
covered       = 0-1844672230              # covered intervals, comma-separated: 0-99,500-799
session       = 2
period        = 3
phase         = work
period_work_s = 241                       # seconds of work already done in this period
rest_until    =                           # for phase = rest: when the rest ends
matches       = 1:4,2:0,3:17,4:0          # match counters per target
```

Configs and checkpoints are rewritten atomically: write to a temp file, `fsync`, `rename`.

### 9.3 Log formats

`finds.log` — one find per line, tab-separated. Words are printable ASCII, so no escaping is
needed.

```
# n  time                  target input             sha256                                  temp_mC  freq_MHz
4    2026-11-21T14:02:17Z  1      kirill-0Gx7Qa-pi  00000003f1a9c2e7…(64 chars)…b41d        61200    1000
```

Verify any find without berejlab: `printf '%s' 'kirill-0Gx7Qa-pi' | sha256sum`.

`periods.log` — one line per completed period, tab-separated:

```
# ses per start                 end                   work_s from              to                hashes      finds hr_avg  temp_avg temp_max throttle_pct
2     3   2026-11-21T14:20:00Z  2026-11-21T14:30:00Z  600    kirill-0G7a1c-pi  kirill-0H2bQx-pi  1803221504  1     3005369 61800    64200    0
```

### 9.4 SD card wear

| What | When |
| --- | --- |
| find | immediately, with `fdatasync` — it is the main result |
| checkpoint (coverage map), mileage | every 60 s, on period change and on shutdown |
| period record | at the end of every period |
| journal | buffered, flushed with the checkpoint |
| telemetry | one line per minute |
| project config | only on change |

Target: ≤ ~10 MB written per day in normal operation. Flood protection (§5.5) keeps targets from
writing more than 10 times per second. The daemon logs to syslog only start, stop and errors.

---

## 10. Telemetry and hardware

Once per second the daemon samples hashrate, temperature, frequency and throttling flags. No
external programs (`vcgencmd` etc.) are called: everything is read directly from kernel
interfaces.

### 10.1 Sources

| Value | Primary source | Fallback |
| --- | --- | --- |
| SoC temperature | `/sys/class/thermal/thermal_zone0/temp` (millidegrees) | mailbox `GET_TEMPERATURE` |
| real ARM frequency | mailbox `/dev/vcio`, tag `GET_CLOCK_RATE_MEASURED` | `/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq` |
| throttling and under-voltage | mailbox, tag `GET_THROTTLED` | none (shown as "unavailable") |
| hashrate | worker counters | — |
| board model | `/proc/device-tree/model` | `/proc/cpuinfo` |

- The mailbox is the same `ioctl` on `/dev/vcio` that `vcgencmd` uses. The daemon user must be in
  group `video`. Tag numbers and `GET_THROTTLED` bits must be checked against the firmware
  documentation and on the device.
- `scaling_cur_freq` may not reflect firmware throttling, so the measured mailbox frequency is
  the primary source.
- Under-voltage is common under full load with a weak power supply; the first occurrence raises a
  `warn` event in the feed and the journal.
- Throttling thresholds belong to the firmware. berejlab never changes them; it only observes.
  Its own guard is `/thermal`.
- On the development desktop most of these sources are absent or different; the daemon must show
  "unavailable" and keep working.

### 10.2 Hashrate

- Each worker has its own counter in a separate 64-byte cache line, updated every 65,536 hashes.
- Hashrate = counter delta over the interval, measured with `CLOCK_MONOTONIC`.

### 10.3 History

| Level | Step | Depth | Where |
| --- | --- | --- | --- |
| per-second | 1 s | 1 h (3600 points) | ring buffer in memory |
| per-minute | 1 min: average hashrate, min/max temperature, average frequency, share of seconds throttled | 7 days in memory, forever on disk | memory + `telemetry/YYYY-MM.log` |

After a restart the daemon reloads 7 days of per-minute data from disk. Panels request history
with `/telemetry <1h|24h|7d>`.

### 10.4 Benchmark

`/bench [seconds]` (default 10) runs 1, 2, 3 and 4 threads in turn on a test input without
targets. Result: hashes/s total and per core, scaling efficiency (rate of N threads / (N × rate
of 1 thread)), temperature at start and end, whether throttling occurred. Results go to the
device journal so optimizations and heatsinks can be compared.

Power measurement is out of scope for v1 (the Pi Zero 2 W has no current sensor).

---

## 11. Non-functional requirements

### 11.1 Language and dependencies

- C17. One `Makefile`, no CMake or generators.
- **Allowed:** libc (glibc), pthreads, Linux kernel interfaces — sysfs, procfs, termios, `ioctl`
  on `/dev/vcio`, Unix sockets, `poll`, signals; compiler built-ins such as `arm_neon.h`.
- **Forbidden:** ncurses, readline and any third-party library, header-only included; invoking
  external programs at runtime (`vcgencmd`, `tmux`, `sh`). Tests may use standard tools
  (`sha256sum`), the program may not.
- Written in-house: SHA-256, terminal rendering, line editor, config and protocol parsers,
  character-width table, Aho–Corasick, PRNG if ever needed.
- Must compile with zero warnings under both gcc and clang.

### 11.2 Build and install

| `make` target | Action |
| --- | --- |
| `make` | release build: `-O3 -Wall -Wextra -Werror`; adds `-mcpu=cortex-a53` when the target is aarch64 |
| `make debug` | `-O0 -g -fsanitize=address,undefined` |
| `make CC=clang` | same with clang |
| `make CROSS=aarch64-linux-gnu-` | cross-compile for the Pi (optional; `STATIC=1` links statically to avoid glibc version mismatch). Building natively on the Pi is the default path. |
| `make test` | unit tests |
| `make bench` | SHA-256 micro-benchmark without the daemon |
| `make run-dev` | run the daemon in the foreground with state in `./.dev/` |
| `sudo make install` | binaries to `/usr/local/bin`, data to `/usr/local/share/berejlab`, unit to `/etc/systemd/system`; creates user and group `berejlab` |
| `sudo make uninstall` | removes everything except `/var/lib/berejlab` |

Build output goes to `build/release/` and `build/debug/`.

Development mode needs no root:
`berejlabd --foreground --state-dir ./.dev/state --socket ./.dev/berejlab.sock` and
`berejlab --socket ./.dev/berejlab.sock` (or `BEREJLAB_SOCKET`).

systemd unit:

```ini
[Unit]
Description=berejlab hash laboratory
After=local-fs.target

[Service]
Type=simple
ExecStart=/usr/local/bin/berejlabd
User=berejlab
Group=berejlab
SupplementaryGroups=video
RuntimeDirectory=berejlab
RuntimeDirectoryMode=0750
StateDirectory=berejlab
Restart=on-failure
RestartSec=5
NoNewPrivileges=yes
ProtectSystem=strict
ProtectHome=yes
PrivateTmp=yes

[Install]
WantedBy=multi-user.target
```

### 11.3 Performance

The absolute hashrate is set by the first benchmark (stage 2) and becomes the baseline. Desktop
numbers are not representative of the Pi. Relative goals:

| Metric | Goal |
| --- | --- |
| scaling to 4 cores (no throttling) | ≥ 3.6× one core |
| overhead of 10 targets | ≤ 10% of pure hashing |
| daemon without workers | < 1% CPU |
| client | ≤ 2% of one core |
| memory, daemon / client | ≤ 32 MB / ≤ 16 MB RSS |
| UI response to a key at 4/4 load | < 100 ms |

SHA-256 speed-ups, simple to hard:

1. One block per hash (word ≤ 55 bytes). Word length is constant, so padding and length are
   prepared once per project.
2. Next word = odometer increment directly in the ASCII buffer. Position → word conversion only
   at chunk start.
3. Precompute rounds whose message words do not depend on the variable parts. The longer the
   constant prefix before the first variable part, the more rounds are computed once per chunk.
   `/preview` shows how many rounds are saved.
4. To be evaluated: NEON, 4 hashes in parallel in 128-bit registers (via the compiler's built-in
   `arm_neon.h`, not an external dependency). The gain on Cortex-A53 is unknown in advance and
   is measured with the benchmark. Implementation selectable in the config.

### 11.4 Reliability

- The daemon runs for weeks without restart: no memory or descriptor leaks (ASan and a 24-hour
  run).
- Workers run with `SCHED_IDLE` so SSH, the system and the UI stay responsive at full load.
- SIGTERM: workers finish their current chunk, the daemon writes a checkpoint and exits within
  2 s.
- After start the daemon resumes the project that was `running`.
- At start the daemon self-tests SHA-256 against reference vectors; on mismatch it refuses to
  enumerate.
- Every find is re-computed with the reference (unoptimized) implementation before it is written,
  so a bug in the fast path can never reach the results.

### 11.5 Security

- The daemon runs as user `berejlab`, not root; only group `berejlab` can access the socket.
- No network sockets, no execution of external commands.
- Project names: `[a-z0-9_-]`, up to 32 characters (prevents path traversal). `/export` writes
  only to `export/`.
- Every protocol input line is length-limited and validated before parsing.

---

## 12. Development plan

Development runs in 8 stages; each ends with a working, verifiable result and a commit on `dev`.

### 12.1 Repository layout

```
berejlab/
  Makefile
  README.md
  LICENSE                MIT
  CLAUDE.md              rules for Claude Code
  docs/
    SPEC.md              this document
    protocol.md          exact reply format of every command (stage 4)
    bench.md             performance measurement history
  src/
    common/              sha256 (fast + reference), conf, proto, commands (command table), utf8, util
    daemon/              main, engine, worker, template, targets, detectors/,
                         store, telemetry, vcio, server, events, mascot
    client/              main, term, render, input, complete, hints, feed, header,
                         panels, mouse, oneshot
  data/
    berejlab.service
    berejlab.conf
    phrases.txt
    dict/hexwords.txt
  tests/
    test_*.c             unit tests on an in-house mini framework (macros)
    verify.sh            cross-check finds with sha256sum
```

### 12.2 Stages

1. **Skeleton.** Repository layout, Makefile with all targets from §11.2, test mini-framework,
   both binaries with `--version` / `--help`.
   - Done when: `make`, `make debug`, `make test`, `make CC=clang` pass with zero warnings.
2. **SHA-256 and benchmark.** Reference implementation (FIPS 180-4) and fast single-block one.
   `make bench`.
   - Done when: both pass NIST vectors; the fast one matches the reference on 1M random inputs
     (in-process) and the reference matches `sha256sum` on 2000 random inputs (`make verify-sha`);
     baseline hashrate on 1 and 4 threads recorded in `docs/bench.md`.
3. **Search engine.** Template parser (word ↔ position, odometer), all detectors and
   combinations from §5, coverage map, worker pool, chunks. Runs without the daemon:
   `berejlabd --foreground --oneshot` prints finds to stdout.
   - Done when: every detector has positive and negative tests; finds pass `tests/verify.sh`;
     scaling and target overhead measured (§11.3).
4. **Daemon.** Storage and checkpoints, sessions with periods, rest and queue, coverage map
   persistence, telemetry (sysfs + mailbox, graceful fallback), socket and protocol, all daemon
   commands, mascot, systemd unit, `make install`. `docs/protocol.md` written alongside.
   - Done when: the daemon is fully controllable through socat; after `kill -9` and after a power
     cut the project resumes, no find is lost, a time period finishes its remaining time, covered
     intervals are not re-hashed; a 24-hour run shows no RSS growth.
5. **Client: core.** Raw mode, double-buffered renderer, header, feed, line editor with history,
   completion and parameter hints, sandbox, one-shot mode `berejlab /command`.
   - Done when: every command of §7 works over SSH; resizing does not break the picture; the
     terminal is restored after a client crash.
6. **Client: polish.** Panels `/stats`, `/temp`, `/finds`, `/targets`; mouse; OSC 52; compact
   mode; colours and `NO_COLOR`.
   - Done when: checked in two terminals (e.g. the desktop terminal and tmux inside it); the
     client meets the CPU and latency goals of §11.3.
7. **Optimization and research (on the Pi).** Round precomputation, NEON 4-way, comparison via
   `/bench`; heatsink and case experiments.
   - Done when: `docs/bench.md` has a "variant → hashrate → temperature" table; the best variant
     is the default.
8. **Stabilization, v1.0.** 7-day continuous run on the Pi, README with install guide and
   examples.
   - Done when: 7 days without crashes or memory growth; every find passes `verify.sh`.

---

## 13. Open questions and future ideas

To decide during v1:

- [ ] Default device / mascot name (placeholder `bit-muncher`).
- [ ] Colour scheme: one fixed theme or several.
- [ ] Chunk size and checkpoint interval — tune after the stage 2 benchmark.
- [ ] Flood threshold (10/s) and default `save` (1000) — check in practice.
- [ ] Default session on `/start` without a plan — currently `time 1h forever`.

Ideas for v2+:

- Templates longer than 55 bytes (two SHA-256 blocks with a precomputed first block).
- Other hash functions: double SHA-256, SHA-1, MD5, BLAKE2. Hide hashing behind a backend
  interface already in v1.
- New project type: partial collision search (two inputs with the same hash prefix). Bound by
  512 MB RAM rather than CPU.
- Small display on the Pi itself (OLED or e-ink) with the mascot and telemetry, like pwnagotchi.
- Notifications about finds outside SSH. Requires networking, so revisits the "no network"
  principle of §1.
- Manual input of USB power-meter readings to compute hashes per joule.
