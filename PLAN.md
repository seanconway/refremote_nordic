# RefRemote — Plan, Status and Validation

**Status as of 2026-08-11.** `SCOPE.md` and `SYSTEM_FUNC_SPEC.md` are the authority on direction; `PROTOCOL.md` **v3.0** is the revision that answers to them, and it is a breaking change against the v2.0 prototype. The scoreboard application is at v3.0 (M1, complete); **the dongle firmware is now at v3.0 too — flashed, and answering on hardware**, with the radio compiled out. The radio layer is **specified** — `RADIO_PROTOCOL.md` **v1.0** — but not implemented, and the remotes do not exist.

**M2 is in progress. Stages 0 and 1 are done — the no-radio baseline is closed, V0–V6.** It is one firmware programme in six stages, ending in an end-to-end demonstration: a press on an nRF52840 DK, scored on the scoreboard, acknowledged back as a rendered haptic on that DK. The implementable contracts are `dongle/BUILD_SPEC.md` and `remote/BUILD_SPEC.md`.

**V0 is green for the first time in the project's history** — 131 checks, 0 failures, on a host compiler installed 2026-08-11 (§2.6, §9.2).

**The dongle was flashed to 0.2.0 on 2026-08-11 and the wire layer answers correctly on hardware.** `INFO` returns `HELLO 3.0 0.2.0 RR-0000 0`; `TEST 1` emits seven events and `TEST 4` emits thirty-two; supervision fires at 2512 ms measured; the 120 ms acknowledgement budget shows all three of its bands. **The haptic and `STATE` downlinks both render, and per-remote routing is proven**, via the `RED`-dark/`GREEN`-lit asymmetry the board's single fitted LED makes visible ([`dongle/BOARD.md`](dongle/BOARD.md) §2.1). **The emulator wire-log diff is clean** — every difference between firmware and `dongleModel.js` is accounted for, and one of them was predicted in advance (§2.7).

**The application has now held the port, and V2–V4 are green.** Handshake reached `ready` with the correct identity fields, twice, from independent connections. `TEST 1` and `TEST 4` round-tripped through the live app with zero sequence gaps and zero duplicates, `SILENT`/plain `ACK` split correctly on the loaded ruleset's inert button, and the `F2` pending-choice assign/clear cycle confirmed on the wire (§5, V2–V3). **The reverse path is confirmed too**: beat-per-second on the owning remote only, ownership transfer moving the beat within one cycle, stop and deassign each producing exactly the wire traffic they should and no more, and the main-clock warning and period-end waveforms each firing exactly once, twice independently (§5, V4). One V4 row — burst suppression under four rapid `ADD_POINT`s — turned out not to be reachable from the operator UI at all, since an on-screen click has no remote to `ACK` to; recorded as a UI-reach gap rather than chased further. **V5 is now green too** (§5, §9.2) — all seven procedures, with the "remotes render link-lost" half of V5.1 correctly deferred to stage 4 rather than chased on hardware that doesn't exist yet. **V6 closes the stage-1 baseline, three rows green and two deferred rather than blocking**: V6.1's disconnect clause has no UI control to drive (a UI-reach gap, same shape as V4's), V6.2 and V6.3 pass outright, and V6.4 (needs scripting, not eyeballing) and V6.5 (needs a second dongle) are carried forward as items for a full-ladder quality-control re-run once M4/M5 hardware exists — §3.10 already establishes that pattern for the radio's own regression risks.

**This is a living document.** It carries the current state of the project (§1), the record of completed work and what it settled (§2), the planned work in order (§3), the decisions that still bind (§4), the full validation ladder (§5–§6), the unmeasured risks (§7), known gaps (§8), and the results log and version history (§9). The validation plan previously lived in `dongle/VALIDATION.md` and has been rolled in here (§5–§6), because a status document that points at a separate test plan gets read as a status document.

| For | See |
|---|---|
| What the system is and why | [`SCOPE.md`](SCOPE.md) |
| How it behaves | [`SYSTEM_FUNC_SPEC.md`](SYSTEM_FUNC_SPEC.md) |
| The wire protocol | [`PROTOCOL.md`](PROTOCOL.md) |
| The radio protocol | [`RADIO_PROTOCOL.md`](RADIO_PROTOCOL.md) |
| **What to build — the dongle** | [`dongle/BUILD_SPEC.md`](dongle/BUILD_SPEC.md) |
| **What to build — the remote** | [`remote/BUILD_SPEC.md`](remote/BUILD_SPEC.md) |
| Build, flash, manual test | [`dongle/README.md`](dongle/README.md) |

---

## 1. Current status

| # | Milestone | State |
|---|---|---|
| **M0** | USB link at v2.0, working on hardware | ✅ done — §2.1 |
| **M1** | Scoreboard application to v3.0, the functional specification, and the design system | ✅ done — §2.3 |
| **M2** | **The firmware programme** — dongle wire v3.0, the radio, and a DK remote, to an end-to-end demonstration | ▶ in progress — §3.1. **Stage 1 (the no-radio baseline) is closed**; V0–V6 all run, with V4 row 7, V5.1's remote-render half, V6.4 and V6.5 carried forward as deferred rather than blocking (§5, §9.2). **Stage 2 (provisioning) is closed 2026-08-12** — **A19 confirmed on hardware**, both the unprovisioned fallback and a written record, dongle side (§5, §9.2). **Stages 3 and 4 are code-complete 2026-08-12** — `radio_ble.c` (dongle, BLE central) and the whole DK remote firmware (GATT server, gesture classifier, haptic proxy, indicators) both build clean; **W0/W1/W2–W5 are all still unconfirmed on hardware** — nothing in either stage has touched a physical DK yet. **Next: flash both units, provision a set, and work the W-ladder** |
| **M3** | *Retired as a separate milestone — absorbed into M2* | §3.4 |
| **M4** | The **2:1** link — two peripherals, two connections, one central | §3.5 |
| **M5** | Full-feature remote firmware on the DK — GPIO buttons, RGB indicators, ERM, nPM1300 | §3.6 |
| **M6** | Custom remote PCB designed | §3.7 |
| **M7** | Firmware ported to the custom remotes; system validated on production-shaped hardware | §3.8 |
| **M8** | Custom dongle, for BOM cost — **optional** | §3.9 |
| **M9** | Deployment validation and MVP hardening — USB identity, ruleset verification, §6 and §8 | §3.11 |

**M2 through M8 are the embedded programme**, and §3.3 maps them onto the development phases. M9 is scoreboard-and-product work that runs alongside from M5 onward rather than after M8.

**M3 is retired rather than renumbered, and M4–M9 keep their numbers.** Merging the old M2 and M3 into one programme leaves a gap in the sequence, which is untidy. Renumbering would be worse: §9.3 records that the last renumber left references to "M4 — validation" pointing at a milestone that had become M9, with nothing to signal the change. A stable reference is worth more than a tidy sequence.

| Component | State |
|---|---|
| `SCOPE.md`, `SYSTEM_FUNC_SPEC.md` | Current. Authoritative. |
| `PROTOCOL.md` v3.0 | Written against the specification. Implemented on the app side only. Byte-identical in both repos. |
| `RADIO_PROTOCOL.md` v1.0 | Written against the specification and against `PROTOCOL.md` §12. **Implemented nowhere.** Amended 2026-08-10 in §12.2 and §12.3 — baseline BLE at 7.5 ms, SCI deferred (§4.8). Every latency figure in it is a prediction awaiting measurement. This repo only. |
| **`dongle/BUILD_SPEC.md`** | **New, 2026-08-10.** The implementable contract for the dongle: the radio seam, module boundaries, state, algorithms, acceptance |
| **`remote/BUILD_SPEC.md`** | **New, 2026-08-10.** The same for the DK remote, including what its reduced surface cannot claim |
| Scoreboard app | **v3.0, M1 complete.** 137 tests passing across protocol, reducer, service and emulator suites. Lint and production build clean. |
| Dongle USB firmware | **v3.0, 2026-08-11. Flashed and answering on hardware.** Buttons and gestures, `ACK`/`SILENT` at 120 ms with an active sweep, `STATE`/`CFG`/`HAP` relay, `JOIN`, supervision at 2.5 s with `DN_HOST` down, `TEST 0–4`, transmit drop counter. The dongle-side clock and heartbeat are **deleted**. 54 KB flash, 21 KB RAM. §2.6 for the build, §2.7 for what the hardware confirmed. |
| Dongle radio | **Code-complete 2026-08-12, unconfirmed on hardware.** `src/radio_ble.c` implements the full connection lifecycle (`dongle/BUILD_SPEC.md` §7.1) behind `CONFIG_DONGLE_RADIO=y`; builds clean, no-radio baseline (`radio_null.c`) unaffected. `CONFIG_DONGLE_FAKE_LINK` remains **deleted** (§4.11) — nothing fabricates a connection here either. |
| Remote firmware | **Code-complete 2026-08-12, unconfirmed on hardware.** Full DK remote per `remote/BUILD_SPEC.md`: GATT server (`link.c`), gesture classifier (`buttons.c`), PWM haptic proxy preserving the BEAT:TAP ratio (`haptic.c`), `LED_LINK` conjunction (`indicators.c`), synthetic telemetry (§8.3). Builds clean against `nrf52840dk/nrf52840`. Custom hardware still not designed — this is the DK prototyping platform the milestone table always specified. |
| Provisioning | **Closed on the dongle side, 2026-08-12.** `common/provisioning.h/.c` (Zephyr-free record, CRC32, validation — 54 host checks) and `dongle/src/provisioning_flash.c` (the `storage_partition` reader, wired into `engine_start()`) confirmed on real hardware: unprovisioned, `INFO` reports the `RR-0000` fallback; with `dongle/tools/provision.py RR-0001`'s record written to `storage_partition`, `INFO` reports `RR-0001` exactly. `dongle/tools/provision.py`'s output was written to a real board, not just cross-checked on the host. **The remote half ("on both boards") waits on stage 4 firmware.** It remains a prerequisite of the radio, not of manufacture — §4.10. |
| Host parser tests | **Green, 2026-08-11 — the first execution in the project's history.** 131 checks, 0 failures, covering §14 T1–T16 and the encoder round-trips. MinGW-w64 GCC 16.1.0 installed and recorded as `dongle/tools/hostenv.sh`. |
| Radio conformance tests | **W0 green, 2026-08-12** — `dongle/tests/rframe/`, `make check`: 20 checks (67 assertions), 0 failures, covering A1–A7 and A11 (`common/rframe.c`'s codec-level cases; A20 is structural — the codec offers no way to construct a silent tap). The `as.exe` `Permission denied` block that stopped this running earlier the same day was a transient Windows Defender cloud-reputation hold on the freshly-installed binary, not a code or path problem — it cleared on its own, confirmed by re-running both `rframe` and the pre-existing `protocol` suite (131/131, unaffected) as a control. §9.2. |
| Validation | **Stage 1 closed, V0–V6 all run**, 2026-08-11 — V0/V1 from a scripted terminal (§2.7), V2–V6 from the live application holding the port (§5, §9.2). The v2.0 passes of V1–V3 are **void** and have been superseded rather than carried. Three rows carried forward rather than closed, each recorded rather than chased: **V4** burst suppression (no wire traffic from an on-screen click), **V5.1**'s remote-render half (no remote exists yet — moves to W5/A15 at stage 4), and **V6.4/V6.5** (need scripting and a second dongle respectively — deferred to a full-ladder QC re-run once M4/M5 hardware exists). §5 carries the rungs as per-stage exit criteria. |

**The two ends have been connected, in both directions, and the no-radio baseline is closed.** The dongle runs 0.2.0 and speaks v3.0, the application has held the port repeatedly and reconnected through every scenario that doesn't need scripting or a second unit. **Stage 2 — provisioning — is now closed on hardware too**: `INFO` reports `RR-0000` unprovisioned and `RR-0001` once a `provision.py`-generated record is written to `storage_partition`, confirmed on the physical dongle, not just on the host. `ERR NO_PROVISIONING` itself was not independently caught on the wire — it fires once, at boot, before any terminal can have the port open, which is a harder version of the same timing problem V5.1 hit — but the fallback serial it accompanies is the repeatable, on-demand evidence that the same code path ran.

**Stages 3 and 4 are code-complete, 2026-08-12** — the dongle's BLE central (`radio_ble.c`) and the whole DK remote firmware were written together rather than sequentially, since stage 3's central needs stage 4's peripheral to test against and there was never a hard gate requiring stage 3 to close in isolation first (the one sequencing discipline that *is* kept: `rframe.c` was written and independently checked before either side's radio code touched it — the same discipline `protocol.c` and `provisioning.c` already established). Both firmwares build clean, and **W0 is now formally green** (`make check`, 67 checks, 0 failures — the earlier `as.exe` block was transient and unrelated to the code; §9.2). **Nothing about either firmware has touched real hardware.** The next action is exactly what stage 3's and stage 4's exit criteria both point at: flash a dongle and a DK, write one `provision.py` set across both, and work the W-ladder starting at W1.

**The COM port is exclusive, and this is now a live constraint rather than a note.** Everything in §2.7 was driven by a scripted terminal holding COM13; §5's V2–V3 closure was driven by the application. The application cannot connect while a terminal handle is open, so the terminal rungs and the browser rungs cannot be interleaved — close one before starting the other.

---

## 2. Completed work

### 2.1 M0 — the USB link at v2.0, proven on hardware

**Proved on real hardware, in both directions, at v2.0:** the handshake, the PING cadence, clock state and the 1 Hz heartbeat, link supervision with `ERR APP_TIMEOUT`, end-to-end confirmation, and all TEST modes driving the scoreboard. V1–V3 passed on 2026-08-07 (§9.2) — they are void at v3.0, because the message set they exercised no longer exists, but they are cheap to re-run and must be, after M2.

That is a real result and it is what made the v3.0 revision cheap: the transport, the framing, the supervision model and the build and flash path are all proven, and the revision touches the message set above them.

### 2.2 The v3.0 protocol revision

`PROTOCOL.md` §15 lists the changes. Three of them were not refinements — the v2.0 design contradicted the specification and could not be extended into compliance. Recorded here because each one is a class of mistake, not just an instance:

1. **The wire carried officiating meaning.** v2.0's `EVT TIME_UP` / `PERIOD_UP` named operations, not buttons. FS §7.3 requires that no message carry ruleset meaning; FS §7.4 requires that adding a ruleset be a scoreboard change only. Worse, those tokens hard-coded a *default* assignment that `SCOPE.md` §9.2 has already marked for post-MVP remapping — under v2.0, letting a referee remap `FORWARD` would have meant a firmware release. v3.0 transports `<button> <gesture> <src>` and assigns meaning in the scoreboard. This was the single most consequential finding of the specification pass: the prototype protocol looked correct because it worked, and was structurally unable to reach the product.

2. **The heartbeat was generated on the dongle.** v2.0 put the 1 Hz heartbeat on the dongle, driven by `CLOCK RUN` / `CLOCK STOP`, to save a per-second packet. FS §6.2 rejects that architecture by name: a remote or dongle beating on stale ownership state reports one thing on the referee's wrist while the scoreboard reports another, quietly and with no self-correcting mechanism. It was also wrong for the product in a way invisible at the prototype — it beat on both remotes whenever the clock ran, where the real heartbeat beats on **the owning athlete's remote only**, which is ownership the dongle cannot know because it is match state. Per-beat commanding costs one message per second and buys no stale-state divergence, continuous end-to-end liveness proof during exactly the periods the referee depends on the system most, and a remote that stays fully stateless.

3. **The acknowledgement budget was four times too loose.** v2.0 gave confirmation a 500 ms window because confirmation was a convenience. FS §5.3 makes it a **functional requirement of the scoring interface**: multi-point actions are entered as repeated presses, and viability rests on the referee feeling each press land. The budget is ~120 ms inclusive of retries, decomposed in `PROTOCOL.md` §11. A late tap is worse than no tap, and that asymmetry now drives the design: exceeding the budget must degrade to silence.

### 2.3 M1 — the scoreboard application at v3.0

| Item | Delivered as |
|---|---|
| `protocol.js` to v3.0 | `src/protocol/protocol.js`. Every message in `PROTOCOL.md` §3, all cases in §14, chunk-boundary reassembly, `seq` over the 65536 modulus |
| `DongleService` to v3.0 | `src/dongle/DongleService.js`. Handshake with `CFG` + `STATE`, 1 s `PING`, 2.5 s supervision, `ACK` with dedupe, `JOIN` → forced `STATE`, `HAP`, running counters |
| Ruleset configuration | `src/match/rulesets.js`. The FS §12.2 schema as pure data: NFHS, NCAA, UWW freestyle and Greco, IBJJF, SJJIF, ADCC, generic no-gi |
| Match model | `src/match/matchReducer.js`. Score with floor, periods and phases, counters with ladder position, tri-state flag, secondary clock in both polarities, freestyle action grouping, action log |
| Clock of record | `src/match/clock.js`. Monotonic subtraction; wall clock read only as a corroborating witness; divergence halts the match and asks |
| Persistence | `src/match/persistence.js`, state version 3. Survives reload and browser restart; restore prompt on load |
| Watchdog | `src/match/useWatchdog.js`. Drops the serial link deliberately on a match-state stall (FS §8.3) |
| Display | `Scoreboard.jsx` primary tier, `DetailPanel.jsx` secondary tier, on the vendored design system and the `.rr-mat` surface |
| Pre-match confirmation | `PreMatch.jsx` — ruleset, period structure, F1/F2 legend, colour assignment, set serial, link and battery (FS §12.3) |
| Soak instrumentation | Running counters (`evtReceived`, `seqGaps`, `duplicates`, `beatsSent`, `beatsSuppressed`, ack latency p99 and max, connection uptime) plus a JSON diagnostics export. **This closes the gap that made V8 unfalsifiable** |
| Tests | 113 across `protocol.test.js`, `matchReducer.test.js`, `DongleService.test.js`. All hardware-free |

**Also settled during M1, and binding on M2:**

- **The operator controls are not a second path into match state.** Every control dispatches the identical `INPUT` action a real press produces — same button, same gesture, same reducer path, same firmware gesture timing (600 ms hold, 150 ms repeat). A second path into scoring state is a second thing that can be wrong, and it would diverge silently.
- **An inert button and a no-op press are different.** An inert button (a ruleset that leaves F1 unassigned) produces no action, no haptic, no indicator and no trace — `ACK … SILENT`. A press that legitimately changes nothing, such as `REMOVE_POINT` at the score floor, still earns a full-amplitude tap, because the referee needs to know the press registered. This distinction is in the reducer and the firmware must not flatten it.
- **The design system is vendored, not fetched.** Its `Icon.jsx` pulled Lucide from a CDN at first render; it was replaced with a local inline-SVG glyph map, because SCOPE.md §7.3 requires a complete match with the venue's network absent.

Two defects were found by driving the application in a real browser that neither the test suite nor the build could have caught — §2.4. Browser verification is not optional for this class of work.

### 2.4 What browser verification caught that testing did not

Recorded because it generalises. Both defects passed the unit suite, the linter and the production build.

