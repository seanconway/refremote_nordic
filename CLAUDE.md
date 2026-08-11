# Working notes for RefRemote

This file is the **entry point** for anyone — human or agent — picking this project up: what the documents are, how the repos relate, and the working knowledge that is **not** written down elsewhere — the traps, the conventions, and the reasoning behind choices that look arbitrary from the code.

It is not a substitute for the specifications. Where they and this file disagree, they win.

## 1. The documentation scheme

Each document has one job. Read in this order; later documents answer to earlier ones.

| Document | Job | Authority |
|---|---|---|
| `SCOPE.md` | What the system is, who it is for, what is out of scope | **Authoritative on direction**, with `SYSTEM_FUNC_SPEC.md` |
| `SYSTEM_FUNC_SPEC.md` | How the system behaves | **Authoritative on behaviour.** Cited as "FS §n" |
| `PROTOCOL.md` | The dongle ↔ scoreboard wire contract | Answers to both of the above. Byte-identical in both repos |
| `RADIO_PROTOCOL.md` | The dongle ↔ remote radio contract. The counterpart to `PROTOCOL.md`, and `PROTOCOL.md` §12 is its acceptance criteria | Answers to both of the above. **This repo only** — the app never sees the radio |
| `PLAN.md` | **The living status document**: current state (§1), completed work (§2), planned work and the embedded roadmap (§3, with §3.3 mapping the milestones onto the development phases), binding decisions (§4), the two validation ladders — USB V0–V8 and radio W0–W8 (§5–§6), risks and gaps (§7–§8), results log and version history (§9) | Answers to all of the above |
| `dongle/BUILD_SPEC.md`, `remote/BUILD_SPEC.md` | **What to build.** Module boundaries, interfaces, state, algorithms, per-stage acceptance. `PLAN.md` says what state the project is in and why the work is ordered as it is; these say what to build | Answer to the protocols. **More specific on mechanism; `PLAN.md` wins on sequence and status** |
| `README.md` (this repo) | Overview of the **embedded domain**: system composition, hardware, the stateless-remote architecture, layout | Orientation |
| `dongle/README.md` | The **dongle firmware** specifically: layout, build, flash, manual test, configuration | Orientation and procedure |
| `dongle/BOARD.md` | **The dongle hardware.** LEDs, button, flash map, the REGOUT0 reset — facts not derivable from the firmware, each cited to an in-tree board file. Exists because the LED question was answered from alias names twice and wrong both times | Reference |
| `wrsl-app/README.md` | The **web application**: responsibilities, architecture, design system, host requirements, deployment | Orientation |
| `CLAUDE.md` | This file — entry point, conventions, traps | Answers to everything above |

If a change would contradict `SCOPE.md` or `SYSTEM_FUNC_SPEC.md`, **that is a conversation, not an implementation detail.** Those two documents were composed deliberately; treat a conflict as a signal that the implementation is wrong, and raise it.

`PLAN.md` is a **living document** — when work completes or direction changes, it moves from §3 (planned) to §2 (completed) and the status tables and results log are updated in the same change. A PLAN.md that lags the code is worse than no PLAN.md.

## 2. Two repos, one product

| Repo | Holds |
|---|---|
| `refremote_nordic` (this one) | Firmware: dongle, later the remotes. All five specification documents and `PLAN.md`. |
| `wrsl-app` | The scoreboard web application. |

`PROTOCOL.md` and `README.md` exist in both. **`PROTOCOL.md` must be byte-identical across the two** — edit it in `refremote_nordic` and copy.

### The hard-link trap

The two copies were once kept in step by a filesystem hard link. That does **not** survive an editor writing a new file rather than modifying in place — it broke silently during the v3.0 revision and left the repos on different versions with no indication at all. Copy explicitly and verify:

```powershell
Copy-Item "refremote_nordic\PROTOCOL.md" "wrsl-app\PROTOCOL.md" -Force
Get-FileHash "refremote_nordic\PROTOCOL.md","wrsl-app\PROTOCOL.md" | Select-Object Hash,Path
```

Two identical hashes or it did not work. This is recorded as a binding decision in `PLAN.md` §4.7.

### The working-directory trap

The shell's working directory persists between commands, and the two repos sit side by side. A `git push` typed after a command that happened to `cd` into the other repo pushes the wrong repo — this has already happened once. **Always use `git -C <path>`** for anything that writes.

## 3. Commands

**Scoreboard app** (`wrsl-app`, Node/Vite):

```bash
npm run dev      # dev server, http://localhost:5173/
npm test         # vitest, 137 tests across 4 files
npm run lint     # eslint; `design-system` and `dist` are ignored
npm run build    # production build — run before claiming done
```

