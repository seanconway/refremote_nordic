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
| `PLAN.md` | **The living status document**: current state (§1), completed work (§2), planned work (§3), binding decisions (§4), the validation ladder (§5–§6), risks and gaps (§7–§8), results log and version history (§9) | Answers to all of the above |
| `README.md` (this repo) | Overview of the **embedded domain**: system composition, hardware, the stateless-remote architecture, layout | Orientation |
| `dongle/README.md` | The **dongle firmware** specifically: layout, build, flash, manual test, configuration | Orientation and procedure |
| `wrsl-app/README.md` | The **web application**: responsibilities, architecture, design system, host requirements, deployment | Orientation |
| `CLAUDE.md` | This file — entry point, conventions, traps | Answers to everything above |

If a change would contradict `SCOPE.md` or `SYSTEM_FUNC_SPEC.md`, **that is a conversation, not an implementation detail.** Those two documents were composed deliberately; treat a conflict as a signal that the implementation is wrong, and raise it.

`PLAN.md` is a **living document** — when work completes or direction changes, it moves from §3 (planned) to §2 (completed) and the status tables and results log are updated in the same change. A PLAN.md that lags the code is worse than no PLAN.md.

## 2. Two repos, one product

| Repo | Holds |
|---|---|
| `refremote_nordic` (this one) | Firmware: dongle, later the remotes. All four specification documents and `PLAN.md`. |
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
```

Flash by holding the board button while plugging in (the LED fades), then writing the hex with nRF Connect Programmer. **Not** by pressing RESET — that is the other dongle.

**Host parser tests** — `cd dongle/tests/protocol && make check`. These have never run; there is no C compiler on this machine. See `PLAN.md` §5 rung V0.

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
| **`CONFIG_DONGLE_FAKE_LINK=y`** | Fabricates `LINK … CONNECTED` with synthetic RSSI and battery. Any test that appears to validate link reporting is validating a constant | Set it to `n` before any link test |
| **A second CDC-ACM instance** | Log output interleaves into the protocol stream; lines corrupt intermittently and silently | Console, shell and logging are off in `prj.conf`, and a `BUILD_ASSERT` in `usb_link.c` fails the build. This is the trap the upstream `cdc_acm` sample falls into |
| **Gating transmission on DTR** | The dongle enumerates but never answers `INFO` | Never gate on it. The app never calls `setSignals()` |

## 7. Verify in a browser, not just in the suite

Two M1 defects passed the tests, the linter and the production build, and were found only by driving the application in Chrome: a running-clock readout frozen at `00:00`, and the browser repainting the entire palette. Both are written up in `PLAN.md` §2.4.

The generalisation: **anything derived from a running clock must be watched for several seconds**, and **the developer's browser is not a representative browser.** Use the `claude-in-chrome` tools for this; the dev server is at `http://localhost:5173/`.

## 8. Current state, in one line

The application is at protocol v3.0 (M1, complete); the dongle firmware is still at v2.0, so **the two ends do not interoperate right now** and the app correctly refuses the link at its major-version guard. Closing that is milestone M2 (`PLAN.md` §3.1); the emulator (§3.2) is how the application is validated in the meantime, and is the reference trace to diff the firmware against when it lands. Full status is in `PLAN.md` §1.