1. **A frozen readout beside a live one.** The detail panel showed accrued secondary-clock time stuck at `00:00` while the primary tier counted up — it rendered the accumulator's stored base instead of its value at `now`. The reducer was correct; the reading of it was not. **Anything derived from a running clock has to be evaluated at a `now`, and the only way to see that it wasn't is to watch it for several seconds.**
2. **The browser repainting the brand palette.** Chrome's auto-dark-mode flattened every surface to `rgb(24,26,27)` and every colour to one off-white on a stock profile. That takes the athlete red and green with it — and those are fixed by the ruleset and are how the corners are identified, so a browser adjusting them is a correctness failure, not a cosmetic one. Fixed by declaring `<meta name="color-scheme" content="dark">` and `:root { color-scheme: dark; }`. **This extends D2**: the browser matrix must be checked on a stock profile, because the developer's browser is not a representative one.

### 2.5 Constraints discovered in implementation, now written into the spec

Real, discovered during implementation, each of which would present as a silent mystery rather than an error. All four are now recorded in `PROTOCOL.md` — the spec should describe what shipped.

1. **`LINK <remote> CONNECTED` without an RSSI value is rejected.** Firmware must always emit it when connected. Symptom if violated: the signal indicator silently never updates. Now mandatory in `PROTOCOL.md` §7.
2. **Resynchronisation happens at the next `\n`, never at a chunk boundary.** A read boundary carries no information about the stream. The app had a bug here (fixed 2026-08-07); `PROTOCOL.md` §2.2 carries the clarifying paragraph and T9c pins it.
3. **Nothing but the protocol may write to the CDC-ACM port** (§4.4). If either guard is relaxed, log output interleaves with protocol traffic, corrupting lines intermittently and silently.
4. **The acknowledgement must fire on the originating remote only**, routed by the `src` recorded against that `seq`. A broadcast tap is indistinguishable from a correct one in single-remote bench testing and wrong in every real match.

### 2.6 M2 stages 0 and 1 — the wire layer at v3.0, radio compiled out — 2026-08-11

**Delivered:** a host toolchain, a rewritten host suite, the dongle wire layer at v3.0, and the `radio.h` seam with `radio_null` behind it. Both build configurations are clean and V0 is green. The firmware was flashed the same day and the hardware run is recorded separately in §2.7; **stage 1 is still not closed**, because V2 and V4–V6 need the application and no LED has been observed.

**The toolchain.** MinGW-w64 GCC 16.1.0, installed with `winget install --id BrechtSanders.WinLibs.POSIX.UCRT -e` and recorded as `dongle/tools/hostenv.sh` beside `ncsenv.sh`. Two things needed fixing before it ran, and both were the same shape — a default that looked like a setting:

- The makefile's `CC ?= gcc` never took effect. `make` defines `CC` as `cc` among its built-in variables, so `?=` saw it as already set and invoked a compiler that exists on Unix and not here. Now `ifeq ($(origin CC),default)`, which distinguishes "the user chose a compiler" from "make guessed one".
- `hostenv.sh` interpolated `$USER`, which Git Bash leaves unset, producing `/c/Users//AppData/...` and a message that read as a missing install rather than a missing variable. Now `$HOME`.

The suite then found one real defect on the first compile, which is what writing tests first is for: `-Wsign-compare` under `-Werror` on an `int` loop counter compared against an enum-typed field. GCC 16 produced no other new diagnostics, and `-Werror` was not weakened.

**V0: 131 checks, 0 failures.** T1–T16 including both fail-closed cases, every button × gesture round-trip through the encoder and back, and the `LINK` RSSI constraint. Three cases are worth naming because each pins something that would otherwise be silent:

- **T7** — the v2.0-shaped `EVT ADD_POINT RED 17` is refused, and the token count is checked *exactly*. A `>= 4` test would have accepted a five-field line and a `>= 3` test the v2.0 form itself.
- **T11** — `ACK 65536` is refused. With `seq` in a `uint16_t` the v2.0 range check became vacuous, and 65536 would have narrowed silently to 0 and acknowledged a different event.
- **T13** — five hex characters invalidate the whole `STATE` line. The length is checked before the digits, because a digit-accumulating parser reads `00A0F` as a colour that is wrong but plausible.

**The wire layer.** `PROTO_VERSION` is `"3.0"`; firmware version `0.2.0`. Buttons and gestures replace the seven officiating actions; `CLOCK`, `EXPIRE` and `CONFIRM` are deleted rather than deprecated, so they now parse as unknown keywords — pinned by a test, because a dongle that still answered `CLOCK` would hold a clock FS §6.2 forbids it. Framing was not touched.

**Three decisions taken during implementation** that the build spec did not settle:

1. **`radio_init()` takes the engine workqueue as a parameter**, rather than the implementation reaching back for it. BUILD_SPEC §3 showed a one-argument form; the queue has to reach the radio somehow, and a parameter keeps `radio_ble.c` from depending on `engine.h`. `radio.h` forward-declares `struct k_work_q` so it stays free of `<zephyr/kernel.h>`. Spec amended to match.
2. **Engine initialisation is split in two.** The transport needs the workqueue before it can accept a byte and the engine needs the transport before it can say anything, so `main()` runs `engine_init()` → `usb_link_init(…, engine_workq())` → `engine_start()`. The alternative was a queue created at file scope by a `SYS_INIT`, which hides the ordering rather than stating it.
3. **`CONFIG_DONGLE_RADIO` defaults to `n` for now**, and `CMakeLists.txt` refuses `=y` with a sentence naming stage 3. BUILD_SPEC §10 specifies `y`; that flips when `radio_ble.c` exists. A missing-file link error is not a useful way to learn that a feature has not been written.

**Two obligations this leaves,** both recorded rather than resolved:

- ~~**The `TEST 4` count is pinned by a `BUILD_ASSERT`,** not by a test~~ — **discharged 2026-08-11 by §2.7.** The sweep table lives in `engine.c`, which is Zephyr-bound and outside the host suite, so the assertion was all that held it. The flashed dongle has now emitted the 32 events and they were counted. The `BUILD_ASSERT` stays: it is what stops the table drifting back to 21 between hardware runs.
- **A15 is unverifiable in this configuration.** `radio_send_host(…, false)` on supervision expiry is called and does nothing, because `radio_null` has nowhere to send it. A green no-radio run does not cover it; it is verified at stage 4 by watching `LED_LINK` on the DK.

### 2.7 The flash, and what a terminal could and could not confirm — 2026-08-11

Firmware 0.2.0 was flashed and the wire layer driven from a scripted terminal on COM13. **The first obligation of §2.6 is now discharged: the `TEST 4` count is executed, not merely asserted.**

| Observation | Result |
|---|---|
| `INFO` | `HELLO 3.0 0.2.0 RR-0000 0`, two `LINK … DISCONNECTED`, two `LOG counters` with `conn=none-noradio` |
| `PING`, `ECHO` | `PONG`; `ECHO` verbatim **including runs of spaces** — §5.7 exactly right |
| `HAP` to both remotes | **Accepted and rendered.** Confirmed by the `RED`/`GREEN` asymmetry below, which also establishes per-remote routing |
| `STATE`, `CFG` | Accepted with no error. **The visible effect was not observed** |
| Supervision | `ERR APP_TIMEOUT` at **2512 ms** after the last inbound line |
| `TEST 1` | Seven events, one per button, all `PRESS`, alternating RED/GREEN, `seq` contiguous, intervals 493–512 ms |
| `TEST 4` | **32 events, 16 per remote**, `HOLD_REP` on `FORWARD`/`BACKWARD` only, `seq` contiguous and unique |
| Malformed lines | `EVT ADD_POINT RED 17` (the v2.0 shape), a 5-char hex field and an out-of-range `seq` were each rejected with a `LOG` naming the reason. Fail-closed on hardware, not just in the suite |

**The 120 ms budget shows all three of its bands,** which is the part no LED can report. Timing an `ACK` against the emitting `EVT`:

| `ACK` sent | Table entry | Outcome | `late` counter |
|---|---|---|---|
| EVT+10 ms | found, fresh | tap fires | unchanged |
| EVT+107 ms | found, **budget spent** | **tap withheld** | **incremented** |
| EVT+302 ms | already swept | nothing, exactly as for an unknown `seq` | unchanged |

That middle row is §4.5 of `CLAUDE.md` working: a tap that could not arrive in time degrades to silence rather than arriving late. It is also the case an active sweep exists to make safe, and the third row shows the sweep doing its job.

**The emulator wire-log diff is clean** — the stage 1 exit criterion. Both ends were driven with one identical script and the traces normalised (`seq` rebased, since the firmware had been running and the model starts fresh). **All 39 `EVT` lines match exactly**: same buttons, gestures, remotes and relative sequence, across both sweeps. Every remaining difference is accounted for:

| Difference | Verdict |
|---|---|
| `LINK … CONNECTED -50 92` vs `DISCONNECTED` | Expected. The emulator mocks two remotes; `radio_null` reports the truth. This is the §5.2 standing trap, visible rather than hidden |
| No 10 s `LINK` re-emit from the firmware | Correct. `link_reemit_handler` re-emits only `CONNECTED` remotes, so under `radio_null` there is nothing to re-emit. Follows from the row above |
| `0.2.0-emulated` / `RR-0147` vs `0.2.0` / `RR-0000` | Deliberate. The emulator should be identifiable as one |
| Two `LOG counters` lines, firmware only | **A real emulator gap.** The model has no counters, so the app's counter path is never exercised against it — §8 |
| `ECHO  two   spaces ` preserved vs collapsed | **Predicted in advance** — the model reconstructs with `args.join(' ')`. The firmware is right and the emulator is wrong. Recording the prediction before running the diff is what made the diff trustworthy |
| `LOG ignored malformed line: …`, firmware only | Neither is wrong. §2.2 says "ignore silently, log locally", and a `LOG` line is the dongle's only local-log channel (§491); the emulator logs to its own UI instead |
| `ERR APP_TIMEOUT` one position earlier or later | A race between the 2500 ms supervision timer and the test timer. Not substantive |

**The indicator stand-in is alive, and per-remote haptic routing is proven — 2026-08-11.** This took two attempts, and the second answered more than the first asked.

**The board question, settled.** The devicetree presents `led0_d1` (P0.06) aliased `led0-green` and `led1_d2` (P0.08) aliased `led1-red`, and those alias names were read as evidence twice, giving two different wrong answers — "two separate lamps", then "one bi-colour package". **They are copied from the Nordic nRF52840 Dongle, which has a real RGB part, and describe nothing on this board.** Raytac's own pin table (`doc/index.rst` lines 41–42) says both LEDs are blue and one is unfitted — and its two pin numbers are **transposed** relative to the hardware. Measurement, not documentation, is what settled it. The reference is now [`dongle/BOARD.md`](dongle/BOARD.md), cited throughout, written so this is not derived from names a third time.

**The measurement**, with `TEST 3` first so supervision could not contribute a blink:

| Sent | Drives | Observed |
|---|---|---|
| `HAP RED LONG` | `led1` → P0.08 | **nothing** |
| `HAP GREEN LONG` | `led0` → P0.06 | **~500 ms blink** |

**The fitted lamp is on P0.06 — the `GREEN` channel.** And the asymmetry proves what a working pair of LEDs could not have: **a firmware ignoring the target and driving both channels unconditionally would have blinked on both commands.** It blinked on one, and the correct one. The missing LED is what made routing observable, which is the opposite of what was predicted an hour earlier.

**Two corrections to what was recorded above.** The earlier `HAP BOTH`/`HAP GREEN` blink was the haptic after all — both commands include `GREEN` — and it could not have been `indicator_error()`, which borrows `RED`, the pin nothing is on. And **every fault indication on this board is therefore invisible**; that is kept deliberately rather than moved to the fitted lamp, because errors already leave on the wire as `ERR` lines and haptics have no other channel ([`BOARD.md`](dongle/BOARD.md) §2.2). On this board, **"no blink" never means "no error".**

**What remains unproven:** that `HAP RED` reaches P0.08. A correctly-routed command to an absent LED is indistinguishable from a command that does nothing. The path is symmetric and the negative result is exactly what correct routing predicts, so this is a gap in evidence rather than a suspicion — and it closes at stage 4 on the DK, which has four separate LEDs.

**Still unobserved:** `STATE` taking a steady base level, `CFG` halving the scale, and the acknowledgement tap that the +10 ms row above says fired. These were accepted on the wire and are not confirmed to render.

Likewise V3 is green only on its firmware half. That the dongle emits thirty-two well-formed events says nothing about whether the scoreboard scores them correctly — that is the other half of the rung and it needs the application.

---

## 3. Planned work

M1 was done before M2 deliberately: the application is the node that holds every requirement the specification added, and it could be built and fully tested against `FakeDongleTransport` with no hardware at all. Bringing the firmware up first would have meant guessing at the shape of the traffic the application actually produces. That guessing is now over — M1 fixed the exact traffic, and M2 is concrete.

### 3.1 M2 — the firmware programme — ▶ next

**One firmware, six stages, ending in an end-to-end demonstration:** a physical button press on an nRF52840 DK, scored on the scoreboard, acknowledged back as a rendered haptic on that same DK.

The implementable contracts are [`dongle/BUILD_SPEC.md`](dongle/BUILD_SPEC.md) and [`remote/BUILD_SPEC.md`](remote/BUILD_SPEC.md), written 2026-08-10. This section carries the shape and the reasoning; the specs carry the detail.

| Stage | Delivers | Exit |
|---|---|---|
| **0** | Host toolchain; both host suites rewritten to v3.0 **before** the parser is touched | ✅ **Closed 2026-08-12** — toolchain done, protocol suite green (§2.6, 131 checks). **The `rframe` suite, written at stage 3** where the codec it tests is designed, is now green too (67 checks) — §9.2 |
| **1** | Wire v3.0, the `radio.h` seam, `radio_null` | ✅ **Closed 2026-08-11.** V0–V6 all run; emulator wire-log diff clean (§2.7). V4 row 7, V5.1's remote-render half, V6.4, V6.5 carried forward as deferred, not blocking (§5, §9.2) |
| **2** | Provisioning record, reader, refusal path, bench tool | ✅ **Closed 2026-08-12.** `common/provisioning.c` (54 host checks), `dongle/src/provisioning_flash.c` wired into `engine_start()`. **A19 confirmed on hardware**: unprovisioned, `INFO` reports the `RR-0000` fallback; with a `provision.py`-generated record written to `storage_partition`, `INFO` reports the provisioned serial exactly. Writing the record needed `nrfutil nrf5sdk-tools` (CLI, not the Programmer GUI) — `dongle/BUILD_SPEC.md` §9 records the procedure and the two dead ends before it. The remote half of "on both boards" needs stage 4 |
| **3** | `rframe` codec, then BLE at 7.5 ms | ◐ **Code-complete, W0 closed, 2026-08-12.** `common/rframe.c/.h` (both directions, CTR arithmetic) and `dongle/tests/rframe/` (20 checks, 67 assertions, covering A1–A7, A11) — `make check` now runs clean, 0 failures, after the host `as.exe` block (a transient Defender cloud-reputation hold, not a code defect) cleared. `dongle/src/radio_ble.c` implements the full connection lifecycle (`dongle/BUILD_SPEC.md` §7.1) and builds clean under `-DCONFIG_DONGLE_RADIO=y`, no-radio baseline unaffected. **W1 unconfirmed** — needs two provisioned units and a radio scan/join, i.e. hardware. §9.2 |
| **4** | DK remote firmware; the demonstration | ◐ **Code-complete 2026-08-12.** `remote/src/{link,buttons,haptic,indicators,prov_flash,main}.c` — GATT server, gesture classifier, PWM haptic proxy with the BEAT:TAP ratio preserved through `DN_CONFIG`, `LED_LINK` as a conjunction, synthetic telemetry per §8.3. Builds clean against `nrf52840dk/nrf52840`. **W2–W5 unconfirmed** — needs a flashed, provisioned DK talking to a flashed, provisioned dongle; nothing here has touched hardware yet. §9.2 |
| **5** | Measurement, second peripheral, soak | **W6–W8**, **V8**, **R2** |

#### Why this is one milestone and not two

The previous revision split this into M2 (dongle USB only) and M3 (radio), and gated M3 behind a fully-green V1–V8. The isolation that gate bought is real — a no-radio USB baseline is what makes a later radio regression attributable rather than a suspicion. **It does not need a milestone boundary to buy it.**

The `TEST` modes generate `EVT` traffic with no radio involved, so "no radio" is a **build configuration**, `CONFIG_DONGLE_RADIO=n`, that is kept permanently and can be re-run in the time it takes to flash. Every item in §3.10's regression list is diagnosed by asking *does this still happen with the radio compiled out?* — and a baseline you can re-run answers that better than a baseline that was green six weeks ago.

So the attribution survives, and what goes is the sequencing cost: a firmware release that ships once, a ladder run to completion against a message set that is about to be extended, and a second bring-up of everything the first one already proved.

#### The wire changes, unchanged in substance from the previous revision

| # | Change | Note |
|---|---|---|
| 1 | `HELLO 3.0 <fw> <set> <caps>` | Until this lands the app's major-version guard refuses the link — correct, and it makes the wire layer all-or-nothing rather than incremental |
| 2 | `EVT <button> <gesture> <src> <seq>` | Four fields. Delete `TIME_UP`/`PERIOD_UP`/`CLOCK`/`EXPIRE`; add the three gestures. **The three-field form must fail closed** |
| 3 | **Delete the clock and the heartbeat timer** | A deletion, not a port. The dongle holds no match state at v3.0 — the beat arrives as `HAP <target> BEAT` from the app |
| 4 | `ACK <seq> [SILENT]` replaces `CONFIRM` | Routed to the originating remote by the `src` recorded against that `seq`, never broadcast |
| 5 | `STATE` and `CFG` accepted and relayed | Idempotent full assertion, **relayed with no dongle-side cache** |
| 6 | `JOIN <remote>` emitted from the radio seam | The app answers it with a forced `STATE` |
| 7 | `seq` widened to 0–65535 | 16-bit wrap. Hold-repeat at 150 ms wraps a 1000-entry space in 2.5 minutes |
| 8 | `PING` 1 s / supervision 2.5 s | Tightened from 2 s / 5 s |
| 9 | `TEST 4` added | **16 events per remote, 32 total** — see §3.12 |
| 10 | Transmit drop counter | Two silent drop paths exist today and `usb_link_send()` returns `void` |

The pending table shrinks in lifetime and grows in importance: at 120 ms, v2.0's lazy expiry no longer holds, because an entry that only expires when a later message touches the table can still match an `ACK` long past its deadline — and firing a tap the referee cannot account for is the worst outcome `PROTOCOL.md` §11 identifies.

#### Stage 0 is not optional, and the reason changed

**The host suites come first, and they need an install.** The parser tests were always "the highest-value outstanding item, needing nothing but a compiler". Verification on 2026-08-10 found that this machine has **no host C compiler at all** — no WSL, no clang, and none inside the NCS toolchain bundle, whose `mingw64/bin` contains 50 executables and not one of them a compiler. That is why V0 has never run, and it is a one-time MinGW-w64 install rather than a standing impossibility.