**The dongle emulator** is at `http://localhost:5173/emulator.html` once the dev server is up, with the scoreboard alongside it at `http://localhost:5173/?anyport`. It plays the dongle half of the protocol over a real serial link and mocks up both remotes — pressable buttons, LEDs, haptics — so the whole system can be run in software. It needs a virtual serial pair; **use Free Virtual Serial Ports, not com0com**, whose 2017 driver signature fails with Code 52 on current Windows. See `wrsl-app/README.md`. Its model, `wrsl-app/src/emulator/dongleModel.js`, is the executable reference the firmware has to match: **a change to `PROTOCOL.md` lands there too, not only in `DongleService`.**

**Dongle firmware** (`refremote_nordic`, NCS v3.4.0):

```bash
source dongle/tools/ncsenv.sh
west build -b raytac_mdbt50q_cx_40_dongle/nrf52840 dongle -d dongle/build
# artifact: dongle/build/dongle/zephyr/zephyr.hex

# The no-radio baseline — currently the default, and kept working forever:
west build -b raytac_mdbt50q_cx_40_dongle/nrf52840 dongle -d dongle/build-noradio -- -DCONFIG_DONGLE_RADIO=n

# From stage 4, the DK remote:
west build -b nrf52840dk/nrf52840 remote -d remote/build
```

`-DCONFIG_DONGLE_RADIO=y` deliberately refuses to configure until `src/radio_ble.c` exists at stage 3.

Flash by holding the board button while plugging in (the LED fades), then writing the hex with nRF Connect Programmer. **Not** by pressing RESET — that is the other dongle.

**Host tests** — the two environments are different and neither substitutes for the other:

```bash
source dongle/tools/hostenv.sh          # a HOST compiler — not ncsenv.sh
cd dongle/tests/protocol && make check   # 131 checks, green since 2026-08-11
```

At stage 3, `cd tests/rframe && make check` joins it for the radio frame codec; that suite is not written.

**The host compiler is MinGW-w64 GCC 16.1.0, installed 2026-08-11** with `winget install --id BrechtSanders.WinLibs.POSIX.UCRT -e`. Before that there was no host C compiler on this machine at all — no WSL, no clang, and none inside the NCS bundle, whose `mingw64/bin` holds 50 executables and not one a compiler. **The board toolchain cannot substitute for a host one**: `arm-zephyr-eabi-gcc` emits binaries this machine cannot execute. That assumption is what left the suite unrun from M0 to M2, across four days and two milestones.

**Nothing runs `make check` for you.** It is not wired into `west build` and there is no CI. Run it after any change to `protocol.c`.

**Nordic and Zephyr questions:** use the `nordic-mcp` server (`nordicsemi_search_sources`, `read_resource`) and the installed NCS tree. Do not fetch Nordic or Zephyr documentation from the web.

## 4. Conventions that are load-bearing

### 4.1 Comments say why, not what

The codebase carries a high comment density, and deliberately so: most comments record a decision, a constraint discovered the hard way, or a thing that will look wrong to the next reader. A comment restating the code is noise; a comment explaining why the obvious approach was rejected is the point. Match this. When removing code, check whether its comment recorded something that is still true.

### 4.2 One path into match state

Every input — a referee's press, an operator's click on the software remotes — dispatches the identical `INPUT` action through the identical reducer path, with the same gesture timing. There is deliberately **no operator shortcut** that bypasses what a referee can do.

A second path into scoring state is a second thing that can be wrong, and it diverges silently. If a feature seems to need one, it doesn't.

### 4.3 Time has two domains and they must not be mixed

- **`performance.now()`** (`monotonicNow()` in `src/match/clock.js`) is the clock of record. Elapsed time is always a *subtraction of two readings*, never an accumulation of ticks — accumulated ticks drift and a skipped frame becomes lost match time.
- **`Date.now()`** is read only as a *corroborating witness*, to detect that the machine suspended, and for wall-clock stamps in logs and the match record.

The reducer and `DongleService` must agree on which domain a timestamp is in. A `Date.now()` value compared against a `performance.now()` value produced a real bug during M1: the numbers are both "milliseconds" and both plausible, so nothing throws — the comparison is just meaningless. Anything named `*Mono` is monotonic; anything named `*Wall` is not.

### 4.4 Fail closed on the wire

A malformed line is discarded, not guessed at. The clearest case: a v2.0-shaped `EVT ADD_POINT RED 17` has three arguments where v3.0 needs four, and it **must be rejected**, not read as a gestureless press. `PROTOCOL.md` §14 T7 pins this and there is a test for it. Resynchronisation happens at the next `\n`, never at a chunk boundary — a read boundary carries no information about the stream.