Writing both suites *before* the code they test is the point of the stage. The parser is being rewritten from scratch and the frame codec written from nothing, and the two cases that matter most both fail closed — T7, the v2.0-shaped gestureless `EVT`, and A4, the duplicate `CTR`. Those are exactly the cases that pass on inspection.

### 3.2 Validating the app ↔ dongle interface without remotes — ▶ alongside M2

The remotes do not exist and will not for some time, so the question of how far the USB interface can be validated without them had to be answered deliberately rather than by default. **The answer is the dongle emulator, built 2026-08-09** — the application is validated against an executable copy of the protocol before firmware exists, so a failure after M2 localises to the firmware rather than being ambiguous across the whole pipeline.

An emulator on the *radio* side was considered and rejected. The original reason was that a laptop's Bluetooth stack cannot hold the peripheral role at the connection parameters this design needed — and **that reason is weaker now that the baseline is 7.5 ms rather than 2.5 ms** (§4.8), so it is worth restating the argument that survives: a host Bluetooth stack gives no control over scheduling, no visibility into retransmission, and no way to render a deadline, so its timing would describe the laptop rather than the product. Radio validation needs Nordic silicon at both ends and belongs to M2 stages 3–4, where the DK is already the remote-prototyping platform and most of that firmware *is* the remote firmware.

The pieces:

**What already exists to build on:**

- **V0**, the host parser tests — no hardware at all, and the highest-value single item (§5, rung V0).
- **The dongle `TEST` modes** (`PROTOCOL.md` §10.2) — deterministic `EVT` stimulus standing in for remote presses: `TEST 1` for one press per button, `TEST 4` for every gesture on every button, `TEST 2` for randomised soak load. These are what let V3 and V8 run with no remotes.
- ~~**`CONFIG_DONGLE_FAKE_LINK`**~~ — synthetic `LINK`/RSSI/battery so the app's indicators could be exercised. **Deleted at M2** (§4.11): it fabricated precisely the values a link test measures, and plausibly. Its replacement, `radio_null`, reports `DISCONNECTED`, which is true.
- **The LED stand-in** (`indicator.c`) — **one blue lamp** representing two remotes' worth of haptics and indicators ([`dongle/BOARD.md`](dongle/BOARD.md)), which is enough to see *that* a command arrived and nothing about *where* it was routed or *how it feels*.
- **The app's running counters and diagnostics export** — ack latency p99/max is measured at the app end, so R2 can be bounded (minus radio hops) as soon as M2 lands, with no extra tooling.
- **The dongle emulator — ✅ built, and the instrument that closes this question.** See below.

#### The dongle emulator

`wrsl-app/src/emulator/` implements the dongle half of `PROTOCOL.md` v3.0 in JavaScript and drives it over a real serial link, with an interactive mockup of **both remotes**: seven pressable buttons per remote carrying the firmware's gesture timing, four indicators rendering `STATE`, and a haptic motor showing waveform and amplitude. The scoreboard connects through its ordinary Web Serial path and cannot distinguish it from firmware.

**Why this and not a dongle-side echo.** The alternative considered was having the firmware report what it *would have* radioed, as `LOG` lines read in the app's debug panel. The emulator is strictly better: it needs no firmware at all, so it works now rather than after M2; it observes each direction at its rich end instead of narrating the poor one; and it makes the two things a single dongle LED can never show directly visible — **which** wrist an acknowledgement landed on, and **how strong** it was against the others.

**What it validates:** the whole application against an executable copy of the protocol — handshake, supervision and recovery, `JOIN` → forced `STATE`, indicator assertion per remote, acknowledgement routing by `src`, the inert/no-op distinction, the beat-on-owner-only rule and burst suppression, and the real Web Serial path including reconnect. In effect the V2–V7 behavioural surface, before any firmware exists.

**What it does not validate, and must not be read as validating:** anything about firmware. Its timing is desktop-grade, not firmware-grade — an ack measured here does not bound R2. It says nothing about how an ERM feels on a wrist (R3, R4), the radio (B4–B8), or real link state.

**Its role at M2 is as the reference trace.** Run a scenario against the emulator, run the same scenario against the firmware, diff the wire logs. A difference is a firmware defect, localised to one segment — which is the failure-isolation the pre-firmware validation exists to buy.

**Running it.** `npm run dev` in `wrsl-app`, then the emulator at `/emulator.html` and the scoreboard at `/?anyport`, each holding one end of a virtual serial pair. Use **Free Virtual Serial Ports**, not com0com — com0com's driver has been unmaintained since 2017 and its signature is no longer trusted, so on current Windows it installs but fails with Code 52 and produces no ports at all. Because the two halves talk over the serial pair rather than over HTTP they need not share an origin, so a **deployed** scoreboard can be pointed at a local emulator, which is the honest way to run rungs D1 and D7 against the artefact that actually shipped.

**Standing obligation:** the model is now a third place the wire contract lives, alongside `PROTOCOL.md` and `DongleService`. `dongleModel.test.js` asserts the §14 cases from the dongle's side, so the two ends are checked against one specification — but a protocol change that skips the model would validate the application against a contract the firmware will not honour.