### 4.5 Late is worse than never

A haptic acknowledgement outside its ~120 ms window must degrade to **silence**, not arrive late. The referee's rule is "no tap means the press did not land, press again" — a late tap makes them score twice, and the failure looks like referee error. Any timeout on this path drops the signal rather than deferring it.

### 4.6 The distinction between inert and no-op

- **Inert** — a ruleset leaves F1 unassigned. No action, no haptic, no indicator, no trace. `ACK … SILENT`. The control is dead, because a rejection signal is more confusing than silence.
- **No-op** — a legitimate press that happens to change nothing, such as `REMOVE_POINT` at the score floor. **Full acknowledgement tap.** The referee needs to know the press registered.

These look alike in the reducer and are not alike. Both have tests.

## 5. Design system

`wrsl-app/design-system/` is vendored from `RefRemote Design System.zip` (gitignored; the extracted tree is what ships). Import it whole via `src/index.css` rather than cherry-picking tokens.

| Rule | Why |
|---|---|
| **Acid lime `--lime-500` means live / valid / go, and nothing else.** One lime element in view is normal; three is a bug | Its whole value is scarcity. A lime "Clock" button on a stopped clock says "running" when it is not |
| Athlete red and green are **functional, not brand** | They are fixed by the ruleset and are how the corners are identified. Never restyle, never theme, never let a browser adjust them |
| `--touch-glove` (64px) minimum for anything pressed during a live match | Gloved hands, glanced at, under time pressure |
| Nothing scrolls, nothing reflows | A scoreboard that moves under a referee glancing up costs them a second they do not have |
| Emphasis by **weight and size** before colour | The oversized tactile datum of FS §3.1 is dominant by mass, not by hue |
| **No network fetches** | `Icon.jsx` was replaced with a local inline-SVG glyph map because upstream pulled Lucide from a CDN. SCOPE.md §7.3 requires a complete match with the venue's network absent. The remaining exception is the Google Fonts import in `tokens/fonts.css` — known, logged in `PLAN.md` §8 |

Do not edit files under `design-system/` to fix an application problem. The one deliberate exception is `Icon.jsx`, and the reason is in a comment at the top of it.

## 6. Traps already hit

Each of these cost real time. None of them produces a useful error message.

| Trap | Symptom | Fix |
|---|---|---|
| **Chrome auto-dark-mode** | Every surface flattens to `rgb(24,26,27)` and every colour to one off-white — on a stock profile, not the developer's | `<meta name="color-scheme" content="dark">` **and** `:root { color-scheme: dark; }`. The `.rr-mat` class is inside `#root` and the browser decides before it gets there |
| **JSX comment inside a `&&`** | Parse error; `{/* … */}` directly inside a parenthesised `&&` expression is read as an object literal | Use a `//` comment inside the attribute list |
| **Cancelled Web Serial picker** | An operator dismissing a dialog trips the watchdog and drops the link, so it looks like an application fault | `.catch(() => {})` at every `connect`/`reconnect` call site |
| **`TEST 3` left on** | Suspends link supervision until `TEST 0` or reboot. Every supervision test then passes for the wrong reason | Send `TEST 0` first and confirm the reply |
| **`CONFIG_DONGLE_FAKE_LINK=y`** | Fabricated `LINK … CONNECTED` with synthetic RSSI and battery, so any test that appeared to validate link reporting was validating a constant | **Deleted 2026-08-11** — `PLAN.md` §4.11 — because a default is no protection when the failure mode is forgetting. Its replacement, `CONFIG_DONGLE_RADIO=n`, reports `DISCONNECTED`, which is true |
| **`CC ?= gcc` in a makefile** | `make` predefines `CC` as `cc`, so `?=` never fires and the recipe calls a compiler that exists on Unix and not on Windows | `ifeq ($(origin CC),default)`. The point is to override make's *guess* without overriding the user's *choice* |
| **`$USER` in a Git Bash script** | Unset, so an interpolated path becomes `/c/Users//…` and the failure reads as a missing install rather than a missing variable | `$HOME` |
| **A synthetic `battery_pct` on the DK remote** | The scoreboard's battery indicator validates a constant. **The same trap with no Kconfig symbol whose name gives it away** | `remote/BUILD_SPEC.md` §8.3 — make the synthetic value obviously synthetic rather than plausible |
| **Reading the dongle's LED hardware off its devicetree aliases** | `led0-green`, `led1-red`, `red-pwm-led`, `green-pwm-led` — all four are copied from the **Nordic** nRF52840 Dongle, which has a real RGB part. This board has two blue LEDs, one unfitted, and renders no colour. The wrong answer was derived twice in one day, in two different directions, and neither attempt produced an error | **Measure it.** `HAP RED LONG` then `HAP GREEN LONG` settled in 20 seconds what two rounds of reading could not. Raytac's pin table is wrong too — it has the pins transposed. `dongle/BOARD.md` §2 |
| **Fault indication is invisible on the dongle** | `indicator_error()` borrows the `RED` channel, which is the unfitted P0.08, so **no `ERR` blinks anything** — `APP_TIMEOUT` included. Left that way on purpose (`BOARD.md` §2.2): moving it to the fitted lamp would collide with every `GREEN` haptic and destroy the asymmetry that proves routing | Read the wire, not the lamp. **"No blink" never means "no error."** Errors always leave as `ERR` lines |
| **A second CDC-ACM instance** | Log output interleaves into the protocol stream; lines corrupt intermittently and silently | Console, shell and logging are off in `prj.conf`, and a `BUILD_ASSERT` in `usb_link.c` fails the build. This is the trap the upstream `cdc_acm` sample falls into |
| **Gating transmission on DTR** | The dongle enumerates but never answers `INFO` | Never gate on it. The app never calls `setSignals()` |