**What no software instrument covers:** acknowledgement routing to a physical *wrist* (B6), haptic amplitude and perceptibility (R3, R4), radio latency and the full 120 ms budget (B7, R2's radio share), and link behaviour at range (B4, B5). These wait for hardware at M2 stage 4 and beyond, and any bench result that appears to speak to them is validating a stand-in.

**Both open questions from the previous revision are now settled:**

1. **Where V0 runs.** A local MinGW-w64 install, recorded as `dongle/tools/hostenv.sh` beside `ncsenv.sh` so the route is written down rather than remembered — M2 stage 0. CI was the alternative and remains the better long-term answer, but it is not reachable today and the tests need to run before the parser is rewritten, not after a runner exists. The obligation this leaves is that `make check` is part of the stage-1 exit criteria rather than a thing somebody remembers to do.
2. **What the pass bar is.** Per-stage exit criteria, §5. Stage 1 closes every rung that does not require a radio; §3.10 stays as the re-entry checklist for when one exists.

**One known-benign difference in the diff, recorded before it is seen.** The emulator reconstructs `ECHO` text with `args.join(' ')`, collapsing runs of spaces, which contradicts `PROTOCOL.md` §3's "identical text". Firmware has the raw line and should return it verbatim, so the two will differ on that one case and the firmware is the correct one. Written down because this diff is the primary stage-1 instrument and its value rests entirely on differences being trustworthy — one unexplained benign difference is how a diff stops being read.

### 3.3 The embedded roadmap in one view

M2 through M8 are one programme with one shape: **each step adds exactly one new thing that can be wrong.** That is the whole reason for the ordering, and it is worth stating before the detail, because the tempting shortcuts all consist of adding two.

| Phase | Step | What is new, and therefore what a failure means | Hardware |
|---|---|---|---|
| 1a | **M2** stages 0–1 | The v3.0 message set. No radio anywhere in the system | Product dongle + host |
| 1b | **M2** stages 2–3 | Association, security and the radio, one connection. A failure is radio or provisioning — never the message set, which stage 1 fixed | + nRF52840 DK |
| 2 | **M2** stage 4 | The remote *end*: real presses, real indicator rendering, a real end-to-end loop | (same) |
| 3 | **M4** | The **second** connection. A failure is central scheduling, routing or skew — nothing else changed | + nRF52840 dongle as remote #2 (§4.9) |
| 4 | **M5** | The remote's real peripherals — seven buttons, RGB, an ERM, a PMIC. A failure is hardware or drivers, not protocol | + GPIO harness, ERM, nPM1300-EK |
| 5 | **M6** | Nothing runs. This is schematic, layout, BOM and enclosure, and its **inputs are M4 and M5 measurements** | — |
| 6 | **M7** | The custom board. A failure is the port or the board, because the firmware above it is the firmware M5 validated | Custom remote PCBs |
| 7 | **M8** | The custom dongle. Optional, cost-driven, and deliberately last | Custom dongle |

**Phase 1 still spans two things that must not be brought up together, and the separation is unchanged — only its mechanism is.** A message-set rewrite and a radio are two milestones' worth of risk in one sentence, and bringing them up simultaneously forfeits the emulator reference trace of §3.2, which is what localises a wire defect to the firmware. It forfeits it exactly when §3.10's list of ways a radio silently degrades a USB link becomes live.

What changed is that the separation is now enforced by a **build configuration retained forever** (`CONFIG_DONGLE_RADIO=n`) rather than by a milestone boundary crossed once. That is strictly stronger: the old arrangement gave a no-radio baseline that was true on the day it was measured, and the new one gives a no-radio baseline that can be re-measured at any point in the project's life, against the firmware actually in hand.

**Two ordering constraints run backwards through this table**, and both are easy to miss because they look like late-phase concerns:

- **M6 cannot start before R6 has a number.** Battery capacity is an enclosure and PCB decision, and the connection-interval rung chosen at M4 (`RADIO_PROTOCOL.md` §12.2) is the dominant input to radio current. Designing the board against a predicted rung means respinning it when the measurement disagrees. §3.6 makes the measurement an M5 exit criterion for this reason.
- **M6 cannot start before R3 and R4 have an answer.** `SCOPE.md` §9.2 lists a dual-haptic hardware revision as contingent on the single-ERM assumption failing in prototyping — that contingency has to resolve while it is still a firmware-and-breadboard question, because after M6 it is a respin.

### 3.4 M3 — absorbed into M2

**M3 no longer exists as a separate milestone.** Its content — the radio brought up 1:1, dongle central with one DK as a remote — is M2 stages 2 to 4, for the reason in §3.1. The number is retired rather than reused, and M4 onward keep theirs.

**The implementation detail has moved to the build specs**, which is where it belongs: [`dongle/BUILD_SPEC.md`](dongle/BUILD_SPEC.md) carries the radio seam, the connection lifecycle, `CTR` accounting, the deadline mechanisms, the counters and the provisioning reader; [`remote/BUILD_SPEC.md`](remote/BUILD_SPEC.md) carries the GATT service, the gesture classifier, the waveform table and the DK hardware map.

What stays here is what a status document should carry: the requirement the radio answers to, the decisions that bind, the things the DK cannot reach, and the questions still open.

#### The deliverables, as a checklist

| # | Deliverable | Stage |
|---|---|---|
| 1 | Provisioning record, CRC check, and the refuse-to-operate-unprovisioned path (A19) | 2 |
| 2 | Bench provisioning tool — a script generating partition hex and a manifest | 2 |
| 3 | Dongle BLE central: fixed-address initiator, LTK from the set key, no pairing | 3 |
| 4 | RefRemote Link Service on the remote | 3 |
| 5 | Frame codec, shared, **written without Zephyr dependencies** so rung W0 can run on a host | 3 |
| 6 | `CTR` accounting, gap and duplicate detection, `ctr_base` re-baselining | 3 |
| 7 | Radio seams filled behind `radio.h` | 3 |
| 8 | Deadline enforcement mechanisms 1 and 2 | 4 |
| 9 | `DN_HOST`, and `LED_LINK` as the conjunction (A15) | 4 |
| 10 | Real `LINK` state, debounced, averaged RSSI — and `CONFIG_DONGLE_FAKE_LINK` **deleted** | 4 |
| 11 | DK remote firmware, reduced surface | 4 |

**The provisioning tool is real work that no document had claimed.** `RADIO_PROTOCOL.md` §10.1 says the record is "written once at manufacture", which is true of the product and unhelpful now, when three units need records today and will need them again after every erase. Build the **record format, the CRC check, and the refusal path for real** — that is a boot path, and boot paths added late are boot paths nothing ever exercised — but generate the records with a script rather than a process. A `#define`-ed key is the tempting shortcut and a bad one: it makes A12, A13 and A19 untestable, and those are three of the four cases standing between this product and a cross-associated match.

#### The DK as a remote: what four buttons and four LEDs can and cannot reach

The nRF52840 DK offers **four buttons and four single-colour LEDs**, one of which is on a PWM channel. The remote specifies **seven buttons and four RGB indicators** (FS §3.1, §3.2) plus an ERM. The gap is not a detail to be worked around silently; it determines what stage 4 can claim.

The full mapping is in [`remote/BUILD_SPEC.md`](remote/BUILD_SPEC.md) §2. In summary: buttons map for **gesture and semantic coverage, not button coverage** — `ADD_POINT` for `PRESS`, `TOGGLE_CLOCK` for `HOLD`, `FORWARD` for `HOLD_REP`, and `F1` for the inert/no-op distinction. That reaches all three gestures, the `HOLD_REP` restriction, and the distinction most likely to be flattened, because each of the seven buttons is otherwise the same code path. `REMOVE_POINT`, `BACKWARD` and `F2` wait for M5 as real GPIO.

**A shift or bank modifier to reach all seven from four buttons is rejected.** It is the obvious solution and it is a second input path — the same objection §2.3 makes to an operator shortcut in the app, one layer down. It would be firmware the product does not have, exercising timing the product does not have, and it would be the only thing under test that ships nowhere.

**One correction against the previous revision of this table.** It put the haptic proxy on LED 4. The stock DK devicetree PWMs only `led0` (P0.13) — `pwm0_default` assigns `PWM_OUT0` there and nowhere else — so any other LED needs a pinctrl overlay. Physical LED position means nothing on a development kit, so the haptic proxy goes on **LED 1** and no overlay is needed. One fewer file diverging from upstream, one fewer thing that can be wrong.

**Three things stage 4 therefore cannot claim, and must not be read as claiming:**

1. **Indicator colour.** `DN_INDICATOR` carries RGB per indicator; single-colour LEDs show mode only. Verify the colour fields by reading them out over RTT, and treat the rendering itself as unvalidated until M5.
2. **`LED_PWR`.** The DK is bus-powered and has no battery, so there is nothing true for it to show. `UP_TELEMETRY.battery_pct` is synthetic until M5, which means the app's battery indicator is being fed a constant — the same trap `CONFIG_DONGLE_FAKE_LINK` set, wearing different clothes and with none of the visibility, because there is no Kconfig symbol whose name gives it away.
3. **Anything haptic.** LED brightness is not amplitude. R3 and R4 — whether one ERM covers the whole range, and whether `BEAT` is reliably distinguishable from `TAP` on a wrist — are **untouched by M2 and M4** and are the substance of M5.

#### Decide before starting, not during: how diagnostics get out

`PLAN.md` has carried this as an open question; at stage 3 it becomes blocking, and the answer differs by board.

The console is disabled and a second CDC-ACM instance is forbidden (§4.4), so the dongle has no diagnostic channel but protocol `LOG` and `ERR` lines. **On the DK this is a non-issue** — it has an onboard debugger, so RTT is free and costs no bootloader. **On the product dongle it is a real cost**: RTT needs the debugger partition table (`fstab-debugger.dtsi`), which means giving up the stock nRF5 bootloader on that unit, and with it the hold-button-while-plugging-in flash path that every procedure in this document assumes.

The recommendation is to **keep the dongle on `LOG` lines and put RTT only on the DK**, since the dongle's traffic is already fully observable at the app end and the remote's is not observable at all. If a dongle-side RTT unit becomes necessary, dedicate *one* dongle to it permanently and label it, rather than converting and reverting the one under test — a unit whose bootloader state is uncertain is a unit whose flash procedure is uncertain. Note also that the DK can act as an SWD programmer and RTT host for an external nRF52840 through its debug-out header, **if** the target board exposes SWDIO/SWDCLK; check the MDBT50Q-CX-40 pinout before planning around it.

#### What the radio has to deliver

From `PROTOCOL.md` §12 and the specification:

| Requirement | Source |
|---|---|
| ~25 ms one-way, inclusive of retransmission, at 12 m with body shadowing | Half the acknowledgement budget |
| Exactly-once and ordered per remote, or visible loss | FS §7.3 |
| Downlink at 1 Hz per remote sustained, plus asynchronous acknowledgements and notifications | FS §6.2, §11 |
| 30 systems / 90 devices in one venue, no degradation attributable to neighbours | `SCOPE.md` §4 |
| **No cross-set association under any circumstance**, cryptographically enforced | FS §2.3 |
| Ten-hour day on the remotes | FS §11.2 |
| RSSI per remote, debounced link state | `PROTOCOL.md` §7 |

#### The four decisions the protocol commits to

Taken deliberately, with the alternatives written up in `RADIO_PROTOCOL.md` §15 rather than discarded, and recorded as binding in §4.8 below.

| Decision | Resolution | Rationale |
|---|---|---|
| **Bearer** | Bluetooth LE. Dongle central holding two peripheral connections; ESB/Gazell retained as the documented fallback if measured latency at density fails | ESB gets the topology and hardware exactly-once right, and fails on channel hopping, on having no security at all, and — decisively — on a downlink that rides only as a preloaded acknowledgement payload, which turns our asynchronous downlink into a polled one. `RADIO_PROTOCOL.md` §15.1 |
| **Timing** | **Baseline BLE at 7.5 ms — rung 3. No SCI, no subrating, no LLPM.** LE 2M PHY, 27-byte payloads, peripheral latency pinned off. Rungs 1 and 2 remain specified as a deferred contingency | Revised 2026-08-10, superseding "SCI, target 2.5 ms". The interval buys **retransmission headroom**, not latency — at 7.5 ms the no-retry one-way is ~9.2 ms with room for about two retries inside the 25 ms allocation. Whether that is enough depends on the retransmission rate at 12 m through a torso, which is unmeasured, so the baseline is the rung needing no special controller feature. §4.8 |
| **Exactly-once** | Device-local 8-bit counter per remote for gap visibility and replay rejection. **No application-level retry**, in either direction | The Link Layer already delivers exactly-once on an intact connection; the counter's real work is making a loss it could not prevent into a number somebody can read. A retry above the link layer fires only when the press is already worthless, and produces the late tap `PROTOCOL.md` §11 forbids. §6 |
| **Set binding** | Provisioned set key installed as the LTK via `bt_nrf_conn_set_ltk()`; fixed static-random identity addresses; **no pairing procedure is ever performed**, at manufacture or in the field | Manufacture-time bonding stores the binding where a DFU or settings migration can silently clear it, and a set that has forgotten its binding presents at an event as two remotes that will not connect. First-boot auto-bonding opens the pairing window FS §2.3 exists to close. §10 |

**The decisions are settled; the numbers are not.** Every latency figure in `RADIO_PROTOCOL.md` §12 is arithmetic from documentation, and the ones that matter — latency at 12 m through a torso, with 29 other systems in the hall — cannot be obtained from documentation. They are stated so the measurement has a prediction to falsify.

**One specification nuance was resolved in the writing and is worth flagging.** FS §2.1 lists link status among the things a remote detects locally; FS §10.2 defines the indication as *connected end to end*. Those reconcile only if the remote is told about the half of the path it cannot see — the USB cable, the browser tab, the laptop's sleep state, the application watchdog of FS §8.3, every one of which leaves the radio connection perfectly healthy while the scoreboard is gone. `PROTOCOL.md` §8 already requires the dongle to *"instruct both remotes to render link-lost"* in prose; `RADIO_PROTOCOL.md` §9.2 gives that requirement a frame (`DN_HOST`) and states the rule: the remote renders `LED_LINK` from the **conjunction** of radio-up and host-up. It still detects its own link state; the state simply has two inputs. This is a refinement of FS §2.1, not a contradiction of it, but it is the kind of thing that should be noticed rather than absorbed.

#### Where the dongle sits is part of the link budget

The Raytac MDBT50Q-CX-40 carries an MDBT50Q-P1M module with a PCB trace antenna, and the nRF52840 will do up to +8 dBm. The dongle then sits in a USB port on a laptop at the scoreboard table: close to the host's own 2.4 GHz radios, often below table height, frequently with bodies between it and the mat.

The link budget must be taken **at the dongle as deployed**, not on a bench with clear line of sight. If it does not close, the available remedies are a USB extension cable to raise and separate the dongle, higher transmit power, or a dongle placement constraint in the deployment documentation — in that order of preference.

#### Seams in the code today

Each is currently satisfied by a stand-in. `dongle/BUILD_SPEC.md` §3 defines the interface they collapse into — `radio.h`, with `radio_null` and `radio_ble` as its two implementations — so the seams stop being scattered `#ifdef`s and become one boundary with two sides.

| Seam | Location | Currently |
|---|---|---|
| Link state source | `engine.c` — `links[]`, under `#ifdef CONFIG_DONGLE_FAKE_LINK` | Synthetic `CONNECTED` with fixed RSSI and battery. **The symbol is deleted, not defaulted off** — §4.11 |
| Event origination | `engine.c` — `send_evt()` | Called only from `test_handler()` |
| Acknowledgement delivery | `engine.c` — pending table | Pulses an LED, not routed by `src` |
| Indicator assertion | *does not exist* | New at stage 1 |
| Haptic delivery | `engine.c`, `indicator.c` | LED pulses |
| Remote join detection | *does not exist* | New at stage 3; drives `JOIN` |

`send_evt()` already allocates and wraps the sequence number and registers actions in the pending table, so a press arriving from a remote must reach *that function* rather than reimplement around it. Note the one trap that creates: it must refuse to allocate a `seq` for a remote that is not connected — otherwise a `seq` gap could mean radio loss as well as USB loss — but that check would silently disable every `TEST` mode under `radio_null`, where nothing is ever connected. `dongle/BUILD_SPEC.md` §3.2 splits it by origin.

#### Questions the protocol answers

Listed because they were open in the previous revision of this section and are not any more. Each is a design commitment now, and changing one is a change to `RADIO_PROTOCOL.md`, not a tuning decision.

| Question | Answer | Where |
|---|---|---|
| How is a physical remote bound to the `RED` / `GREEN` identity, and how is the set serial provisioned so `HELLO` can report it? | A CRC-checked provisioning record in a dedicated flash partition, written once at manufacture: serial, role, own and peer identity addresses, set key. An unprovisioned unit does not advertise and does not initiate | §10.1 |
| How is `LINK` state derived and debounced so a remote at the edge of range does not flood the USB link? | `CONNECTED` reported only after encryption, identity validation and subscription; `DISCONNECTED` after 2 s; `CONNECTING` emitted immediately so the app has something true to show meanwhile. The remote's *own* indication is not debounced | §9.4 |
| Where do presses go that arrive while the app is disconnected? | Dropped, counted, and reported in the first `LOG` line after the app returns. Never queued — a queued press applied minutes later is a wrong score with no visible cause | §6.5 |
| How does the remote know the difference between "radio up" and "scoreboard reachable"? | It is told, by `DN_HOST`, and renders the conjunction | §9.2 |

#### Questions M2 answers

- What is the one-way latency on **one** connection, on a bench, at 7.5 ms — the floor everything later is measured against?
- Does the app tolerate a real `LINK … CONNECTING` state, emitted for the first time by real hardware rather than by the emulator (`RADIO_PROTOCOL.md` §13.1)?
- Is LE Flushable ACL Data usable in v3.4.0, where it is marked experimental? The deadline guarantee does not rest on it — mechanisms 1 and 2 must hold without it — but it is the natural mechanism if it works.

**One question the previous revision asked has been answered by decision rather than by measurement.** *Does radio work share the system workqueue, or does the engine need its own?* The answer is a dedicated cooperative workqueue, specified from the start in `dongle/BUILD_SPEC.md` §4. Measuring first was the honest position when the alternative was rework; but the queue costs a stack definition, and taking it up front removes B1 — workqueue contention showing up as acknowledgement latency rather than as an error — from the list of things any later latency figure might mean. §4.6 is amended accordingly.

#### Questions M2 deliberately leaves to M4

Each needs the second connection to mean anything, and answering them on one connection produces a number that looks like an answer and is not:

- What is the measured one-way latency at 12 m through body shadowing, at density — p99, not median? **This is now the measurement that would reopen SCI** (§4.8), rather than a rung-selection exercise.
- Is two-connection scheduling at 7.5 ms clean — no dropped connection events, no event-length overruns? The arithmetic says trivially yes, which is a much weaker claim than the one rung 1 needed, and it is still unrun.
- What is the power cost of the connection cadence against the ten-hour target — given that peripheral latency and subrating are both ruled out (§9.1) and heartbeat density is not available as a lever (FS §11.2)?

### 3.5 M4 — the 2:1 link

One central, two peripherals, two connections. Everything else is unchanged from M2, which is the point: a failure here is scheduling, routing or skew.

#### What M4 has to establish

| # | Question | Why it needs two connections |
|---|---|---|
| 1 | That 7.5 ms is schedulable on the **pair**, and the p99 latency it delivers at range | Central scheduling requires every link's timing-event to fit inside the common interval, so the interval is a property of the *pair*. At rung 3 the arithmetic says trivially yes — a far weaker claim than rung 1 needed, and still unrun. **The latency figure is the one that would reopen SCI** (§4.8) |
| 2 | Acknowledgement routing to the correct remote (B6) | With one remote, a broadcast tap and a correctly routed tap are indistinguishable — §2.5 item 4. This is the defect the whole `src`/pending-table mechanism exists to prevent, and one connection cannot see it |
| 3 | Cross-connection arrival skew (R5) | Ordering is guaranteed per connection and not between them (`RADIO_PROTOCOL.md` §6.4). The skew is the measurement; there is no skew with one link |
| 4 | Downlink fan-out under load | 1 Hz beat to the owner only, plus asynchronous taps to either, plus `LINK` re-emission — B2's transmit-ring pressure is a two-remote condition |
| 5 | Beat-on-owner-only, on real hardware | The emulator validated the app's half (§3.2). The radio half is new |
| 6 | Density behaviour, first look | Two connections from one central is the smallest system that has an aggregate to degrade |

#### The hardware decision: what plays the second remote

Available: 2 × MDBT50Q-CX-40 dongles, 2 × nRF52840 dongles (PCA10059), 1 × nRF52840 DK.

**Decision: the DK is remote #1; a spare nRF52840 dongle is remote #2. Do not buy a second DK, and do not have one DK emulate both remotes.** Recorded as binding in §4.9.

**Why not one DK emulating two remotes.** It is achievable — Zephyr supports multiple local identities and multiple advertising sets, so one nRF52840 can hold two peripheral connections to one central. It should still be rejected, and not on grounds of effort:

- **It tests the wrong scheduler.** M4 exists to find out whether *central* scheduling of two links at 2.5 ms works. Two connections terminating on one peripheral radio adds a *peripheral*-side scheduling problem that does not exist in the product, on the node whose budget the product never constrains. A rung-1 failure would be uninterpretable — peripheral contention and central contention produce the same symptom, and the product only has one of them.
- **There is one antenna, in one place.** Cross-connection arrival skew (R5), body shadowing, spatial diversity and the 12 m link budget all require two devices in two positions. A single device cannot be shadowed from the dongle by one torso and not the other, which is the actual field condition.
- **The firmware is thrown away and diverges.** Dual-identity peripheral firmware is not remote firmware. The code M4 validated would not be the code M5 extends, which forfeits the reason for prototyping on real silicon at all.

**Why a spare dongle is sufficient, and why it does not need to be a full remote.** Remote #2's job in M4 is to hold a second connection, consume the downlink, generate uplink traffic at realistic rates, and report telemetry. It does not need seven buttons or a wrist. One button covers all three gestures, and a self-stimulus timer covers sustained load — the same trick `TEST 2` already plays on the dongle. Every question in the table above is answerable with an asymmetric pair.

**Use the PCA10059 rather than the spare MDBT50Q-CX-40 for bring-up**, on a concrete difference confirmed in the board devicetree: the PCA10059 carries a green LED *and an RGB LED with all three channels on PWM*, where the MDBT50Q-CX-40 has two mono LEDs and one button (§4.2). That RGB is worth having — it is the only surface in the system before M5 that can render `DN_INDICATOR` colour at all, which is one of the three things §3.4 lists M2 stage 4 as unable to claim. **Then swap in the spare MDBT50Q-CX-40 for the range and density measurements**, because those are antenna-and-module measurements and the MDBT50Q is the module the product is likely to carry. Two boards, two purposes, and the swap costs a flash.

**Would a second DK be justified?** Not for M4 — nothing in the table above needs one, and the asymmetric pair answers all six. The honest case for a second DK is narrower and later: **two *haptic-capable* remotes**, for B6 as a felt experience rather than a routing assertion, B8's beat-and-tap collision, and R4's amplitude separation. Three points against buying one for that:

- Those three are wrist-and-strap questions. The variables that decide them are motor mass, mounting compliance and enclosure coupling — none of which a bare DK on a bench has, and all of which the M6 board and enclosure do. A second DK would not settle them; it would produce a number that gets re-measured at M7 anyway.
- You have one nPM1300-EK. A second haptic-capable remote means a second PMIC evaluation board and a second ERM before it means a second DK, so the DK is not even the binding purchase.
- R4 is a *perceptual* judgement, and the single most useful instance of it is one referee wearing one well-made remote. That is an M5 experiment with one DK.

So: **no second DK for M4, and probably not for M5 either.** Revisit only if the M6 spin slips far enough that two-wrist evaluation on breadboards becomes the critical path — and if that happens, price a second nPM1300-EK and ERM in the same breath, because a DK alone would not unblock it.

### 3.6 M5 — the full-feature remote, on the DK

The DK's GPIO carries what the DK's onboard peripherals could not: seven buttons in the FS §3.1 layout, four RGB indicators, an ERM with its driver IC, and the nPM1300-EK for charging, fuel gauging and regulation.

| # | Deliverable | Answers |
|---|---|---|
| 1 | Seven GPIO buttons, FS §3.1 positions, debounce 15 ms | The three buttons the DK's four could not reach; tactile discrimination is an M6 concern |
| 2 | Four RGB indicators, `DN_INDICATOR` rendered in **colour** | The fidelity gap §3.4 opened |
| 3 | `LED_PWR` from a real state of charge, four-band (FS §10.1) | Retires the synthetic `battery_pct`, which is the last fake value in the system |
| 4 | ERM + driver IC, full waveform table | `TAP`, `BEAT`, `WARN`, `BUZZ`, `LONG`, `DOUBLE`, `TRIPLE` |
| 5 | `DN_CONFIG` scaling that **preserves the `BEAT`:`TAP` ratio** | `RADIO_PROTOCOL.md` §7.3 — an implementation scaling one shared amplitude parameter satisfies the frame and violates the requirement |
| 6 | nPM1300 integration: charge, fuel gauge, regulator, USB-C | FS §3.4 |
| 7 | Link-lost repeating double buzz, low-battery haptic | FS §10.2 — remote-local behaviours with no wire representation |

**M5 is where R3, R4 and R6 are settled, and settling them is an exit criterion, not a nice-to-have.** §3.3 gives the reason: all three are inputs to M6, and each has a hardware contingency behind it (`SCOPE.md` §9.2 — dual-haptic revision, power-saving mode, swappable battery). A board designed while any of the three is a prediction is a board that may need respinning for a reason that was measurable first.

Measure with the motor on a strap on an actual wrist, wired back to the DK. The board is large and that is fine — the motor's mounting is the variable that matters, and it is the one thing that can be made representative early.

### 3.7 M6 — the custom remote PCB

No firmware runs during M6. Its inputs are the M4 and M5 measurements, and its output is a board.

Nothing about the hardware design is recorded anywhere in this repository yet — this section exists to say so rather than to specify it. The open items, at least: module selection (an MDBT50Q variant keeps the RF characterisation and the board target, and it is what the range measurements of M4 should be taken against); the ERM and driver IC chosen at M5, or the dual-motor contingency if R3 failed; nPM1300 as the production part; battery chemistry and capacity, sized from the R6 measurement and not from the target; the FS §3.1 button shapes and the oversized `TOGGLE_CLOCK` datum, which is a mechanical requirement before it is an electrical one; the four RGB indicators with `LED_F1`/`LED_F2` physically adjacent to their buttons; USB-C charging; the provisioning partition and APPROTECT as manufacturing steps (`RADIO_PROTOCOL.md` §10.4); and a DFU strategy for the remotes, which does not exist in any document and which §10.3 makes consequential — the argument against manufacture-time bonding was precisely that a firmware update can silently clear a settings partition.

### 3.8 M7 — port and validate on custom hardware

The firmware is M5's, with the board layer swapped. That is the claim the ordering buys, and M7's job is to test it rather than assume it: re-run the full radio ladder (§5.3) and the §3.10 regression list on production-shaped hardware, then the parts of §7 that only real remotes can reach — B6 on two wrists, B8's collision, R3 and R4 through the real enclosure and strap, R5 during live matches, and R6 over a full ten-hour day.

This is also the first point at which a complete officiating set exists, so it is where `SCOPE.md` §8.6's field-substitution procedure and V6.5 become testable end to end with real hardware on both sides.

### 3.9 M8 — a custom dongle, optional

Cost-driven, deliberately last, and worth stating plainly: it changes the RF platform underneath a validated system. Everything measured at M4 and M7 about range, link budget and density is a property of the MDBT50Q module and its trace antenna, and a custom dongle re-opens all of it. If BOM cost justifies that, the re-measurement is part of the milestone, not a follow-up.

### 3.10 How the radio can regress the USB link

**Read this before writing radio code, not after.** The USB interface is validated in an environment with no radio. Adding one can degrade it without touching a line of USB code.

| # | Risk | Test |
|---|---|---|
| B1 | **Workqueue contention.** Every engine timer and the RX drain run on the system workqueue (§4.6). Radio work on the same queue delays them. Shows up as acknowledgement latency, not as an error. | Re-run V4 and V5 with the radio active and both remotes connected. Measure the `EVT`→`ACK`→tap path, p99. Consider a dedicated workqueue. |
| B2 | **Transmit ring saturation.** The TX ring is 1024 bytes and drops whole lines when full. Two remotes at 1 Hz heartbeat, plus 10 s `LINK` re-emission, plus event traffic, raises the line rate well above bench conditions. | Run V8 with both remotes connected and pressing. **Instrument the drop path with a counter first** — an uninstrumented drop is a silent loss. |
| B3 | **Fabricated link state.** `CONFIG_DONGLE_FAKE_LINK` reported synthetic `CONNECTED` while real remotes were disconnected. **Deleted at M2** (§4.11), which closes this by construction rather than by discipline. | Confirm `INFO` reports `DISCONNECTED` with no remotes powered — and that it does so because nothing is connected, not because a symbol says so. |
| B4 | **`LINK` state churn.** Real connections flap at the edge of range. Each transition is a line, and the app renders link loss as a primary-tier alarm. | Power-cycle a remote repeatedly at the edge of range. Confirm no flood and no strobing indicator. |
| B5 | **Real RSSI and battery values.** Bench values are constants. Real ones can fall outside the ranges the app accepts, and an out-of-range value is dropped silently. | Verify at various distances and charge levels. |
| B6 | **Acknowledgement routing.** Every acknowledgement currently pulses the same LED. It must reach the **originating remote only**, routed by the `src` recorded against that `seq`. | Press RED and GREEN in quick succession; confirm each tap lands on the correct wrist. |
| B7 | **The 120 ms budget now includes two radio hops.** This is the risk most likely to bite in a real match. | Re-measure after the radio lands. Distribution, not median. |
| B8 | **Heartbeat contention with acknowledgement.** New at v3.0. The beat and the tap share one motor, and a four-press burst takes about a second, so they *will* collide (FS §11.1). | Confirm burst suppression in the app, and confirm amplitude separation on real hardware with a real strap. |

### 3.11 M9 — deployment validation and MVP hardening

Not a phase at the end of the embedded programme. **It runs alongside from M5 onward**, because most of it is scoreboard and product work with no dependency on remote hardware, and two items in it are long-lead:

- **Deployment validation** — D1–D8 (§6). D5 is blocked on a real USB VID/PID, which is a procurement and manufacturing item and should be started early rather than discovered late; D6 requires deciding the production domain before deployment, not after.
- **MVP hardening** — the gaps of §8 that M2–M8 do not close: the real VID/PID and a matching `requestPort()` `filters:` array, connection-error language that distinguishes a policy block from a cancelled picker, the Web Worker heartbeat if R1's test demands it, self-hosted fonts, and **ruleset verification against the published rulebooks for the current cycle** — which needs a rules-literate reviewer rather than an engineer, and is therefore the item most likely to be left until it blocks a real event.

The definition of done is §9.1.

### 3.12 Corrections made to the specifications, 2026-08-10

Recorded here rather than only in the documents themselves, because each was found by *verifying an assertion against the installed tree or the shipped code* rather than by reading, and that is a habit worth making visible.

| # | Document | Was | Is | How it would have failed |
|---|---|---|---|---|
| 1 | `PROTOCOL.md` §10.2 | `TEST 4` sweeps "21 events per remote" | **16 per remote, 32 total** | §5.1 emits `HOLD_REP` only for `FORWARD`/`BACKWARD`, so 7 + 7 + 2 = 16. The two clauses could not both hold. `dongleModel.js` already implements 16, so a firmware written from the document would have disagreed with the emulator and with the app, and the diff would have been read as a firmware defect |
| 2 | `RADIO_PROTOCOL.md` §12.3 | A four-call SCI enable sequence | **Five calls, corrected** | `sdc_support_extended_feature_set()` is deprecated in v3.4.0 in favour of role-specific variants, and `sdc_support_lowest_frame_space()` requires `sdc_support_frame_space_update_*()`, which the sequence omitted entirely. **The failure is a rejected HCI command at runtime, not a build error** |
| 3 | `RADIO_PROTOCOL.md` §12.2 | Rung 1 (2.5 ms, SCI) is the target | **Rung 3 (7.5 ms, baseline BLE) is the baseline** | Not an error — a decision, §4.8. Recorded here because it supersedes a commitment the previous revision called settled |

Two things were **confirmed** rather than corrected, and are worth stating because the design rests on them: `bt_nrf_conn_set_ltk()` exists at `nrf/include/bluetooth/nrf/host_extensions.h:174`, so the set-binding design is buildable; and both boards carry a `storage_partition` — 16 KB at `0xf0000` on the dongle, 32 KB at `0xf8000` on the DK — so the provisioning record has a home reachable by identical code on both, with the difference confined to devicetree.

A third was **discovered**: `PLAN.md` §3.4's DK indicator table put the haptic proxy on LED 4, but the stock DK devicetree PWMs only `led0`. Corrected in §3.4.

---

## 4. Decisions that still bind

Established with evidence. Not to be revisited casually.

### 4.1 NCS v3.4.0, pinned

The LTS release — five years of security and critical fixes — and the **last release supporting the nRF52 Series**, which it declares feature complete. Nordic explicitly recommends it for new nRF52840 designs. Older DevZone advice recommending v3.1.x for this dongle predates that announcement and is superseded. Do not downgrade, and expect no newer SDK to support this chip.

### 4.2 Board target `raytac_mdbt50q_cx_40_dongle/nrf52840`

Present upstream in the installed tree. **Not interchangeable with `nrf52840dongle/nrf52840`** — LED and button mapping differ, and so does bootloader entry (hold the button while plugging in, rather than pressing RESET). `FLASH_LOAD_OFFSET=0x1000` is applied automatically for the nRF5 bootloader; never set it by hand, and do not enable MCUboot or sysbuild without deliberately switching bootloaders.

The board exposes **two LED nodes and one button** — `led0_d1` on P0.06 and `led1_d2` on P0.08, both active-low, both on PWM. **Both parts are blue and only P0.06 is fitted** (measured; Raytac's pin table says the opposite), so the real indicator budget is one lamp and it sits on the `GREEN` channel. The `led0-green`/`led1-red` aliases are inherited from the Nordic dongle and describe hardware this board does not have. That is why `indicator.c` is as coarse as it is, and the full reference is [`dongle/BOARD.md`](dongle/BOARD.md) — written because this fact was derived from the alias names twice and got wrong both times.

### 4.3 The new USB stack (`device_next`)

The board defaults it on. Reports of "old and new stack conflict" come from fighting that default with legacy-stack samples.

### 4.4 The protocol owns the CDC-ACM port exclusively

The board's common devicetree points `zephyr,console`, `zephyr,shell-uart`, `zephyr,uart-mcumgr` and both `bt-*-uart` chosen nodes at a single CDC-ACM instance. Anything left live interleaves bytes into the protocol stream: a log line over 120 bytes trips the receiver's discard-and-resync path, and a write landing mid-line corrupts that line. Both failures are silent and intermittent.

Console, shell and logging are off in `prj.conf`, and a `BUILD_ASSERT` in `usb_link.c` fails the build if a second CDC-ACM instance ever appears. Diagnostics leave as protocol `LOG` / `ERR` lines.

*This is the trap the upstream `cdc_acm` sample falls into on this board, and it cost real time during bring-up.*

### 4.5 Never gate transmission on DTR

The app opens the port via Web Serial and never calls `setSignals()`, so DTR assertion is the browser's default rather than anything the protocol guarantees. Gating on it yields a dongle that enumerates but never answers `INFO`. Boot-time `HELLO` is best-effort; the handshake is driven by the app sending `INFO`.

### 4.6 One queue owns the engine — and at M2 it becomes a dedicated cooperative one

The invariant is **exactly one producer feeding the transmit path**, so no locking is needed between the engine's timers and the RX drain. At v2.0 that queue was the system workqueue.

**At M2 it becomes a dedicated cooperative workqueue**, with BLE host callbacks and the USB RX work both marshalling onto it. The previous revision of this section said to expect that and to *measure before assuming*. That was the right instinct when the alternative was rework, and it is superseded for a simple reason: the queue costs a stack definition, and taking it up front removes B1 — radio work on the shared queue delaying the acknowledgement turnaround — from the list of things any later latency measurement might mean. A measurement is worth making when it changes a decision; here it would only have confirmed one whose cost is a `K_THREAD_STACK_DEFINE`.

Cooperative, not preemptible, and that part is not cosmetic: a preemptible queue can be descheduled between receiving an `ACK` and handing the `TAP` to the controller, and that latency is invisible — it presents as a missing tap under load and as nothing else.

### 4.7 `PROTOCOL.md` is duplicated, not linked

The two copies were previously kept in step by a filesystem hard link. That does not survive an editor writing a new file rather than modifying in place — it silently broke during the v3.0 revision, leaving the two repos on different versions with no indication. Copy explicitly and **verify the hashes match** after any change.

**`RADIO_PROTOCOL.md` is not duplicated.** It lives in this repo only, because the scoreboard never sees the radio and giving it a copy would create a second file to keep in step for no reader's benefit.

### 4.8 The radio is Bluetooth LE at 7.5 ms, and SCI is deferred

Bearer, exactly-once mechanism and set binding are settled in `RADIO_PROTOCOL.md` v1.0. **The timing commitment is revised, 2026-08-10**, and this supersedes the "SCI, target 2.5 ms" of the previous revision.

**The baseline is rung 3 — 7.5 ms, plain Bluetooth LE, no SCI, no subrating, no LLPM.** `RADIO_PROTOCOL.md` §12.2 is amended to match, and §12.3 corrected against the v3.4.0 headers so that a future adoption starts from something true.

The reasoning, because the previous commitment was not wrong so much as prematurely paid for:

- The interval buys **retransmission headroom, not latency.** At every rung the no-retry one-way figure sits comfortably inside the 25 ms allocation. What changes is how many consecutive failed connection events the budget absorbs: about two at 7.5 ms, about eight at 2.5 ms.
- Whether two is enough depends on **the retransmission rate at 12 m through a torso** — the one quantity in the whole analysis that cannot be derived and has never been measured.
- Nothing in `SCOPE.md` or `SYSTEM_FUNC_SPEC.md` specifies an interval. They specify the ~120 ms acknowledgement, which rung 3 meets.
- SCI costs a five-call enable sequence, a subrating prerequisite this design otherwise refuses, and a failure mode that is a rejected HCI command at runtime rather than a build error. That is a real price against an unmeasured benefit.

**SCI is revisited only if body shadowing proves a problem on shipping hardware**, measured at the dongle as deployed, p99 rather than median. Rungs 1 and 2 stay specified so that change has somewhere to land.

**The consequence that runs backwards into hardware, taken knowingly.** §3.3 makes the interval an input to the power budget (R6) and through it to battery sizing and the PCB. A board sized against rung 3 and later moved to rung 1 sees roughly three times the connection events per second, so a battery sized exactly to rung 3 would need a respin. Size with headroom, and keep the interval a single named constant.

Three standing invitations to do the ordinary thing, each of which fails silently:

- **Do not raise the ATT MTU or enable Data Length Extension.** At rung 3 the 27-byte payload is no longer a *precondition* for anything, so the argument is now the simpler one: there is nothing to carry. The largest frame is 10 bytes, a larger MTU lengthens air time against the density requirement, and it would quietly foreclose the SCI contingency.
- **Do not enable peripheral latency or connection subrating.** They are the standard BLE power levers and they work by skipping connection events, which is exactly what delays an acknowledgement tap. Subrating is not needed at all now that SCI is deferred.
- **Do not add an application-level retry to the press path.** Link-layer retransmission inside the connection event is the bounded effort this design wants. Anything above it fires only when the press is already worthless, and delivers the late tap `PROTOCOL.md` §11 rules out.

### 4.9 The 2:1 link is tested with two physical peripherals, asymmetric

**One nRF52840 DK as remote #1, one spare nRF52840 dongle as remote #2.** Not one DK holding two connections, and not a second DK. The full argument is in §3.5; the decision is recorded here because the rejected option is the cheaper-looking one and will look attractive again.

The load-bearing part is the first reason rather than the practical ones: M4 exists to measure **central** scheduling of two links, and terminating both on one peripheral radio introduces a peripheral-side scheduling constraint the product does not have. A rung-1 failure measured that way cannot be attributed, and an unattributable failure at M4 propagates into the power budget and from there into the M6 board.

The second remote is deliberately not a full remote. It holds a connection, consumes the downlink, generates uplink load, and reports telemetry — which is the entire set of things a second connection must do for §3.5's six questions to be answerable.

**Use the PCA10059 for bring-up and the spare MDBT50Q-CX-40 for range and density**, because the second measurement is a property of the module and antenna and the first is not. Swapping is a flash.

### 4.10 Provisioning is built for real at stage 2, with a bench tool rather than a process

`RADIO_PROTOCOL.md` §10.1 describes a record written once at manufacture. Read at bring-up time that sounds like permission to defer it, and it is not — it is stage 2, before any radio code, because nothing connects without it.

**Built now:** the record format, the CRC check, and the refuse-to-operate-unprovisioned path (A19) — code-complete 2026-08-12, `dongle/BUILD_SPEC.md` §9. **The LTK installation is stage 3**, not stage 2 — it needs `radio_ble.c` to exist, which reads `set_key` from the same record but doesn't yet. **Deferred:** the manufacturing process around it — a script generating a partition hex is sufficient and correct for three units, and `dongle/tools/provision.py` is that script.

A key compiled into the firmware as a `#define` would be faster and would make A12 (no key), A13 (set mismatch) and A19 (unprovisioned) untestable. Those three are most of what stands between this product and a cross-associated match at a multi-mat event, and FS §2.3 calls that a scoring-integrity failure rather than an inconvenience. A boot path added after the fact is also a boot path nothing ever exercised.

### 4.11 `CONFIG_DONGLE_FAKE_LINK` is deleted, not defaulted off

It fabricated `LINK … CONNECTED` with a fixed RSSI and battery so the app's indicators could be exercised before a radio existed. That was reasonable then and is a hazard now: **it fabricates precisely the values every link test is trying to measure, and it does so plausibly.** §5 listed it as a standing trap and `RADIO_PROTOCOL.md` notes it invalidates four rungs while producing entirely believable output.

A Kconfig default is not protection against that, because the failure mode is forgetting, and a forgotten `y` produces a passing test. Deletion is. Its replacement is `radio_null` (`CONFIG_DONGLE_RADIO=n`), which reports both remotes `DISCONNECTED` — **which is true** — and renders the downlink on the board LEDs. The app shows two disconnected remotes, correctly, and no result needs a caveat attached to it.

The same trap exists on the remote wearing different clothes and with none of the visibility: the DK has no battery, so `UP_TELEMETRY.battery_pct` is synthetic, and **there is no Kconfig symbol whose name gives it away**. `remote/BUILD_SPEC.md` §8.3 requires the synthetic value to be obviously synthetic rather than plausible.

### 4.12 Implementation detail lives in the build specs, not here

[`dongle/BUILD_SPEC.md`](dongle/BUILD_SPEC.md) and [`remote/BUILD_SPEC.md`](remote/BUILD_SPEC.md) are the implementable contracts, written 2026-08-10. They answer to `PROTOCOL.md` and `RADIO_PROTOCOL.md`, which answer to `SCOPE.md` and `SYSTEM_FUNC_SPEC.md`.

The split is that **this document says what state the project is in and why the work is ordered as it is; the build specs say what to build.** Module boundaries, function-level interfaces, state layouts, algorithms and per-stage acceptance belong there. When they and this document disagree about a mechanism, they are the more specific and they win; when they disagree about *sequence or status*, this document wins.

---

## 5. Validation ladder

**Work the rungs in order.** Each isolates one failure domain, and a failure high up is uninterpretable if a lower rung was skipped. Record the date and firmware version against each result in §9.2.

The interface "works" in the sense that a happy path completed once. That is a much weaker claim than "reliable", and the gap between them is where this class of system fails: at hour three, on a cable pull, on a backgrounded tab, on someone else's laptop.

**There are two ladders.** V0–V8 test the USB link, W0–W8 (§5.3) test the radio. They are separate because they isolate different domains. **The rung names are unchanged** so that §9.2's results log stays continuous; what changed is that they are now the exit criteria of M2's stages rather than a sequence gating one milestone behind another.

| Stage | Rungs | Note |
|---|---|---|
| 0 | — | Both suites written, running, and failing |
| 1 | **V0**, then V1–V6 | With `CONFIG_DONGLE_RADIO=n`. **This is the no-radio baseline** |
| 2 | A19 | ✅ Dongle side closed 2026-08-12. Remote side on both boards needs stage 4 |
| 3 | **W0**, W1 | W1 *including* A12, A13, A14, A19 |
| 4 | W2–W5 | With A15 and A20 |
| 5 | W6–W8, V8, R2 | Two connections, range, soak, measurement |

**The no-radio baseline is a build configuration now, not a milestone.** The previous revision required the whole V ladder green *before any radio code existed*. The isolation that bought is preserved and improved: stage 1 runs V0–V6 with the radio compiled out, and that configuration is **kept for the life of the project**, so §3.10's regression list is diagnosed by re-running it against the firmware in hand rather than by comparing against a result from six weeks earlier.

**Deferred, with reasons:**

- **V7** (version guard) — needs a deliberately-wrong rebuild and revert, and the app-side guard is unit-tested at M1. Cheap, low yield now, and it becomes live the moment a second dongle exists.
- **V8** as a *gate* — it stays a required rung but runs overnight once stage 4 is stable, rather than blocking progress.

**V1–V3 passed against protocol v2.0 and are void.** The message set they exercised no longer exists. They are cheap to re-run and must be, at stage 1.

### 5.1 Test rig

| Item | Value |
|---|---|
| Board target | `raytac_mdbt50q_cx_40_dongle/nrf52840` |
| SDK | nRF Connect SDK **v3.4.0** |
| Build | `source dongle/tools/ncsenv.sh` then `west build -b raytac_mdbt50q_cx_40_dongle/nrf52840 dongle -d dongle/build` |
| Artifact | `dongle/build/dongle/zephyr/zephyr.hex` |
| Flash | Hold the button while plugging in (LED fades), then write the hex with nRF Connect Programmer |
| App | `npm run dev` in `wrsl-app`, `http://localhost:5173/` |
| Browser | Chromium-based, desktop |

**The COM port is exclusive.** A serial terminal and the app cannot both hold it. Commands go to the dongle from the app's debug panel.

### 5.2 Standing traps

- **`TEST 3` suspends link supervision until `TEST 0` or reboot.** Left on, every supervision test in V5 passes for the wrong reason. Send `TEST 0` first and confirm the reply. This trap gets *worse* at v3.0, not better: with the dongle-side clock deleted, `TEST 3` is the only way to keep a bench terminal quiet, so it will be reached for more often.
- **`CONFIG_DONGLE_RADIO=n` means the link rungs are not being tested.** V1–V6 are wire-layer rungs and are valid in that configuration; anything about `LINK` state, RSSI or battery is not. This replaces the `CONFIG_DONGLE_FAKE_LINK` trap, which is retired along with the symbol (§4.11) — the difference being that a null radio reports `DISCONNECTED`, which is true and visibly so, where the fake reported `CONNECTED`, which was false and entirely plausible.
- **A synthetic `battery_pct` from the DK has no Kconfig symbol to notice.** It is the same class of trap with none of the visibility — §4.11.
- **Chrome DevTools' "Create live expression" cannot trigger the app watchdog (V5.7).** It catches a thrown exception internally to display it in the pin and never dispatches it as a real `window.onerror` — the app sees nothing and nothing changes on screen, with no indication that the test itself is the thing that failed. Use `window.dispatchEvent(new ErrorEvent('error', {message: '...'}))` from the actual console input or a bookmarklet instead.

### V0 — Parser unit tests (host, no hardware) — ✅ **GREEN 2026-08-11**

`protocol.c` has no Zephyr dependencies precisely so this can run anywhere.

```bash
source dongle/tools/hostenv.sh
cd dongle/tests/protocol && make check
```

**Result: 131 checks, 0 failures**, GCC 16.1.0 with `-Wall -Wextra -Werror`. Covers `PROTOCOL.md` §14 T1–T16, every button × gesture encoder round-trip, and the `LINK` RSSI constraint of §7. Details and the three cases worth naming are in §2.6.

**Why it had never run, established 2026-08-10 and resolved 2026-08-11.** Not "no compiler was handy" — **there was no host C compiler on this machine at all.** No WSL (`wsl.exe -l` reports the subsystem is not installed), no clang, nothing on `PATH`, and nothing inside the NCS toolchain bundle either: `C:\ncs\toolchains\dcbdc366a1\mingw64\bin` holds 50 executables and not one of them is a compiler, and the Zephyr SDK ships only `arm-zephyr-eabi` and `riscv64-zephyr-elf` cross-toolchains whose output will not run here. The board-target toolchain cannot substitute for a host one, which is the assumption that let this sit unrun for four days across two milestones.

Resolved by a one-time MinGW-w64 install, recorded as `dongle/tools/hostenv.sh` beside `ncsenv.sh` so the route is written down rather than remembered.

**The obligation this leaves: `make check` is still a remembered step.** It is not wired into `west build` and there is no CI, so nothing fails if it is skipped. CI remains the better answer and is not reachable today (§8).

The app's counterpart suite (`npm test` in `wrsl-app`) covers the same §14 cases from the other side: **137 tests green**. Both ends of this protocol are now tested, which is new — and they are supposed to agree, which the emulator wire-log diff is what actually checks.

### V1 — Manual terminal, no browser — ✅ **GREEN on everything this board can show, 2026-08-11**

Every wire response below was confirmed against firmware 0.2.0 on 2026-08-11 — the fixed responses from a scripted terminal, the haptic and `STATE` renders from the bench, and finally `STATE` again through the app itself once V2 was reached, so the closing observation was made twice by two different routes.

**The haptic half closed first.** `HAP GREEN LONG` blinks the lamp and `HAP RED LONG` does not, which establishes both that `indicator.c` renders and that it honours the target — a firmware driving both channels would have blinked on both ([`dongle/BOARD.md`](dongle/BOARD.md) §2.1).

**The `STATE` half closed second, operator-driven through the app's raw-command console** (`DetailPanel.jsx`'s System tab — built for exactly this, since the exclusive COM port means a bench terminal can't be up at the same time as the app). `STATE GREEN …` took the lamp to a steady level and held it. **`CFG` remains structurally unobservable on this board** — `indicator.c` drives GPIO, not PWM, so there is no amplitude or brightness for a scale factor to act on. Recorded as out of this rung's reach rather than pending; it first becomes observable at stage 4.

**Nothing is left open on V1** except what the hardware cannot show at all (`CFG`, anything addressed to `RED`).

Disconnect the app first. With a terminal on the port at 115200 8-N-1:

| Send | Expect |
|---|---|
| `INFO` | `HELLO 3.0 <fw> <set> 0`, then one `LINK` line per remote |
| `PING` | `PONG` |
| `ECHO hello` | `ECHO hello` |
| `STATE RED SOLID 00A0FF OFF 000000` | Indicator stand-in reflects it |
| `HAP BOTH LONG` | One long pulse |
| `CFG BOTH 50 50` | Accepted; subsequent haptics and LEDs at half scale |

**Pass:** all of the above. Observing `ERR APP_TIMEOUT` ~2.5 s after the last typed line is **correct** (`PROTOCOL.md` §8) and is itself strong evidence — it exercises RX, parse, state transition, timer and TX in one message. **Measured at 2512 ms.**

### V2 — App handshake — ✅ **GREEN 2026-08-11**

Run twice, independently — once mid-session and once from a cold reload requiring a fresh port grant — both from a browser tab holding the real port. Both times the app reached `ready` with `Set serial RR-0000`, `Dongle firmware 0.2.0`, `Protocol 3.0`, matching the dongle's own `HELLO` fields exactly. No version-guard warning.

The literal `INFO → HELLO → LINK → CFG → STATE → PING` line-by-line ordering was **not** independently captured — the wire-log UI keeps only the most recent 200 lines and the 1 Hz `PING`/`PONG` cadence rolls the handshake out of that window in about 100 seconds, faster than it can be reached by clicking through the panel. That specific check was abandoned as not worth a second native-picker interruption once the identity fields already gave equivalent evidence: `Protocol 3.0`/`Link ready` cannot be produced by a version-guard failure or a `HELLO` that didn't parse, and `STATE`'s round-trip is independently confirmed in V1. The ordering constraint (unprompted `STATE` before the first `PING`) is exercised by `useDongleConnection.test.js` and `DongleService.test.js` on the app side and is not re-litigated here.

### V3 — Deterministic stimulus — ✅ **GREEN 2026-08-11, both halves**

The dongle emits both sweeps correctly — seven and thirty-two, counted on the wire (§2.7). **Now also run through the live app**, via `TEST 1` / `TEST 4` in the same System-tab console that closed V1's `STATE` render, which is the actual reducer under real device timing rather than the emulator or a bench count.

**`TEST 1`:** seven `EVT` lines, `seq` 89–95 contiguous, alternating RED/GREEN. All seven `ACK`'d — `F1 PRESS GREEN` (94) got `ACK … SILENT` (F1 is inert in the loaded NFHS ruleset), every other press a plain `ACK`. `F2 PRESS RED` (95) additionally produced `STATE RED OFF 000000 SOLID FFFFFF` — the pending-choice assignment round-tripping to the indicator, live. Zero sequence gaps, zero duplicates.

**`TEST 4`:** thirty-two `EVT` lines, `seq` 96–127 contiguous — the dongle's own `LOG TEST 4: complete, 32 events` agrees with the app's count. `HOLD_REP` appeared only on `FORWARD` and `BACKWARD`, matching §3.12's correction. A second `F2` press-and-hold on each remote (110, 126) toggled the earlier assignment back off — `STATE RED OFF 000000 OFF 000000` — confirming the pending-choice **clear** path, not just assign. `TEST 0` afterward returned `LOG TEST 0: stopped, supervision active`. Zero gaps, zero duplicates throughout both sweeps.

**A sweep producing 21 for `TEST 4` is a defect, not a variant** — see §3.12.

### V4 — Reverse path (app → dongle) — ✅ **GREEN 2026-08-11, six of seven rows; the seventh is not reachable this way**

Driven from the scoreboard's own UI — NCAA Folkstyle (the ruleset with an active secondary clock; NFHS's `f1` is inert, §5 V1/V3), P1 shortened to 8 s so the warning and period-end fell inside a few real seconds instead of two minutes.

| Action | Expect on the wire | Result |
|---|---|---|
| Start the clock with a secondary clock owned | `HAP <owner> BEAT` once per second, on the owner only | **Pass.** `TX HAP RED BEAT` every second while RED owned, `TX HAP GREEN BEAT` every second after transfer — never both, never the non-owner |
| Transfer secondary-clock ownership | Beats move to the other remote within one beat | **Pass.** `STATE RED OFF …` and `STATE GREEN SOLID …` pushed together on the transfer click; the very next second's beat was `HAP GREEN BEAT` |
| Stop the main clock | Beats stop; no `STATE` change (ownership is retained) | **Pass.** Beats ceased the second the clock was stopped; no `STATE` line until the separate deassign click three seconds later |
| Deassign the secondary clock | `STATE` for that remote with F1 `OFF` | **Pass.** `STATE GREEN OFF 000000 OFF 000000`, on its own, from the deassign click |
| Let a period run to 0:00 | One `HAP BOTH LONG` | **Pass, twice independently** (two short periods run back to back). Exactly one `HAP BOTH LONG` each time |
| Reach the configured main-clock warning | One `HAP BOTH WARN` | **Pass, twice independently.** Exactly one `HAP BOTH WARN` each time, at the 5 s-remaining crossing (NCAA `warning_at_s: 5`) |
| Enter a burst of four `ADD_POINT` presses during accrual | Four `ACK` lines and **no `BEAT`** inside the suppression window | **Not reachable from the operator UI.** The on-screen `+1` dispatches the identical `INPUT` action (§4.2) but produces no wire traffic to acknowledge — there is no remote to ACK *to*. A real burst-suppression test needs dongle-originated `EVT`s (`TEST` modes or a physical remote), not an operator click. Two live attempts via rapid `+1` clicks left `beatsSuppressed` unchanged at 1, consistent with this. The mechanism itself is unit-tested (`DongleService.test.js`, "suppresses the beat during an input burst") and not re-litigated here |

**Six of seven rows pass live, on hardware, driven by the actual reducer.** The seventh is a gap in what the operator UI can exercise, not in the firmware or the app — recorded rather than chased, matching the call made on V2's handshake-ordering capture.

### V5 — Supervision and disconnection — ✅ **GREEN 2026-08-11, seven of seven checkable**

The most important rung. `TEST 0` sent first.

| # | Procedure | Pass criteria | Result |
|---|---|---|---|
| V5.1 | Clock running, secondary clock accruing; close the browser tab | Beats stop immediately; dongle emits `ERR APP_TIMEOUT` within 2.5 s and instructs both remotes to render link-lost | **Pass, dongle-emission half only.** Tab closed, a terminal opened on the freed COM port without sending anything (opening a port doesn't reset the dongle's supervision timer, only a received line does), and `ERR APP_TIMEOUT` printed inside the window. **The "instructs both remotes to render link-lost" half is not checkable here** — `radio_null.c`'s `radio_send_host()` is a stub under `CONFIG_DONGLE_RADIO=n` and its own comment says this is verified at stage 4. Deferred to **W5/A15**, not a gap in this result |
| V5.2 | Unplug the dongle | App shows disconnected, stops the `PING` cadence, offers reconnect | **Pass** |
| V5.3 | Sleep the laptop 30 s and wake it | App either recovers or cleanly reports a stale link; the match clock shows the correct elapsed time or halts and asks (FS §8.1) | **Pass, and a finding worth keeping.** At a 15 s sleep the app's own port handle survived the suspend — `ERR APP_TIMEOUT` fired and `PING` paused for the duration, then recovered automatically on wake with no manual reconnect needed. Elapsed time on the match clock was correct. A longer sleep may still force a manual V5.6 reconnect; this result pins the short-sleep case only |
| V5.4 | Kill the browser process outright | Same as V5.1 | **Pass** — same signature as V5.1's dongle-emission half |
| V5.5 | Pull the dongle while idle | Clean disconnect, no spurious `ERR` | **Pass** |
| V5.6 | Reconnect after any of the above | Handshake re-runs from scratch **including `STATE` assertion**; indicators are correct before anything else happens | **Pass, re-run three times** — after V5.2, V5.4, and V5.5. Full handshake and `STATE` reassertion confirmed each time. (Not needed after V5.3 at 15 s, per that row's finding.) |
| V5.7 | Trigger the app watchdog (FS §8.3) | App drops the serial link deliberately; remotes render link-lost; the fault is visible on the display | **Pass.** Triggered via `window.dispatchEvent(new ErrorEvent('error', {message: 'V5.7 test'}))` from a bookmarklet. Fault banner shown ("Application fault. The dongle link was dropped deliberately."), `PING` stopped, dongle timed out on schedule. **DevTools' "Create live expression" does not work for this** — it catches a thrown exception internally for its own display and never lets it reach `window.onerror`, so that route silently produces no effect. New trap, §5.2 |

V5.3 is materially harder at v3.0 than at v2.0 and worth extra attention: the clock must be immune to wall-clock adjustment across a suspend, and an implausible gap must halt the clock rather than be absorbed. **Confirmed at 15 s; not yet tried at the full 30 s or longer**, where the OS is more likely to actually tear down the USB device.

### V6 — Reconnect lifecycle — ◐ **2026-08-11, three of five closed; two deferred**

| # | Procedure | Pass criteria | Result |
|---|---|---|---|
| V6.1 | Disconnect in the app, reconnect | No port picker — `getPorts()` reuses the grant | **Pass, on a narrower procedure than written.** There is no disconnect control in the app's UI — a disconnect only ever arrives externally (unplug, tab close, watchdog), which V5 already covers. What V6.1 can actually check is the reconnect half: after a V5-style disconnect, the reconnect button was used and **no OS port picker appeared** — `getPorts()` reused the existing grant, as required. Recorded as a UI-reach gap on the "disconnect in the app" clause, same treatment as V4's row 7 |
| V6.2 | Unplug, replug, reconnect | Handshake re-runs in full | **Pass** |
| V6.3 | Set a score and a secondary-clock owner, disconnect, reconnect | **No match state is lost, and no dongle state is resumed.** Indicators reassert from the app's copy | **Pass** |
| V6.4 | Reconnect 10× in a row | No leaked readers or writers; no duplicate `PING` cadences (watch for `PING` faster than 1 s) | **Deferred.** Ten manual reconnect cycles with a stopwatch on the `PING` interval isn't a reliable way to catch a one-interval leak by eye — this needs scripting (drive the reconnect and read `beatsSent`/wire-log timestamps programmatically) rather than a person watching. Returning to it, not skipping it |
| V6.5 | Mid-match, swap to a different dongle | Match state fully retained; new set serial displayed; `STATE` asserted to both new remotes before the clock restarts; the substitution appears in the match record | **Deferred, not pursued now.** Needs a second flashed dongle in hand; not chased today by choice rather than blocked |

V6.4 finds real bugs — a reader lock or interval leaked per reconnect is invisible until it isn't. V6.5 is the field-substitution procedure of `SCOPE.md` §8.6 and is the reason `STATE` exists. **Both stay open rather than closed-by-inspection**, and are exactly the kind of thing a full ladder re-run should catch: once the DK remote and a second dongle exist for other reasons (M4, M5), re-running V0–V8 as a quality-control pass — not just the rungs a new stage adds — is cheap and catches regressions the staged, one-thing-at-a-time approach can miss between milestones. §3.10 already establishes this pattern for B1/B2 (re-run V4/V5 with the radio live); the same logic extends to V6.4 and V6.5 once the hardware to run them properly exists.

### V7 — Version guard

Emit `HELLO 4.0 …`, flash, connect.

**Pass:** the app refuses to operate and says to update the dongle firmware. Then `HELLO 3.1 …`: the app warns and **continues**.

Revert afterwards. This is cheap and it is the only mechanism protecting against a mixed-firmware fleet — which is a live concern the moment a second dongle exists.

### V8 — Soak

`TEST 2`, with the app connected and supervision **on**. **Minimum 4 hours, preferred overnight.**

**Pass:**

- **Zero sequence gaps.** On 3 cm of USB there should never be one. A gap is a real finding.
- **Zero duplicate `seq` applied.** The dedupe counter may be non-zero; the applied-twice counter must be zero.
- Acknowledgement latency p99 well inside budget, and stable over the run.
- No RAM growth; no transmit-ring drops beyond `BEAT`.
- COM port never drops; the app never goes stale; the UI stays responsive.

**Do not judge scoreboard correctness during V8.** `TEST 2` fires at random and the board will look nonsensical by design. V8 measures throughput, `seq` integrity and latency; V3 is the behavioural test.

**Instrumentation — closed at M1.** The v2.0 app logged gaps as one-off warnings into a ring buffer that would have rolled over long before anyone read it, which made V8 unfalsifiable. Running counters now exist (§2.3), with a JSON diagnostics export from the detail panel. **Export at the start of the soak as well as the end** — the counters are cumulative and monotonic, so a single reading at the end cannot distinguish a fault at hour one from a fault at hour four.

### 5.3 The radio ladder — **NOT RUN, NOT WRITTEN**

The counterpart to §5.1–§5.2, for `RADIO_PROTOCOL.md`. Every rung is new and none has ever been executed. The `A`-references are the conformance cases of `RADIO_PROTOCOL.md` §14; the `B`- and `R`-references are §3.10 and §7.

**W0–W1 are M2 stage 3, W2–W5 are M2 stage 4** (one connection). **W6–W7 are M4** (two). W8 is worked at M4 and re-run in full at M7.

| Rung | Stage | What it isolates | Pass |
|---|---|---|---|
| **W0** | 3 | **Frame codec, on a host, no hardware** | A1–A7, A11, A20 green. The radio's V0, and it exists only if the codec is written without Zephyr dependencies (§3.4 deliverable 5) |
| **W1** | 3 | **Association and security.** One connection, encrypted from the provisioned key, no pairing procedure performed | `RR_IDENTITY` read and validated, CCCD subscribed, `LINK … CONNECTED` with a real RSSI. Negative cases are the point: A12 no key, A13 set mismatch, A14 proto major, A19 unprovisioned. **A pass on the positive case alone is not a pass** |
| **W2** | 4 | **Uplink.** Button → `UP_INPUT` → `EVT` → scoreboard | All three gestures on the four DK buttons (§3.4); 600 ms hold and 150 ms repeat measured, not assumed; A4 duplicate, A5 gap, A6 wrap |
| **W3** | 4 | **Downlink.** `STATE` → `DN_INDICATOR`, `HAP` → waveform, `CFG` → scaling | Indicators assert idempotently (A11); **`ACK … SILENT` puts nothing on the air** (A20) — verify by frame count, not by watching an LED that was never going to light |
| **W4** | 4 | **Round trip and the deadline rule** | `EVT`→`ACK`→render measured as a distribution. A8 a late `ACK` is not sent at all, A9 a second `ACK` replaces rather than queues, A10 a `BEAT` never truncates a `TAP`. `taps_dropped_late` non-zero when provoked and zero when not |
| **W5** | 4 | **Link state, in all four supervision relationships** (`RADIO_PROTOCOL.md` §9.1) | **A15 is the rung** — app supervision expires, radio stays up, both remotes render link-lost. Also A16 boot-is-DOWN, A17 sub-2 s reconnect emits no `DISCONNECTED`, A18 press out of range, A7 reboot re-baselines with no false gap, and §9.4 debounce under repeated power-cycling at the range edge (B4) |
| **W6** | **M4** | **Two connections.** The rung M4 exists for | That 7.5 ms is clean on the pair — no dropped events, no event-length overruns — with the interval reported in the setup `LOG` line so every latency figure is attributable. **B6: taps land on the originating remote only.** R5: cross-connection arrival skew measured, and it is **wider at rung 3 than the previous plan assumed**. Beat on the owner only, from real hardware. B2 with the transmit-ring drop counter already instrumented |
| **W7** | **M4** | **Range, link budget and density**, at the dongle **as deployed** | 12 m with body shadowing, dongle in a laptop port below table height — not a bench with line of sight. p99, not median. Whatever density can be synthesised. **Take this rung with the MDBT50Q-CX-40 as remote #2** (§4.9). Escalation order if it does not close is fixed: USB extension cable, then a placement constraint in the documentation, then transmit power |
| **W8** | M4, re-run M7 | **Radio soak, and the §3.10 regression list in full** | ≥4 h with both remotes connected and pressing. `radio_gap` and `radio_dup` accounted for rather than merely observed; no transmit-ring drops beyond `BEAT`; B1 workqueue contention re-measured with the radio live; V4 and V5 re-run underneath it |

**Three traps specific to this ladder**, in the spirit of §5.2:

- **Building the radio rungs with `CONFIG_DONGLE_RADIO=n` invalidates all of them**, obviously — but the check worth running is B3's: confirm `INFO` reports `DISCONNECTED` with no remotes powered, and that it does so because nothing is connected rather than because a symbol says so. `CONFIG_DONGLE_FAKE_LINK` is deleted (§4.11), which retires the version of this trap that produced *plausible* output.
- **A synthetic `battery_pct` from the DK invalidates anything about the battery indicator**, with none of the visibility — there is no Kconfig symbol to notice. It is real only from M5.
- **A one-connection latency figure is not a two-connection latency figure.** Do not carry a W4 number forward past W6. This matters more at rung 3 than it did at rung 1, because the retransmission headroom being measured against is thinner.

---

## 6. Deployment validation — **NOT RUN**

The product ships from a website onto organisation-managed computers. That introduces failure modes no bench test reveals.

| # | Test | Pass criteria |
|---|---|---|
| D1 | Serve over real HTTPS (not localhost) | Works. Plain `http://` on a LAN IP will **not** |
| D2 | Chromium-based browsers, **on a stock profile** (§2.4) | Connects. Non-Chromium degrades with a clear message rather than a broken page |
| D3 | Linux client | Port opens. Requires `dialout` membership or a udev rule — without it Chrome lists the port and fails to open it, opaquely |
| D4 | Machine with `DefaultSerialGuardSetting=2` | App detects the block and says so in plain language, not as a raw DOMException |
| D5 | Machine with `SerialAllowUsbDevicesForUrls` allowlisting the origin + VID/PID | Connects with **no picker and no prompt** |
| D6 | Change the site origin, then reconnect | Confirms the expected loss of all grants |
| D7 | Full offline run — load once, disconnect the network, run a complete match | Works end to end. This is `SCOPE.md` §7.3 and it is the one users will actually exercise |
| D8 | Ten-hour session on one host without restart | No memory growth, no clock drift, no degradation of the debug ring |

**Blocker for D5:** the dongle enumerates with Zephyr's test identity (`VID 0x2fe3`, `PID 0x0004`, `"CDC ACM serial backend"`). A policy allowlisting `0x2fe3` would grant the site access to any Zephyr device the user plugs in, and IT will reject it. A real VID/PID and product string are prerequisites, and `requestPort()` must carry a matching `filters:` array.

**D6 is a decision, not just a test.** Serial grants are per-origin. Decide the production domain before deployment, not after.

---

## 7. Unmeasured risks

### R1 — Background-tab throttling versus the heartbeat — **materially worse at v3.0**

Chrome throttles timers in hidden tabs; after several minutes hidden a tab can drop to roughly one callback per minute.

At v2.0 this threatened only the liveness proof, because the heartbeat was generated on the dongle. **At v3.0 the heartbeat is generated by the app**, so throttling stops the referee's heartbeat directly, and the `PING` cadence with it. Alt-tabbing away from the scoreboard would stop the beat on the referee's wrist while riding time continued to accrue on a display nobody is looking at.

`SCOPE.md` §5 makes a foregrounded scoreboard a stated operating assumption, and it is an easy one to hold in practice — the scoreboard is the mat's public state indicator. But an assumption is not a mechanism.

**Test:** connect, start a secondary clock, confirm the beat. Fully hide the window for 6+ minutes. Return and read the counters.

**Fail:** any beat interval materially over one second, or `ERR APP_TIMEOUT` present.

**Mitigations, in order of preference:** a screen wake lock, which is appropriate anyway for an application driving a public display; moving the beat and `PING` into a Web Worker, which is not throttled the same way; and surfacing loss of foreground in the primary tier so the operator sees the state the assumption was violated in. Widening the supervision timeout is not a mitigation — it weakens the fail-safe the whole design rests on.

**Applied at M1:** the screen wake lock, and a foreground banner that appears the moment the window loses visibility. **Not applied:** the Web Worker, which is the only one of the three that actually keeps the beat running rather than merely telling the operator it stopped. Deliberately deferred — it is real work, and the test above has to run first to establish whether the wake lock alone is sufficient in practice. Until that test runs, `SCOPE.md` §5's foregrounded-window assumption is load-bearing in a way it was not at v2.0.

### R2 — The 120 ms acknowledgement budget has never been measured

`PROTOCOL.md` §11 allocates 120 ms across six hops. The v2.0 round trip through React render and the browser event loop was never measured even against 500 ms, and that path is now allocated 25 ms with two radio hops added around it.

**Test:** with `TEST 2` as background load, pair each `RX EVT … <seq>` with its `TX ACK <seq>` and take the difference. **Measure the distribution, not the median** — p99 is what matters. The app's ack-latency counters (§2.3) make the app share measurable the moment M2 lands.

**Fail:** any sample approaching the app's 25 ms allocation, or any total approaching 120 ms once the radio exists.

**Why it matters:** exceeding the window means no tap. The referee follows the rule correctly — *no tap means the press did not land, press again* — and scores twice. The failure is silent and looks like referee error.

### R3 — The ERM has to cover the whole haptic range

FS §3.3 assumes one ERM plus driver IC delivers unmistakable expiry amplitude, a countable reduced-amplitude heartbeat, and an acknowledgement inside 120 ms. Motor spin-up alone is allocated 20 ms of the budget and is the hard floor. This cannot be settled from datasheet figures. Contingency is an LRA or a dual-motor revision, which affects enclosure and cost.

### R4 — Amplitude separation has to be perceptible

Whether the difference between `BEAT` and `TAP` is reliably distinguishable on the wrist, in motion, through a strap, by a referee not attending to it. This is the assumption repeated-press scoring rests on (FS §11.1, §15.5), and it fails quietly: a referee who miscounts a near fall has no way to know.

### R5 — Event ordering under rapid exchange

Order of receipt is authoritative, assuming referee input intervals comfortably exceed transit variance. Log inter-press intervals during live matches against measured transit jitter. If it fails, remote-side sequencing is required, which adds protocol complexity (FS §15.1).

### R6 — Ten-hour battery life

Measure average current attributable to the radio at the connection cadence the acknowledgement budget requires, and to a reduced-amplitude 1 Hz beat over a representative match. The motor is expected to dominate. Heartbeat suppression is **not** available as an unconditional mitigation, because the beat carries the running/paused distinction — any reduction in beat density must be accompanied by the LED taking that distinction over, which is the basis of the planned power-saving mode (FS §11.2).

---

## 8. Known gaps and debt

| Item | Impact |
|---|---|
| ~~The application has never held the port at v3.0~~ | **Closed 2026-08-11.** V2–V6 all run (V6.4/V6.5 deferred, not blocked on this). The port is exclusive, so browser work and terminal work cannot be interleaved — that constraint stays live even though the gap it used to describe is gone |
| **The `STATE` render is unobserved** | Accepted with no error, which is byte-identical on the wire to a dead indicator. Unlike `CFG` it **is** observable here — two commands on the `GREEN` channel, §5 rung V1. The haptic half of this gap closed 2026-08-11 |
| **`CFG` and `HAP RED` are unobservable on this board, permanently** | `indicator.c` drives GPIO, not PWM, so `CFG`'s amplitude and brightness scaling has nothing to act on; and `RED` drives the unfitted footprint, so a correctly-routed command there is indistinguishable from one that does nothing. Neither is a defect and neither is closable at the bench — both first become observable at **stage 4** on the DK. Recorded so they are not carried as open rungs indefinitely — [`dongle/BOARD.md`](dongle/BOARD.md) §2 |
| **Fault indication is invisible on the dongle** | `indicator_error()` drives the unfitted P0.08, so **no `ERR` produces any visible signal** — including `APP_TIMEOUT`. Kept deliberately rather than moved to the fitted lamp, because that would put errors on the same channel as every `GREEN` haptic and destroy the asymmetry that proves routing; errors already report on the wire, which is richer. The rule to remember: **"no blink" never means "no error"** — BOARD.md §2.2 |
| **The emulator emits no `LOG counters` lines** | Surfaced by the wire-log diff. The firmware reports counters on handshake and the model has none, so the app's counter-display path is never exercised against the emulator — the one place it is cheap to exercise |
| **The emulator collapses runs of spaces in `ECHO`** | `dongleModel.js` reconstructs with `args.join(' ')` where the firmware preserves the raw line, as §5.7 requires. Predicted before the diff and confirmed by it. The firmware is correct; the model is the thing to fix |
| Radio protocol specified, implemented nowhere | `RADIO_PROTOCOL.md` v1.0 has no implementation on either side and no conformance suite. Its §14 cases are the counterpart to `PROTOCOL.md` §14 and, like V0, will be trusted on inspection until something runs them. M2 stages 3–4 |
| Every latency figure in `RADIO_PROTOCOL.md` §12 is arithmetic | The connection-interval table and the retransmission counts are predictions from documentation and have never been near this hardware. **The gap this leaves is now load-bearing**: §4.8 chooses rung 3 partly *because* the retransmission rate at 12 m is unmeasured, so measuring it is what would reopen the decision. R2 |
| ~~No provisioning record, reader, or tool~~ | **Closed 2026-08-12.** `common/provisioning.c`, `dongle/src/provisioning_flash.c`, `dongle/tools/provision.py` — host-verified and then confirmed on real hardware, A19 both halves, dongle side. **Residual: the remote-side half needs stage 4 firmware** — §3.1 stage 2, §4.10 |
| **No radio conformance harness** | A1–A20 have the same status V0 had: written, never run. Rung W0, and it is only cheap because the frame codec is specified free of Zephyr dependencies from the first line (§4.12) |
| **No DFU strategy for the remotes** | Absent from every document, and `RADIO_PROTOCOL.md` §10.3 makes it consequential — the case against manufacture-time bonding was that a firmware update can silently clear a settings partition. Decide at M6, at the latest |
| **No hardware design work of any kind** | Module, ERM and driver, PMIC production part, battery sizing, button mechanics, enclosure, APPROTECT. M6, and its inputs are M4 and M5 measurements — §3.3 |
| ~~Host parser tests never executed~~ | **Closed 2026-08-11.** 131 checks green on MinGW-w64 GCC 16.1.0. The residue is that `make check` is not wired into `west build` and there is no CI, so it remains a remembered step |
| V6.4, V6.5, V7, V8 never run | Handshake, reverse path, supervision and most of reconnect are closed (V2–V6, §9.2). **V6.4** (10× reconnect) needs scripted observation, not manual watching; **V6.5** (dongle swap) needs a second flashed dongle and isn't being chased now; **V7** (version guard) is deferred by design until a second dongle exists; **V8** (soak) is staged for stage 5, after the DK remote and second peripheral |
| Rulesets not verified against current rulebooks | The library is written and the schema is right, but the **numbers have not been checked against the published rules for the current cycle**. They are implementation-accurate, not authoritative. This must happen before any real match and again at each rules cycle |
| USB identity is Zephyr's test VID/PID | Blocks the enterprise deployment path (D5) |
| `requestPort()` has no `filters` | Users can select the wrong serial device. Blocked on a real VID/PID |
| Connection errors surface raw DOMException text | A policy block is indistinguishable from a cancelled picker (D4) |
| Heartbeat survives only in the foreground | Wake lock and banner applied; the Web Worker that would actually keep it running is not. R1 |
| No remote hardware | Every haptic and indicator requirement is unvalidated on the surface that carries it |
| Design-system fonts fetch from Google Fonts | `design-system/tokens/fonts.css`. Survivable — the pre-event load caches them and every family falls back to a system face — but self-hosted `.woff2` is the correct fix when licensed binaries exist |

**Closed at M1:** soak instrumentation (running counters and a JSON diagnostics export now exist, so V8 is falsifiable); the ruleset library (written as data, though see the verification gap above).

---

## 9. Definition of done and history

### 9.1 Definition of done

- [x] Application at v3.0, tested and browser-verified (M1)
- [x] Soak instrumentation exists — counters and diagnostics export
- [x] Build specs written — `dongle/BUILD_SPEC.md`, `remote/BUILD_SPEC.md` (§4.12)
- [x] Host C compiler installed and `dongle/tools/hostenv.sh` recorded (M2 stage 0) — 2026-08-11
- [x] **V0 green** — 131 checks, 0 failures, 2026-08-11
- [ ] Both host suites part of the routine build rather than a remembered step — **still remembered**; both suites (`protocol` 131 checks, `rframe` 67 checks) now exist and pass, but neither runs except by hand
- [ ] V1–V6 pass at v3.0 with `CONFIG_DONGLE_RADIO=n`, recorded in §9.2 — **the no-radio baseline, and it stays re-runnable** (§5)
- [x] **W0 green** — `dongle/tests/rframe`, 20 checks (67 assertions), 0 failures, 2026-08-12
- [ ] W1 pass *including* its negative cases (M2 stage 3) — needs hardware
- [ ] W2–W5 pass on one connection, with A15 and A20 (M2 stage 4)
- [ ] **The demonstration:** press on the DK → score on the scoreboard → tap rendered on that DK, and on that DK only
- [ ] W6–W7 pass on two connections, with the interval in use recorded against every latency figure (M4)
- [ ] R1–R6 measured, with mitigations applied where they fail — **R3, R4 and R6 before M6 opens** (§3.3)
- [ ] D1–D8 pass; real VID/PID assigned and `requestPort()` filtered
- [ ] V8 clean for ≥4 h with zero sequence gaps and zero applied duplicates, using real instrumentation
- [ ] W8 clean for ≥4 h with both remotes connected, `radio_gap` and `radio_dup` accounted for
- [ ] Every ruleset in the library checked against the published rulebook for the current cycle
- [ ] §3.10 re-run in full after the radio lands, and again at M7 on custom hardware
- [ ] `PROTOCOL.md` amended for any further constraint that proves real; `RADIO_PROTOCOL.md` likewise, and its §12 predictions replaced by measurements
- [ ] **The rung 3 decision revisited against a measurement** — either confirmed by a p99 that closes 25 ms at 12 m through a torso, or reopened in favour of SCI (§4.8)

### 9.2 Results log

| Date | FW | Proto | Rung | Result | Notes |
|---|---|---|---|---|---|
| 2026-08-07 | 0.1.0 | 2.0 | V1 | pass | *void at v3.0* — `ERR APP_TIMEOUT` observed and correct |
| 2026-08-07 | 0.1.0 | 2.0 | V2 | pass | *void at v3.0* — handshake and PING cadence confirmed |
| 2026-08-07 | 0.1.0 | 2.0 | V3 | pass | *void at v3.0* — `TEST 1`, seven events, confirmation round trip |
| 2026-08-09 | — | 3.0 | app | pass | M1: 113 tests, lint and build clean, browser-verified against `FakeDongleTransport`. **Not a ladder rung** — no hardware involved |
| 2026-08-11 | 0.2.0 | 3.0 | **V0** | **pass** | **First execution in the project's history.** 131 checks, 0 failures. GCC 16.1.0, `-Wall -Wextra -Werror`. T1–T16 including T7 and T11; every button × gesture round-trip. One real defect caught on first compile — §2.6 |
| 2026-08-11 | 0.2.0 | 3.0 | build | pass | `CONFIG_DONGLE_RADIO=n`, both the default and the explicit build directory. 54,380 B flash (5.21%), 21,688 B RAM (8.27%). No warnings. `=y` refused with a message naming stage 3 |
| 2026-08-11 | 0.2.0 | 3.0 | **V1** | **pass** | **Wire and render both green.** `HELLO 3.0 0.2.0 RR-0000 0`; `PONG`; `ECHO` verbatim with runs of spaces intact; `ERR APP_TIMEOUT` at **2512 ms**; malformed lines rejected with a reason. Haptic render and per-remote routing green — see the board row below. `STATE` render closed via the app's raw-command console once V2 was reached. **Nothing left open except `CFG`/`HAP RED`, unobservable on this board and reassigned to stage 4** — §2.7 |
| 2026-08-11 | 0.2.0 | 3.0 | board | **pass** | **Measured, after two wrong answers derived from the devicetree aliases.** With supervision suspended: `HAP RED LONG` → **nothing**; `HAP GREEN LONG` → **~500 ms blink**. So the one fitted lamp is on **P0.06**, transposed from Raytac's pin table, and **per-remote routing is proven** — a firmware driving both channels would have blinked on both. Written up with citations as [`dongle/BOARD.md`](dongle/BOARD.md). Corollary: `indicator_error()` drives the unfitted pin, so **every fault indication is invisible** and is deliberately left that way |
| 2026-08-11 | 0.2.0 | 3.0 | ack budget | pass | The 120 ms window shows all three bands: `ACK` at EVT+10 ms taps, at EVT+107 ms is **withheld** and increments `late`, at EVT+302 ms finds the entry already swept. §4.5 — late degrades to silence — confirmed on hardware, and not observable any other way |
| 2026-08-11 | 0.2.0 | 3.0 | emulator diff | **pass** | Stage 1 exit criterion. Firmware and `dongleModel.js` driven with one identical script; **all 39 `EVT` lines match exactly** on button, gesture, remote and rebased `seq`. Seven differences, all accounted for — including the `ECHO` space collapse **predicted before the run** — §2.7 |
| 2026-08-11 | 0.2.0 | 3.0 | **V2** | **pass** | **First time the application has held the port at v3.0.** Reached `ready` twice, independently, each from a fresh port grant: `Set serial RR-0000`, `Dongle firmware 0.2.0`, `Protocol 3.0` — exact match to the dongle's `HELLO`. No version-guard warning. The literal handshake line ordering was not separately captured (wire-log UI keeps only 200 lines, rolled past by the time it was checked) and that check was abandoned rather than chased with a second picker interruption — the identity fields plus V1's independent `STATE` confirmation are equivalent evidence |
| 2026-08-11 | 0.2.0 | 3.0 | **V3** | **pass** | **Both halves now green — run through the live app, not just counted on the wire.** `TEST 1`: seven events, `seq` 89–95 contiguous, alternating RED/GREEN, all seven `ACK`'d, `F1 PRESS GREEN` correctly `SILENT` (inert in the loaded ruleset), `F2 PRESS RED` correctly plain-`ACK`'d with a `STATE` push. `TEST 4`: 32 events, `seq` 96–127 contiguous, `HOLD_REP` only on `FORWARD`/`BACKWARD`, dongle's own `LOG TEST 4: complete, 32 events` agreeing with the app's count, and the `F2` pending-choice **clear** path confirmed on the second press. Zero sequence gaps, zero duplicates, both sweeps |
| 2026-08-11 | 0.2.0 | 3.0 | **V4** | **pass (6/7)** | **Reverse path, driven from the scoreboard UI**, NCAA Folkstyle (active secondary clock), P1 shortened to 8 s. `HAP RED BEAT`/`HAP GREEN BEAT` once per second, strictly on the owner, never both. Transfer: `STATE RED OFF …` and `STATE GREEN SOLID …` pushed together, beat follows within one second. Stop: beats cease immediately, no `STATE` line. Deassign: `STATE GREEN OFF …` alone. `HAP BOTH WARN` at the 5 s-remaining crossing and `HAP BOTH LONG` at 0:00, each exactly once, **reproduced on two independent periods**. **Burst suppression (row 7) is not reachable from the operator UI** — an on-screen `+1` has no remote to `ACK` to, so it generates no wire traffic to suppress against; two live attempts left `beatsSuppressed` unchanged. Covered instead by `DongleService.test.js`'s existing unit test. Recorded as a UI-reach gap, not chased further — same call as V2's handshake-ordering capture |
| 2026-08-11 | 0.2.0 | 3.0 | **V5** | **pass (7/7 checkable)** | **All seven procedures green.** V5.1: tab closed, terminal opened on the freed COM port without transmitting, `ERR APP_TIMEOUT` caught inside the 2.5 s window; the "remotes render link-lost" half is out of reach on this board (`radio_null.c` stub) and moves to W5/A15 at stage 4. V5.2 unplug: clean disconnect, `PING` cadence stopped, reconnect offered. V5.3 sleep 15 s: **the app's port handle survived the suspend** — `ERR APP_TIMEOUT` fired and `PING` paused during the sleep, then recovered automatically with no manual reconnect; elapsed match time correct on wake. Not yet tried at 30 s+. V5.4 kill-browser: same signature as V5.1. V5.5 pull-while-idle: clean disconnect, no spurious `ERR`. V5.6 re-run three times (after V5.2, V5.4, V5.5): full handshake and `STATE` reassertion each time. V5.7 watchdog: `window.dispatchEvent(new ErrorEvent('error', ...))` via bookmarklet — DevTools "Create live expression" does **not** propagate a thrown exception to `window.onerror` and was a dead end first (§5.2 trap); the real dispatch produced the fault banner, stopped `PING`, and the dongle timed out on schedule |
| 2026-08-11 | 0.2.0 | 3.0 | **V6** | **pass (3/5), 2 deferred** | V6.1: the app has no in-UI disconnect control, so only the reconnect half of the procedure is checkable — reconnecting after an external disconnect showed **no OS port picker**, `getPorts()` reused the grant. Recorded as a UI-reach gap on the untestable clause, not a fail. V6.2 unplug/replug/reconnect: full handshake re-ran. V6.3 score + secondary-clock owner through a disconnect/reconnect: match state fully retained, no dongle state resumed, indicators reasserted from the app's copy. **V6.4 (10× reconnect) and V6.5 (dongle swap) deferred** — V6.4 needs scripted observation rather than a person watching a `PING` interval by eye, V6.5 not pursued today by choice. Both stay open and are the first candidates for a full-ladder QC re-run once M4/M5 hardware exists, per §3.10's existing re-run pattern |
| 2026-08-12 | — | — | provisioning host suite | **pass** | `common/provisioning.c` against `dongle/tests/provisioning`: 54 checks, 0 failures. CRC-32 check value, all three roles, erased-flash-reads-as-bad-magic, bad version, a single flipped bit caught by the CRC, `set_serial` charset and NUL-padding (bad char, garbage after the NUL, all-NUL), bad role, full field fidelity, and a failed parse leaves `*out` untouched |
| 2026-08-12 | — | — | provision.py round-trip | **pass** | A generated set's dongle record, decoded independently from its Intel HEX output and fed back through `provisioning_parse()`, comes back with the identical serial, role and address bytes — the reader and the bench tool agree, which is the whole point of pinning the CRC variant and role values in one shared header rather than two implementations |
| 2026-08-12 | 0.2.0 | 3.0 | build | pass | `CONFIG_DONGLE_RADIO=n` with `provisioning_flash.c` and `common/provisioning.c` linked in. 55,996 B flash (5.36%), 21,752 B RAM (8.30%). No warnings. **Built, not flashed** — A19 on hardware is the next rung |
| 2026-08-12 | 0.2.0 | 3.0 | **A19** (unprovisioned) | **pass** | Reflashed board, `storage_partition` erased. `INFO` → `HELLO 3.0 0.2.0 RR-0000 0`, the fixed fallback, matching `provisioning_load()` returning non-`PROVISIONING_OK` and `send_hello()`'s branch. `ERR NO_PROVISIONING` itself was not independently caught on the wire: it fires once, at boot, before any terminal can have the port open — a harder version of V5.1's observation problem, since unlike `APP_TIMEOUT` it never re-fires. Not chased further; the fallback serial is the repeatable evidence that matters |
| 2026-08-12 | 0.2.0 | 3.0 | **A19** (provisioned) | **pass** | `dongle/tools/provision.py RR-0001`'s dongle record written to `storage_partition` on the physical board via `nrfutil nrf5sdk-tools dfu usb-serial`. `INFO` → `HELLO 3.0 0.2.0 RR-0001 0` — exact match. **Two dead ends on the way, recorded in `dongle/BUILD_SPEC.md` §9**: nRF Connect Programmer's GUI, given both the app hex and the provisioning hex together, timed out (`Slip decoder error`) — almost certainly from treating the pair as one merged image spanning the ~900 KB gap between them; and the GUI's DFU packaging step has no "no SoftDevice" option, requiring the `nrfutil` CLI's `--sd-req=0x00` instead. `nrfutil`'s core binary was installed but its `nrf5sdk-tools` plugin was not — `nrfutil install nrf5sdk-tools` is a one-time step, same shape as the MinGW-w64 install (§2.6). A momentary scare where the dongle stopped enumerating after the provisioning write turned out to be the bootloader sitting in DFU mode pending a manual power cycle, not corruption — the subsequent app reflash (which necessarily replugged the board) resolved it, and `storage_partition` survived that reflash untouched, exactly as `BUILD_SPEC.md` §9 says it should |
| 2026-08-12 | — | — | rframe cross-check | **pass (13/13)** | `common/rframe.c` and its CTR arithmetic reimplemented independently in a throwaway Python script and run against the same A1–A7 cases the host suite covers, because the host toolchain's `as.exe` was blocked in this environment (below). **Caught a real bug**: the `a6_wrap_is_consecutive` fixture primed `rframe_ctr_state` by calling `rframe_ctr_accept(&s, 255, …)` against a freshly-init'd state (`last == 0`), which the CTR window logic itself classifies as a duplicate (`back = (0-255) mod 256 == 1`, inside the window) rather than as priming — so the fixture silently failed to establish `last == 255`, and the wrap assertion that followed would have passed for the wrong reason. Fixed by setting `.last` directly in the fixture rather than priming through `accept()`, in both the C test and the Python cross-check |
| 2026-08-12 | — | — | rframe host suite | **blocked** | `dongle/tests/rframe`, 20 checks, written and `-fsyntax-only` clean, but `make check` cannot run: `as.exe` (MinGW-w64, the same toolchain that ran the protocol and provisioning suites at 131 and 54 checks) fails with `Permission denied` — from `gcc` invoking it internally, and identically when invoked directly, and identically for a fresh copy in a different directory. `gcc.exe` itself runs fine. Reads as an OS/AV-level block on this specific binary rather than a code or path problem; not resolved in this session. Needs `make check` run on a machine where the toolchain isn't blocked, or the block diagnosed with admin access (Defender exclusions require it) |
| 2026-08-12 | — | 3.0/1.0 | build | **pass** | `west build -DCONFIG_DONGLE_RADIO=y`: `radio_ble.c` and `common/rframe.c` linked in, 184,724 B flash (17.69%), 44,248 B RAM (16.88%), no warnings beyond the pre-existing `flash_area_open` deprecation notice. `CONFIG_DONGLE_RADIO=n` rebuilt alongside and unaffected, 56,200 B flash — the +204 B over the previous no-radio build is `engine.c`'s new `on_gap`/`on_dup`/`on_fault` callback wiring, present regardless of radio config |
| 2026-08-12 | — | 3.0/1.0 | build | **pass** | `west build -b nrf52840dk/nrf52840 remote`: clean on the first attempt, 154,820 B flash (14.76%), 31,884 B RAM (12.16%), same pre-existing deprecation warning and nothing else. The board's stock devicetree already carries a `storage_partition` at `0xf8000`/32 KB (`nordic/nrf52840_partition.dtsi`, included by `nrf52840dk_nrf52840.dts`) — exactly `remote/BUILD_SPEC.md` §9's address, needing no board overlay |
| 2026-08-12 | — | — | `as.exe` block, diagnosed | **resolved — transient, not code** | The row above's `Permission denied` was chased down rather than left as a standing gap: `Get-MpThreatDetection` and file ACLs were inconclusive without admin; AppLocker's event log showed policy-refresh (8001) and unrelated installer (8043) entries only, no denial for `as.exe`; running `as.exe --version` elevated succeeded outright, with the file's ACL already `Full Control` for the account, ruling out a permissions or policy explanation. Re-tested some hours later (machine had slept in between) with **no environment change made** and it now runs unelevated too. Best explanation: Windows Defender's cloud-delivered protection puts a transient hold on a freshly-installed, unfamiliar binary pending a reputation verdict, surfacing as `Permission denied` rather than a labelled block, and clearing on its own once the verdict lands. Not an AV exclusion, not an AppLocker rule, not a file-permission fix — nothing was changed to unblock it |
| 2026-08-12 | — | — | **W0** | **pass** | `dongle/tests/rframe`: `make check`, 20 checks, 67 assertions, 0 failures — A1–A7, A11, both encode round-trips and the duplicate-window boundary all pass for real, superseding the Python cross-check above. `dongle/tests/protocol`'s 131 checks re-run alongside as a control, unaffected, confirming the fix wasn't specific to one binary |

### 9.3 Project history

| Date | Event |
|---|---|
| 2026-07-31 | Repository created |
| 2026-08-06 | Protocol v2.0 (newline-delimited ASCII; superseded v1.0's binary framing — `PROTOCOL.md` §16) |
| 2026-08-07 | **M0**: dongle USB firmware 0.1.0 working on hardware at v2.0; V1–V3 passed |
| 2026-08-09 | `SCOPE.md` v1.1 and `SYSTEM_FUNC_SPEC.md` v2.1 adopted as authoritative; `PROTOCOL.md` v3.0 written against them (breaking) |
| 2026-08-09 | **M1**: scoreboard application at v3.0 — 113 tests, lint and build clean, browser-verified |
| 2026-08-10 | `RADIO_PROTOCOL.md` v1.0 written against `SCOPE.md`, `SYSTEM_FUNC_SPEC.md` and `PROTOCOL.md` §12. Bluetooth LE as bearer, SCI with a fallback ladder, a device-local counter for gap visibility with no application-level retry, and a provisioned set key with no pairing procedure ever. Settles the M3 architecture; measures nothing — §3.4, §4.8 |
| 2026-08-10 | **Embedded roadmap restructured.** The single M3 "radio layer and remote firmware" was carrying seven phases' worth of work; it is now M3 (radio 1:1) → M4 (2:1) → M5 (full-feature DK remote) → M6 (PCB) → M7 (port and validate) → M8 (custom dongle, optional), with validation and hardening moved to M9 and run alongside. Added the radio validation ladder W0–W8 (§5.3), which did not exist. **The old M4 and M5 numbers are now M9** — a reference to "M4 — validation" predates this change. Decisions §4.9 (2:1 test topology) and §4.10 (provisioning at M3) recorded — §3.3 |
| 2026-08-09 | Dongle emulator built (`wrsl-app/src/emulator/`): the dongle half of v3.0 over real Web Serial, with interactive mockups of both remotes. Settles how the interface is validated before firmware — §3.2. Suite now 137 tests. Verified end to end over a virtual serial pair: handshake, indicator assertion per remote, acknowledgement routing, and the gesture axis |
| 2026-08-10 | **Build specs written and the firmware programme restructured.** `dongle/BUILD_SPEC.md` and `remote/BUILD_SPEC.md` are new and are the implementable contracts (§4.12). **M2 and M3 are merged into one programme of six stages** (§3.1): the no-radio USB baseline is now a permanently retained build configuration, `CONFIG_DONGLE_RADIO=n`, rather than a milestone gate crossed once — same attribution, re-runnable against the firmware in hand. **M3's number is retired rather than reused and M4–M9 keep theirs**, because the last renumber left stale references. V0–V8 and W0–W8 keep their names and become per-stage exit criteria (§5). V7 and V8-as-a-gate deferred with reasons |
| 2026-08-10 | **Radio timing revised: baseline BLE at 7.5 ms, SCI deferred** (§4.8, `RADIO_PROTOCOL.md` §12.2). The interval buys retransmission headroom rather than latency, and whether two retries suffice depends on the retransmission rate at 12 m through a torso — which is unmeasured, so the baseline is the rung needing no special controller feature. Rungs 1 and 2 stay specified as a contingency reopened only by a measurement on shipping hardware. Consequence recorded: the interval feeds battery sizing, so size with headroom |
| 2026-08-12 | **M2 stage 2 code-complete: the provisioning record, reader, refusal path and bench tool all exist.** `common/provisioning.h/.c` — the record struct, the pinned CRC32 variant and role values, parse/validate — is Zephyr-free like `protocol.c` and host-tested at 54 checks (`dongle/tests/provisioning`). `dongle/src/provisioning_flash.c` reads `storage_partition` via `flash_area_open(FIXED_PARTITION_ID(storage_partition), …)` and additionally requires `role == DONGLE`. `engine_start()` loads it before the first `HELLO`; on failure it logs the specific reason and emits `ERR NO_PROVISIONING`, and `HELLO`'s `<set>` field falls back to a fixed `RR-0000` rather than the retired `CONFIG_DONGLE_SET_SERIAL_FALLBACK` Kconfig option. `dongle/tools/provision.py` generates a set's three records as Intel HEX plus a manifest, with no third-party dependencies; its output was decoded independently and fed back through the parser, byte-for-byte identical. No-radio build clean, 55,996 B flash. **Not yet flashed to a real board** — A19 on hardware is the outstanding rung, and the remote-side half of stage 2's acceptance criterion waits on stage 4 |
| 2026-08-12 | **M2 stage 2 closed: A19 confirmed on real hardware, dongle side.** `INFO` reports the `RR-0000` fallback unprovisioned and the exact provisioned serial (`RR-0001`) once a `provision.py` record is written to `storage_partition`. Getting there found that the flashing story in `BUILD_SPEC.md` §9 was incomplete: nRF Connect Programmer's GUI cannot write the provisioning hex alongside the application hex (times out merging them into one ~900 KB transfer) and its DFU packaging has no "no SoftDevice" option; the working path is the `nrfutil` CLI with `--sd-req=0x00`, which needed its `nrf5sdk-tools` plugin installed as a one-time step. `storage_partition` survives an application reflash exactly as designed, confirmed by reflashing the app afterward and finding the provisioned serial still in place |
| 2026-08-11 | **M2 stages 0–1 code-complete: the dongle wire layer reaches v3.0, and V0 runs for the first time.** MinGW-w64 GCC 16.1.0 installed and recorded as `dongle/tools/hostenv.sh`; the host suite rewritten to v3.0 *before* the parser and green at 131 checks. Firmware 0.2.0: buttons and gestures, `ACK`/`SILENT` at a 120 ms window with an active sweep, `STATE`/`CFG`/`HAP` relay with no dongle-side cache, `JOIN`, supervision at 2.5 s driving `DN_HOST` down, `TEST 0–4`, a transmit drop counter, and a dedicated cooperative workqueue. The dongle-side clock and heartbeat are deleted. `src/radio.h` with `radio_null.c` behind `CONFIG_DONGLE_RADIO=n` replaces `CONFIG_DONGLE_FAKE_LINK`, which is gone. **Built, not flashed** — V1–V6 outstanding, and A15 is not verifiable in this configuration — §2.6 |
| 2026-08-11 | **The dongle is flashed to 0.2.0 and the wire layer answers on hardware.** §2.7 |
| 2026-08-11 | **The dongle's indicator hardware settled by measurement, and per-remote haptic routing proven with it** — [`dongle/BOARD.md`](dongle/BOARD.md), new, every fact cited to an in-tree board file. **One fitted blue lamp, on P0.06**, transposed from Raytac's own pin table. Two earlier answers — "two lamps", then "one bi-colour package" — were both derived from the `led0-green`/`led1-red` aliases, which are **copied from the Nordic nRF52840 Dongle and describe a part this board does not have**; the general lesson is that alias names are not hardware evidence. `HAP RED LONG` dark and `HAP GREEN LONG` blinking is a **positive proof of routing** that two working LEDs could not have given: a firmware ignoring the target would light the same lamp for both. No firmware change; comments in `indicator.c/.h`, `dongle/README.md`, `dongle/BUILD_SPEC.md` and §2.7 corrected. Recorded as consequences: fault indication is **invisible** on this board and stays that way (§2.2), and `CFG` and anything addressed to `RED` are unobservable here and move to stage 4 |
| 2026-08-11 | V1 and V3 green on the half a terminal can reach; supervision measured at 2512 ms; `TEST 4` emits the 32 events the `BUILD_ASSERT` could only assert, discharging the first of §2.6's two obligations. The 120 ms acknowledgement budget shows all three bands, including the withheld tap of §4.5 that no LED can report. **The emulator wire-log diff is clean** — 39 of 39 `EVT` lines identical, and the `ECHO` space collapse it surfaced had been predicted in writing beforehand, which is what makes the method worth trusting. **The application has still never held the port** — §2.7 |
| 2026-08-11 | **The application held the port for the first time, and V2–V3 close.** Reached `ready` twice with `Set serial RR-0000`/`Dongle firmware 0.2.0`/`Protocol 3.0`, each from an independent connection. `TEST 1` and `TEST 4` were driven through the app's own raw-command console (`DetailPanel.jsx`'s System tab, built for exactly this — the exclusive COM port means a bench terminal can't be up alongside the app) rather than a bench count: 39 events total, zero sequence gaps, zero duplicates, correct `SILENT` on the ruleset's inert button, correct `HOLD_REP` restriction, and the `F2` pending-choice assign/clear cycle confirmed live on the wire. The same console closed V1's outstanding `STATE` render. **One thing was tried and abandoned as inefficient**: capturing the literal handshake line ordering, which the wire-log UI's 200-line cap rolls past in about 100 seconds — dropped in favour of the identity-field evidence already in hand rather than costing a second native-picker interruption to chase it |
| 2026-08-11 | **V4 closes, six rows of seven.** Driven from the scoreboard UI under NCAA Folkstyle with P1 shortened to 8 s: beat-per-second on the riding-time owner only, ownership transfer moving the beat within one cycle (`STATE` for both remotes pushed together), stop halting beats with no `STATE` line, deassign producing exactly one `STATE … OFF`, and the main-clock warning/period-end waveforms each firing exactly once — reproduced on two independent short periods rather than taken on a single run. The seventh row, beat suppression under a burst of `ADD_POINT` presses, turned out to be structurally unreachable from the operator UI: an on-screen click dispatches the same `INPUT` action a remote press would (§4.2) but generates no wire `EVT`, so there is nothing for the suppression window to act on. Left to the existing unit coverage rather than chased with `TEST`-mode workarounds |
| 2026-08-10 | **Three specification corrections and two confirmations, from verifying assertions against the installed tree** — §3.12. `PROTOCOL.md` §10.2 `TEST 4` corrected from 21 events per remote to 16; `RADIO_PROTOCOL.md` §12.3's SCI call sequence corrected against the v3.4.0 headers, where it would have failed as a rejected HCI command rather than a build error; `PLAN.md` §3.4's DK haptic proxy moved to the one LED the stock devicetree actually PWMs. Confirmed: `bt_nrf_conn_set_ltk()` exists, and both boards carry a usable `storage_partition`. Also established that **this machine has no host C compiler at all**, which is why V0 has never run |
| 2026-08-12 | **M2 stages 3 and 4 code-complete, both build clean, neither touched hardware.** `common/rframe.c/.h` — the RP v1.0 frame codec and CTR arithmetic, both directions — was written and host-tested first, per stage 0's carried-forward obligation, mirroring `protocol.c`'s discipline; `dongle/tests/rframe` covers A1–A7 and A11 at 20 checks, written and syntax-clean but not yet *run* — the host `as.exe` returns `Permission denied` in this environment, independent of this project's code (`gcc.exe` itself works; a copy of `as.exe` in a clean directory fails identically), and was instead cross-checked against a Python reimplementation that caught one real fixture bug (§9.2). `dongle/src/radio_ble.c` then implements the connection lifecycle of `dongle/BUILD_SPEC.md` §7.1 in full — LTK install, security, RR_IDENTITY validation (A13/A14), CCCD subscribe, debounced `LINK` (A15–A17), CTR gap/duplicate accounting fed back to two new `radio.h` callbacks (`on_gap`/`on_dup`) that `engine.c` was already staging counters for. The whole DK remote firmware — `remote/src/{link,buttons,haptic,indicators,prov_flash,main}.c` — was written alongside it rather than after, since stage 3's central needs a peripheral to test against and `remote/BUILD_SPEC.md` already scopes the DK as the minimal peripheral stage 3 needs, not a separate thing to invent. **A gap knowingly left in both**: GATT and `bt_conn` callbacks call directly into application state from whatever thread Zephyr's BT host dispatches them on, not marshalled onto the single cooperative workqueue `engine.h`'s architecture otherwise guarantees — flagged in-code at both call sites, low-frequency (connection lifecycle, not the press/haptic path), and listed as the thing to fix before soak testing (stage 5) rather than silently shipped or silently forgotten. Next action: flash both units, provision one set across them, and open W1 |
| 2026-08-12 | **W0 closed — the `as.exe` block was diagnosed and turned out to be transient, not a defect in this project.** Chased with AppLocker's event log (policy refreshes and unrelated installer entries only, no denial recorded for `as.exe`) and an elevated re-run (succeeded, with the file's own ACL already `Full Control` — ruling out a permissions fix). Retested unelevated some hours later with nothing changed in between, and it now runs cleanly: the working theory is a transient Windows Defender cloud-reputation hold on a freshly-installed binary, which clears itself once a verdict lands, rather than anything requiring a Defender exclusion or an AppLocker rule. `dongle/tests/rframe`'s `make check` then ran for real — 20 checks, 67 assertions, 0 failures — superseding the Python cross-check as the authoritative result, with `dongle/tests/protocol`'s 131 checks re-run alongside unaffected. M2 stage 0 is now closed in full; stage 3's remaining gate is hardware (W1) |