## 7. Verify in a browser, not just in the suite

Two M1 defects passed the tests, the linter and the production build, and were found only by driving the application in Chrome: a running-clock readout frozen at `00:00`, and the browser repainting the entire palette. Both are written up in `PLAN.md` §2.4.

The generalisation: **anything derived from a running clock must be watched for several seconds**, and **the developer's browser is not a representative browser.** Use the `claude-in-chrome` tools for this; the dev server is at `http://localhost:5173/`.

## 8. Current state, in one line

The application and the dongle firmware are both at protocol v3.0, the dongle is **flashed to 0.2.0 and answering on hardware**, and **the two ends have still never been connected** — because no browser has yet held the port, not because anything refuses. **The next action is V2, the app handshake.** This is **M2** (`PLAN.md` §3.1), one firmware programme in six stages; stages 0 and 1 are code-complete (§2.6), V0 is green, V1 and V3 are green on their wire half, and the emulator wire-log diff is clean (§2.7). Stages 2–5 add provisioning, the radio, and a DK remote, ending in a press on the DK scoring on the scoreboard and acknowledged back to it. Full status is `PLAN.md` §1.

**Three caveats on that green, all of which look like pedantry and are not.** The **`STATE` render is still unobserved** — accepted is byte-identical on the wire to a dead indicator; the haptic half closed on 2026-08-11 and `STATE` is two commands away, addressed to `GREEN`. **The dongle has one blue lamp and it is on P0.06, the `GREEN` channel** — `RED`/P0.08 is unfitted, so `HAP RED`, `STATE RED` and every `ERR` (which borrows `RED`) are invisible: *"no blink" never means "no error"*. That silence is also what proves routing, since a firmware ignoring the target would light the same lamp for both. `dongle/BOARD.md` is the reference; the devicetree aliases are not, and gave two different wrong answers before a measurement settled it. And **the COM port is exclusive**: a terminal and the app cannot both hold it, so terminal rungs and browser rungs have to be sequenced, not interleaved.

After M2: M4 (the 2:1 link) → M5 (full-feature remote on the DK) → M6 (custom PCB) → M7 (port and validate) → M8 (custom dongle, optional), mapped onto the development phases in `PLAN.md` §3.3. **M3's number is retired, not reused** — it was the separate radio milestone, now absorbed into M2 — because the last renumbering left stale references and a gap costs less than that.

**Three things about the ordering are load-bearing and look like pedantry until they bite:**

- **The no-radio baseline is a build configuration, not a milestone.** `CONFIG_DONGLE_RADIO=n` runs the whole wire layer with the radio compiled out, and it is kept for the life of the project — that is what makes §3.10's regression list attributable, and it beats a milestone that was green once.
- **M6 cannot open until R3, R4 and R6 have measurements.** Each has a hardware contingency behind it, and a board designed against a prediction is a board that gets respun.
- **The radio is plain BLE at 7.5 ms, and SCI is deferred** (`PLAN.md` §4.8). The interval buys retransmission headroom rather than latency, and whether the headroom suffices depends on an unmeasured number — so the baseline is the rung needing no exotic controller feature. It also means battery sizing should carry headroom, because a later move to 2.5 ms triples connection events.
