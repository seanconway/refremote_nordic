# RefRemote — Plan, Status and Validation

**Status as of 2026-08-10.** `SCOPE.md` and `SYSTEM_FUNC_SPEC.md` are the authority on direction; `PROTOCOL.md` **v3.0** is the revision that answers to them, and it is a breaking change against the v2.0 prototype. The scoreboard application is at v3.0 (M1, complete); the dongle firmware is still at v2.0 (M2, next); the radio layer is **specified** — `RADIO_PROTOCOL.md` **v1.0** — but not implemented, and the remotes do not exist.

**This is a living document.** It carries the current state of the project (§1), the record of completed work and what it settled (§2), the planned work in order (§3), the decisions that still bind (§4), the full validation ladder (§5–§6), the unmeasured risks (§7), known gaps (§8), and the results log and version history (§9). The validation plan previously lived in `dongle/VALIDATION.md` and has been rolled in here (§5–§6), because a status document that points at a separate test plan gets read as a status document.

| For | See |
|---|---|
| What the system is and why | [`SCOPE.md`](SCOPE.md) |
| How it behaves | [`SYSTEM_FUNC_SPEC.md`](SYSTEM_FUNC_SPEC.md) |
| The wire protocol | [`PROTOCOL.md`](PROTOCOL.md) |
| The radio protocol | [`RADIO_PROTOCOL.md`](RADIO_PROTOCOL.md) |
| Build, flash, manual test | [`dongle/README.md`](dongle/README.md) |

---

## 1. Current status

| # | Milestone | State |
|---|---|---|
| **M0** | USB link at v2.0, working on hardware | ✅ done — §2.1 |
| **M1** | Scoreboard application to v3.0, the functional specification, and the design system | ✅ done — §2.3 |
| **M2** | **Dongle USB firmware to v3.0** — no radio | ▶ next — §3.1 |
| **M3** | Radio layer brought up **1:1** — dongle central, one DK standing in as a remote | protocol specified, firmware not started — §3.4 |
| **M4** | The **2:1** link — two peripherals, two connections, one central | §3.5 |
| **M5** | Full-feature remote firmware on the DK — GPIO buttons, RGB indicators, ERM, nPM1300 | §3.6 |
| **M6** | Custom remote PCB designed | §3.7 |
| **M7** | Firmware ported to the custom remotes; system validated on production-shaped hardware | §3.8 |
| **M8** | Custom dongle, for BOM cost — **optional** | §3.9 |
| **M9** | Deployment validation and MVP hardening — USB identity, ruleset verification, §6 and §8 | §3.11 |

**M2 through M8 are the embedded programme**, and §3.3 maps them onto the seven development phases in one table. M9 is scoreboard-and-product work that runs alongside from M5 onward rather than after M8.

| Component | State |
|---|---|
| `SCOPE.md`, `SYSTEM_FUNC_SPEC.md` | Current. Authoritative. |
| `PROTOCOL.md` v3.0 | Written against the specification. Implemented on the app side only. Byte-identical in both repos. |
| `RADIO_PROTOCOL.md` v1.0 | Written against the specification and against `PROTOCOL.md` §12. **Implemented nowhere.** Every latency figure in it is a prediction awaiting measurement. This repo only. |
| Scoreboard app | **v3.0, M1 complete.** 113 tests passing across protocol, reducer and service suites. Lint and production build clean. |
| Dongle USB firmware | **Still v2.0**: framing, parser, clock, heartbeat, supervision, `CONFIRM`, TEST modes. 52 KB flash, 19 KB RAM. Milestone M2 brings it to v3.0. |
| Dongle radio | **Not started.** `CONFIG_DONGLE_FAKE_LINK` fabricates link state; LEDs stand in for haptics. |
| Remote firmware | **Not started.** Custom hardware not designed. The nRF52840 DK is the prototyping platform from M3 (§3.4). |
| Provisioning | **Not started, and it is a prerequisite of M3, not of manufacture.** `RADIO_PROTOCOL.md` §10.1 specifies the record; nothing writes or reads one, and no set can connect without it — §3.4. |
| Host parser tests | Written, **never executed** — no C compiler on the dev machine. |
| Radio conformance tests | `RADIO_PROTOCOL.md` §14 A1–A20 exist as a specification only. **No harness.** The radio's counterpart to V0 — §5, rung W0. |
| Validation | V1–V3 passed against v2.0 and are **void** for v3.0. V0 and V4–V8 never run, and all hardware rungs are blocked on M2. |

**The two ends are deliberately out of step right now.** The app speaks v3.0 and refuses a v2.0 `HELLO` at the major-version guard, so plugging in today's dongle produces "dongle firmware is incompatible" — the correct behaviour, and the V7 guard working, but not a usable link. Nothing in the app is blocked by this: `FakeDongleTransport` exercises the full protocol surface without hardware.

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

---

## 3. Planned work

M1 was done before M2 deliberately: the application is the node that holds every requirement the specification added, and it could be built and fully tested against `FakeDongleTransport` with no hardware at all. Bringing the firmware up first would have meant guessing at the shape of the traffic the application actually produces. That guessing is now over — M1 fixed the exact traffic, and M2 is concrete.

### 3.1 M2 — dongle USB firmware to v3.0 — ▶ next

Straightforward against a finished application: the message set changes, the clock and heartbeat timer are deleted, `ACK` routing replaces `CONFIRM` routing at a 120 ms window, `STATE` and `CFG` are relayed to the radio seam, `JOIN` is emitted from it, and `TEST 4` is added. The framing, transport, supervision skeleton and build path are unchanged.

| # | Change | Note |
|---|---|---|
| 1 | `HELLO` reports `3.0` | Until this lands, the app's major-version guard refuses the link — correct, and it means M2 is all-or-nothing rather than incremental |
| 2 | `EVT <button> <gesture> <src> <seq>` | Four fields. Delete `TIME_UP`/`PERIOD_UP`/`CLOCK`/`EXPIRE`; add the three gestures |
| 3 | **Delete the clock and the heartbeat timer** | This is a deletion, not a port. The dongle holds no match state at v3.0 — the beat arrives as `HAP <target> BEAT` from the app |
| 4 | `ACK <seq> [SILENT]` replaces `CONFIRM` | Routed to the originating remote by the `src` recorded against that `seq`, never broadcast (§10.4) |
| 5 | `STATE` and `CFG` accepted and relayed | Idempotent full assertion; at M2 they land on the LED stand-in |
| 6 | `JOIN <remote>` emitted from the radio seam | Does not exist yet; the app answers it with a forced `STATE` |
| 7 | `seq` widened to 0–65535 | 16-bit wrap. Hold-repeat at 150 ms wraps a 1000-entry space in 2.5 minutes |
| 8 | `PING` 1 s / supervision 2.5 s | Tightened from 2 s / 5 s |
| 9 | `TEST 4` added | 21 events per remote, every gesture on every button |

The pending table shrinks in lifetime and grows in importance. The transmit ring needs a drop counter before M3, not after.

**Do V0 first.** The host parser tests are the cheapest possible check on a message-set rewrite, they need no hardware, and they cover the two cases that must fail closed (T7, the v2.0-shaped gestureless `EVT`; T16, the duplicate `seq`). Rewriting the parser without running them means trusting a rewritten parser on inspection alone — which is how the current one is trusted, and that was already the highest-value outstanding item before M2 added to it.

### 3.2 Validating the app ↔ dongle interface without remotes — ▶ alongside M2

The remotes do not exist and will not for some time, so the question of how far the USB interface can be validated without them had to be answered deliberately rather than by default. **The answer is the dongle emulator, built 2026-08-09** — the application is validated against an executable copy of the protocol before firmware exists, so a failure after M2 localises to the firmware rather than being ambiguous across the whole pipeline.

An emulator on the *radio* side was considered and rejected: a laptop's own Bluetooth stack cannot hold the peripheral role with the connection parameters this design needs (SCI, LLPM — §3.4), so its timing would describe the laptop rather than the product. Radio validation needs Nordic silicon at both ends and belongs to M3, where the nRF52840 DK is already the remote-prototyping platform and most of that firmware is the remote firmware.

The pieces:

**What already exists to build on:**

- **V0**, the host parser tests — no hardware at all, and the highest-value single item (§5, rung V0).
- **The dongle `TEST` modes** (`PROTOCOL.md` §10.2) — deterministic `EVT` stimulus standing in for remote presses: `TEST 1` for one press per button, `TEST 4` for every gesture on every button, `TEST 2` for randomised soak load. These are what let V3 and V8 run with no remotes.
- **`CONFIG_DONGLE_FAKE_LINK`** — synthetic `LINK`/RSSI/battery so the app's indicators can be exercised. A stand-in, and a standing trap when left on (§5.2).
- **The LED stand-in** (`indicator.c`) — two LEDs representing two remotes' worth of haptics and indicators, which is enough to see *that* a command arrived and nothing about *where* it was routed or *how it feels*.
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

**What no software instrument covers:** acknowledgement routing to a physical *wrist* (B6), haptic amplitude and perceptibility (R3, R4), radio latency and the full 120 ms budget (B7, R2's radio share), and link behaviour at range (B4, B5). These wait for M3 hardware, and any bench result that appears to speak to them is validating a stand-in.

**Still to decide while M2 is in progress:**

1. **Where V0 runs, permanently.** A one-off run on a borrowed machine proves the parser once; the parser is about to be rewritten and will be touched again at M3. Decide whether V0 becomes a CI job (the tests are plain C with a makefile — any Linux runner works) or a documented WSL/MSYS2 step on the dev machine, so it cannot silently return to "never executed".
2. **What the M2 pass bar is.** Proposed: V0 green, V1–V8 pass at v3.0 with the results logged in §9.2, each rung's wire log diffed against the emulator's for the same scenario, and R1 and the app share of R2 measured. That closes every rung that does not require a radio, and leaves §3.10 as the reentry checklist when one exists.

### 3.3 The embedded roadmap in one view

M2 through M8 are one programme with one shape: **each milestone adds exactly one new thing that can be wrong.** That is the whole reason for the ordering, and it is worth stating before the detail, because the tempting shortcuts all consist of adding two.

| Phase | Milestone | What is new, and therefore what a failure means | Hardware |
|---|---|---|---|
| 1a | **M2** | The v3.0 message set. No radio anywhere in the system | Product dongle + host |
| 1b | **M3** | The radio, one connection. A failure is radio or remote — never the message set, which M2 fixed | + nRF52840 DK as a remote |
| 2 | **M3** | The remote *end* of the system: real presses, real indicator rendering, a real end-to-end loop | (same) |
| 3 | **M4** | The **second** connection. A failure is central scheduling, routing or skew — nothing else changed | + nRF52840 dongle as remote #2 (§4.9) |
| 4 | **M5** | The remote's real peripherals — seven buttons, RGB, an ERM, a PMIC. A failure is hardware or drivers, not protocol | + GPIO harness, ERM, nPM1300-EK |
| 5 | **M6** | Nothing runs. This is schematic, layout, BOM and enclosure, and its **inputs are M4 and M5 measurements** | — |
| 6 | **M7** | The custom board. A failure is the port or the board, because the firmware above it is the firmware M5 validated | Custom remote PCBs |
| 7 | **M8** | The custom dongle. Optional, cost-driven, and deliberately last | Custom dongle |

**Phase 1 as originally framed spans two milestones, and splitting it is the single most important structural point in this section.** "Dongle firmware that bridges the radio and wire protocols" is one sentence and two milestones' worth of risk: M2 changes the message set with no radio present, M3 adds the radio to a message set already proven. Bringing them up together forfeits the thing that makes M2 cheap — the emulator reference trace of §3.2, which localises any v3.0 wire defect to the firmware — and it forfeits it exactly when §3.10's list of ways the radio silently degrades the USB link becomes live. **A no-radio USB baseline that has passed V1–V8 is what makes every later radio regression attributable.** Do not merge them.

**Two ordering constraints run backwards through this table**, and both are easy to miss because they look like late-phase concerns:

- **M6 cannot start before R6 has a number.** Battery capacity is an enclosure and PCB decision, and the connection-interval rung chosen at M4 (`RADIO_PROTOCOL.md` §12.2) is the dominant input to radio current. Designing the board against a predicted rung means respinning it when the measurement disagrees. §3.6 makes the measurement an M5 exit criterion for this reason.
- **M6 cannot start before R3 and R4 have an answer.** `SCOPE.md` §9.2 lists a dual-haptic hardware revision as contingent on the single-ERM assumption failing in prototyping — that contingency has to resolve while it is still a firmware-and-breadboard question, because after M6 it is a respin.

### 3.4 M3 — the radio layer, brought up 1:1

**The protocol is now designed: [`RADIO_PROTOCOL.md`](RADIO_PROTOCOL.md) v1.0, written 2026-08-10.** What follows is the scope of M3, the requirement the protocol answers to, the four architectural decisions it commits the project to, the seams in the code it attaches at, and the questions it deliberately leaves open.

#### Scope: one connection, both ends, end to end

M3 brings up **one** radio connection — dongle central, one nRF52840 DK standing in as a remote — and closes the loop from a physical button to the scoreboard and back to a physical indicator. The second connection is deliberately held back to M4 (§3.5), because central scheduling of two links is a distinct failure domain and mixing it into first light makes every symptom ambiguous.

| # | Deliverable | Note |
|---|---|---|
| 1 | **Provisioning record and its reader** | `RADIO_PROTOCOL.md` §10.1. Nothing connects without it — this is the first thing built, not the last |
| 2 | **Bench provisioning tool** | Generates a partition hex from a serial, role, address pair and key. An unlisted deliverable until now — see below |
| 3 | Dongle BLE central: fixed-address initiator, LTK from the set key, no pairing | §10.2, §10.3. `bt_nrf_conn_set_ltk()` |
| 4 | RefRemote Link Service on the remote: `RR_IDENTITY`, `RR_UPLINK`, `RR_DOWNLINK` | §3 |
| 5 | Frame codec, both ends, **written without Zephyr dependencies** | So rung W0 can run on a host, exactly as `protocol.c` does. This is a constraint on how it is written, and it is free only if decided now |
| 6 | `CTR` accounting, gap and duplicate detection, `ctr_base` re-baselining | §6, §7.2 |
| 7 | Dongle radio seams filled: `send_evt()` fed from `UP_INPUT`, `ACK` routed by `src`, `STATE`/`CFG` relayed, `JOIN` from `UP_READY` | The seam table below |
| 8 | Deadline enforcement mechanisms 1 and 2 | §8.3. Mechanism 3 is optional and experimental; the guarantee must not rest on it |
| 9 | `DN_HOST`, and `LED_LINK` as the conjunction | §9.2. The case most likely to be missed (A15) |
| 10 | Real `LINK` state, debounced, with averaged RSSI — and `CONFIG_DONGLE_FAKE_LINK=n` | §9.3, §9.4. Retiring the fake is part of the milestone |
| 11 | **DK remote firmware**, reduced surface | Below |

**The provisioning tool is real work that no document had claimed.** `RADIO_PROTOCOL.md` §10.1 says the record is "written once at manufacture", which is true of the product and unhelpful at M3, where three units need records today and will need them again after every erase. Build the **record format, the CRC check, and the refuse-to-operate-unprovisioned path (A19) for real now** — that is a boot path, and boot paths added late are boot paths that were never exercised — but generate the records with a script rather than a process. A `#define`-ed key compiled into the firmware is the tempting shortcut and it is a bad one: it makes A12, A13 and A19 untestable, and those are three of the four cases standing between this product and a cross-associated match.

#### The DK as a remote: what four buttons and four LEDs can and cannot reach

The nRF52840 DK offers **four buttons and four green LEDs**, one of which is also a PWM channel. The remote specifies **seven buttons and four RGB indicators** (FS §3.1, §3.2) plus an ERM. The gap is not a detail to be worked around silently; it determines what M3 can claim.

Map the four buttons for **gesture and semantic coverage**, not for button coverage — three gestures and the inert/no-op distinction are the things that can be wrong, and each of the seven buttons is the same code path:

| DK | Button | Reaches |
|---|---|---|
| Button 1 | `ADD_POINT` | `PRESS`. The scoring path, and the one repeated-press scoring rests on |
| Button 2 | `TOGGLE_CLOCK` | `PRESS` and `HOLD` — the hold threshold at 600 ms |
| Button 3 | `FORWARD` | `HOLD_REP` at 150 ms, the only button class that repeats |
| Button 4 | `F1` | **Inert vs no-op** (§2.3) — `ACK … SILENT` must put *nothing at all* on the air (A20) |

That covers all three gestures, the `HOLD_REP` restriction, and the distinction most likely to be flattened. The remaining three buttons (`REMOVE_POINT`, `BACKWARD`, `F2`) wait for M5, where they arrive as real GPIO.

**A shift or bank modifier to reach all seven from four buttons is rejected.** It is the obvious solution and it is a second input path — the same objection §2.3 makes to an operator shortcut in the app, one layer down. It would be firmware the product does not have, exercising timing the product does not have, and it would be the only thing under test that ships nowhere.

The indicators are the tighter constraint, because five things want four mono LEDs:

| DK | Renders | Fidelity |
|---|---|---|
| LED 1 | `LED_F1` from `DN_INDICATOR` | Mode only — `OFF`/`SOLID`. **Colour is not rendered** |
| LED 2 | `LED_F2` from `DN_INDICATOR` | Mode only |
| LED 3 | `LED_LINK`, as the conjunction of radio-up and `DN_HOST` | Full behaviour. This is A15 and it is fully testable here |
| LED 4 (PWM) | Haptic activity, brightness standing in for amplitude | **A proxy, and not a weak one — a false one** |

**Three things M3 therefore cannot claim, and must not be read as claiming:**

1. **Indicator colour.** `DN_INDICATOR` carries RGB per indicator; mono LEDs show mode only. Verify the colour fields by reading them out over RTT, and treat the rendering itself as unvalidated until M5.
2. **`LED_PWR`.** The DK is bus-powered and has no battery, so there is nothing true for it to show. `UP_TELEMETRY.battery_pct` is synthetic until M5, which also means the app's battery indicator is being fed a constant — the same trap `CONFIG_DONGLE_FAKE_LINK` sets, wearing different clothes.
3. **Anything haptic.** LED brightness is not amplitude. R3 and R4 — whether one ERM covers the whole range, and whether `BEAT` is reliably distinguishable from `TAP` on a wrist — are **untouched by M3 and M4** and are the substance of M5.

#### Decide before starting, not during: how diagnostics get out

`PLAN.md` has carried this as an open question; at M3 it becomes blocking, and the answer differs by board.

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
| **Timing** | SCI, target 2.5 ms, with a measured fallback ladder to 5 / 7.5 / 10 ms. LE 2M PHY, 27-byte payloads, peripheral latency and subrating both pinned off | The interval is the dominant term in the 25 ms one-way allocation: at 10 ms one retransmission spends the budget, at 2.5 ms eight fit. LLPM rejected as primary — Nordic's own multi-connection guidance moves it to 10 ms. §12 |
| **Exactly-once** | Device-local 8-bit counter per remote for gap visibility and replay rejection. **No application-level retry**, in either direction | The Link Layer already delivers exactly-once on an intact connection; the counter's real work is making a loss it could not prevent into a number somebody can read. A retry above the link layer fires only when the press is already worthless, and produces the late tap `PROTOCOL.md` §11 forbids. §6 |
| **Set binding** | Provisioned set key installed as the LTK via `bt_nrf_conn_set_ltk()`; fixed static-random identity addresses; **no pairing procedure is ever performed**, at manufacture or in the field | Manufacture-time bonding stores the binding where a DFU or settings migration can silently clear it, and a set that has forgotten its binding presents at an event as two remotes that will not connect. First-boot auto-bonding opens the pairing window FS §2.3 exists to close. §10 |

**The decisions are settled; the numbers are not.** Every latency figure in `RADIO_PROTOCOL.md` §12 is arithmetic from documentation, and the ones that matter — latency at 12 m through a torso, with 29 other systems in the hall — cannot be obtained from documentation. They are stated so the measurement has a prediction to falsify.

**One specification nuance was resolved in the writing and is worth flagging.** FS §2.1 lists link status among the things a remote detects locally; FS §10.2 defines the indication as *connected end to end*. Those reconcile only if the remote is told about the half of the path it cannot see — the USB cable, the browser tab, the laptop's sleep state, the application watchdog of FS §8.3, every one of which leaves the radio connection perfectly healthy while the scoreboard is gone. `PROTOCOL.md` §8 already requires the dongle to *"instruct both remotes to render link-lost"* in prose; `RADIO_PROTOCOL.md` §9.2 gives that requirement a frame (`DN_HOST`) and states the rule: the remote renders `LED_LINK` from the **conjunction** of radio-up and host-up. It still detects its own link state; the state simply has two inputs. This is a refinement of FS §2.1, not a contradiction of it, but it is the kind of thing that should be noticed rather than absorbed.

#### Where the dongle sits is part of the link budget

The Raytac MDBT50Q-CX-40 carries an MDBT50Q-P1M module with a PCB trace antenna, and the nRF52840 will do up to +8 dBm. The dongle then sits in a USB port on a laptop at the scoreboard table: close to the host's own 2.4 GHz radios, often below table height, frequently with bodies between it and the mat.

The link budget must be taken **at the dongle as deployed**, not on a bench with clear line of sight. If it does not close, the available remedies are a USB extension cable to raise and separate the dongle, higher transmit power, or a dongle placement constraint in the deployment documentation — in that order of preference.

#### Seams that exist in the code today

Each is currently satisfied by a stand-in, and each is where M3 attaches:

| Seam | Location | Currently |
|---|---|---|
| Link state source | `engine.c` — `links[]`, populated under `#ifdef CONFIG_DONGLE_FAKE_LINK` | Synthetic `CONNECTED` with fixed RSSI and battery |
| Event origination | `engine.c` — `send_evt()` | Called only from `test_handler()` |
| Acknowledgement delivery | `engine.c` — pending table | Pulses an LED, not routed by `src` |
| Indicator assertion | *does not exist* | New at M2 |
| Haptic delivery | `engine.c`, `indicator.c` | LED pulses |
| Remote join detection | *does not exist* | New at M3; drives `JOIN` |

`send_evt()` already allocates and wraps the sequence number and registers confirmable actions in the pending table, so a press arriving from a remote needs to reach *that function* rather than reimplement around it.

#### Questions the protocol answers

Listed because they were open in the previous revision of this section and are not any more. Each is a design commitment now, and changing one is a change to `RADIO_PROTOCOL.md`, not a tuning decision.

| Question | Answer | Where |
|---|---|---|
| How is a physical remote bound to the `RED` / `GREEN` identity, and how is the set serial provisioned so `HELLO` can report it? | A CRC-checked provisioning record in a dedicated flash partition, written once at manufacture: serial, role, own and peer identity addresses, set key. An unprovisioned unit does not advertise and does not initiate | §10.1 |
| How is `LINK` state derived and debounced so a remote at the edge of range does not flood the USB link? | `CONNECTED` reported only after encryption, identity validation and subscription; `DISCONNECTED` after 2 s; `CONNECTING` emitted immediately so the app has something true to show meanwhile. The remote's *own* indication is not debounced | §9.4 |
| Where do presses go that arrive while the app is disconnected? | Dropped, counted, and reported in the first `LOG` line after the app returns. Never queued — a queued press applied minutes later is a wrong score with no visible cause | §6.5 |
| How does the remote know the difference between "radio up" and "scoreboard reachable"? | It is told, by `DN_HOST`, and renders the conjunction | §9.2 |

#### Questions M3 answers

- Does radio work share the system workqueue (§4.6), or does the engine need its own? What jitter does the 1 Hz heartbeat tolerate, and what does the 120 ms acknowledgement budget tolerate? **Expect to need a dedicated cooperative workqueue for the engine, and measure rather than assume** (§4.6).
- What is the one-way latency on **one** connection, on a bench, at the chosen rung — the floor that everything later is measured against?
- Is LE Flushable ACL Data usable in v3.4.0, where it is marked experimental? The deadline guarantee does not rest on it (§8.3 mechanisms 1 and 2 must hold without it), but it is the natural mechanism if it works.
- Does the app tolerate a real `LINK … CONNECTING` state, emitted for the first time by real hardware rather than by the emulator (`RADIO_PROTOCOL.md` §13.1)?

#### Questions M3 deliberately leaves to M4

Listed separately because each needs the second connection to mean anything, and answering them on one connection produces a number that looks like an answer and is not:

- Is `RADIO_PROTOCOL.md` §12.2 rung 1 (2.5 ms, **two** connections) schedulable on this silicon? The arithmetic says yes with headroom and rung 0 says no; neither has been run. **This is the single measurement M4 exists for**, because the rung it settles is an input to the power budget, and through that to the PCB.
- What is the measured one-way latency at 12 m through body shadowing, at density — p99, not median?
- What is the power cost of the connection cadence the acknowledgement budget requires, against the ten-hour target — given that peripheral latency and subrating are both ruled out (§9.1) and heartbeat density is not available as a lever (FS §11.2)?

### 3.5 M4 — the 2:1 link

One central, two peripherals, two connections. Everything else is unchanged from M3, which is the point: a failure here is scheduling, routing or skew.

#### What M4 has to establish

| # | Question | Why it needs two connections |
|---|---|---|
| 1 | Which rung of `RADIO_PROTOCOL.md` §12.2 the link actually lands on | Central scheduling requires every link's timing-event to fit inside the common interval. The rung is a property of the *pair*, and §12.2 predicts rung 0 fails for exactly this reason |
| 2 | Acknowledgement routing to the correct remote (B6) | With one remote, a broadcast tap and a correctly routed tap are indistinguishable — §2.5 item 4. This is the defect the whole `src`/pending-table mechanism exists to prevent, and M3 cannot see it |
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

**Use the PCA10059 rather than the spare MDBT50Q-CX-40 for bring-up**, on a concrete difference confirmed in the board devicetree: the PCA10059 carries a green LED *and an RGB LED with all three channels on PWM*, where the MDBT50Q-CX-40 has two mono LEDs and one button (§4.2). That RGB is worth having — it is the only surface in the system before M5 that can render `DN_INDICATOR` colour at all, which is one of the three things §3.4 lists M3 as unable to claim. **Then swap in the spare MDBT50Q-CX-40 for the range and density measurements**, because those are antenna-and-module measurements and the MDBT50Q is the module the product is likely to carry. Two boards, two purposes, and the swap costs a flash.

**Would a second DK be justified?** Not for M4 — nothing in the table above needs one, and the asymmetric pair answers all six. The honest case for a second DK is narrower and later: **two *haptic-capable* remotes**, for B6 as a felt experience rather than a routing assertion, B8's beat-and-tap collision, and R4's amplitude separation. Three points against buying one for that:

- Those three are wrist-and-strap questions. The variables that decide them are motor mass, mounting compliance and enclosure coupling — none of which a bare DK on a bench has, and all of which the M6 board and enclosure do. A second DK would not settle them; it would produce a number that gets re-measured at M7 anyway.
- You have one nPM1300-EK. A second haptic-capable remote means a second PMIC evaluation board and a second ERM before it means a second DK, so the DK is not even the binding purchase.
- R4 is a *perceptual* judgement, and the single most useful instance of it is one referee wearing one well-made remote. That is an M5 experiment with one DK.

So: **no second DK for M4, and probably not for M5 either.** Revisit only if the M6 spin slips far enough that two-wrist evaluation on breadboards becomes the critical path — and if that happens, price a second nPM1300-EK and ERM in the same breath, because a DK alone would not unblock it.

### 3.6 M5 — the full-feature remote, on the DK

The DK's GPIO carries what the DK's onboard peripherals could not: seven buttons in the FS §3.1 layout, four RGB indicators, an ERM with its driver IC, and the nPM1300-EK for charging, fuel gauging and regulation.

| # | Deliverable | Answers |
|---|---|---|
| 1 | Seven GPIO buttons, FS §3.1 positions, debounce 15 ms | The three buttons M3 could not reach; tactile discrimination is an M6 concern |
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
| B3 | **`CONFIG_DONGLE_FAKE_LINK` still enabled.** Leaves the dongle reporting synthetic `CONNECTED` while real remotes are disconnected. | Set it to `n`. Confirm `INFO` reports `DISCONNECTED` with no remotes powered. |
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

---

## 4. Decisions that still bind

Established with evidence. Not to be revisited casually.

### 4.1 NCS v3.4.0, pinned

The LTS release — five years of security and critical fixes — and the **last release supporting the nRF52 Series**, which it declares feature complete. Nordic explicitly recommends it for new nRF52840 designs. Older DevZone advice recommending v3.1.x for this dongle predates that announcement and is superseded. Do not downgrade, and expect no newer SDK to support this chip.

### 4.2 Board target `raytac_mdbt50q_cx_40_dongle/nrf52840`

Present upstream in the installed tree. **Not interchangeable with `nrf52840dongle/nrf52840`** — LED and button mapping differ, and so does bootloader entry (hold the button while plugging in, rather than pressing RESET). `FLASH_LOAD_OFFSET=0x1000` is applied automatically for the nRF5 bootloader; never set it by hand, and do not enable MCUboot or sysbuild without deliberately switching bootloaders.

The board exposes **two LEDs and one button** — `led0_d1` on P0.06 and `led1_d2` on P0.08, aliased green and red respectively in the board devicetree, both active-low, both also available as PWM channels. That is the entire indicator budget for firmware standing in for remote haptics, and it is why `indicator.c` is as coarse as it is.

### 4.3 The new USB stack (`device_next`)

The board defaults it on. Reports of "old and new stack conflict" come from fighting that default with legacy-stack samples.

### 4.4 The protocol owns the CDC-ACM port exclusively

The board's common devicetree points `zephyr,console`, `zephyr,shell-uart`, `zephyr,uart-mcumgr` and both `bt-*-uart` chosen nodes at a single CDC-ACM instance. Anything left live interleaves bytes into the protocol stream: a log line over 120 bytes trips the receiver's discard-and-resync path, and a write landing mid-line corrupts that line. Both failures are silent and intermittent.

Console, shell and logging are off in `prj.conf`, and a `BUILD_ASSERT` in `usb_link.c` fails the build if a second CDC-ACM instance ever appears. Diagnostics leave as protocol `LOG` / `ERR` lines.

*This is the trap the upstream `cdc_acm` sample falls into on this board, and it cost real time during bring-up.*

### 4.5 Never gate transmission on DTR

The app opens the port via Web Serial and never calls `setSignals()`, so DTR assertion is the browser's default rather than anything the protocol guarantees. Gating on it yields a dongle that enumerates but never answers `INFO`. Boot-time `HELLO` is best-effort; the handshake is driven by the app sending `INFO`.

### 4.6 Everything runs on the system workqueue

All engine timers and the RX drain run on the system workqueue, giving exactly one producer feeding the transmit path, so no locking is needed between them.

**This is the constraint most likely to matter at M3.** Radio work on the same queue can delay the acknowledgement turnaround, and the budget that used to have 500 ms of slack now has 120 ms across six hops. Expect to need a dedicated cooperative workqueue for the engine, and measure before assuming otherwise.

### 4.7 `PROTOCOL.md` is duplicated, not linked

The two copies were previously kept in step by a filesystem hard link. That does not survive an editor writing a new file rather than modifying in place — it silently broke during the v3.0 revision, leaving the two repos on different versions with no indication. Copy explicitly and **verify the hashes match** after any change.

**`RADIO_PROTOCOL.md` is not duplicated.** It lives in this repo only, because the scoreboard never sees the radio and giving it a copy would create a second file to keep in step for no reader's benefit.

### 4.8 The radio is Bluetooth LE, with the four commitments of §3.4

Bearer, timing strategy, exactly-once mechanism and set binding are settled in `RADIO_PROTOCOL.md` v1.0 and summarised in §3.4. Three of them are worth restating here because each is a standing invitation to do the ordinary thing:

- **Do not raise the ATT MTU or enable Data Length Extension.** The 27-byte Link Layer payload is a *precondition* for the shortest connection intervals (`RADIO_PROTOCOL.md` §4.3). Raising it is a normal, sensible optimisation that would spend the latency budget to buy throughput this product does not need, and it would not fail a single test — it would lengthen the acknowledgement tail by milliseconds nobody attributes to it.
- **Do not enable peripheral latency or connection subrating.** They are the standard BLE power levers and they work by skipping connection events, which is exactly what delays an acknowledgement tap (§9.1). Subrating is enabled only because SCI mandates it, with the subrate factor held at 1.
- **Do not add an application-level retry to the press path.** Link-layer retransmission inside the connection event is the bounded effort this design wants. Anything above it fires only when the press is already worthless, and delivers the late tap `PROTOCOL.md` §11 rules out (§6.3).

Each of these is the thing a competent implementer following ordinary practice would do, and each fails silently.

### 4.9 The 2:1 link is tested with two physical peripherals, asymmetric

**One nRF52840 DK as remote #1, one spare nRF52840 dongle as remote #2.** Not one DK holding two connections, and not a second DK. The full argument is in §3.5; the decision is recorded here because the rejected option is the cheaper-looking one and will look attractive again.

The load-bearing part is the first reason rather than the practical ones: M4 exists to measure **central** scheduling of two links, and terminating both on one peripheral radio introduces a peripheral-side scheduling constraint the product does not have. A rung-1 failure measured that way cannot be attributed, and an unattributable failure at M4 propagates into the power budget and from there into the M6 board.

The second remote is deliberately not a full remote. It holds a connection, consumes the downlink, generates uplink load, and reports telemetry — which is the entire set of things a second connection must do for §3.5's six questions to be answerable.

**Use the PCA10059 for bring-up and the spare MDBT50Q-CX-40 for range and density**, because the second measurement is a property of the module and antenna and the first is not. Swapping is a flash.

### 4.10 Provisioning is built for real at M3, with a bench tool rather than a process

`RADIO_PROTOCOL.md` §10.1 describes a record written once at manufacture. At M3 that reads as permission to defer it, and it is not.

**Built at M3:** the record format, the CRC check, the LTK installation from the provisioned key, and the refuse-to-operate-unprovisioned path (A19). **Deferred:** the manufacturing process around it — a script generating a partition hex is sufficient and correct for three units.

A key compiled into the firmware as a `#define` would be faster and would make A12 (no key), A13 (set mismatch) and A19 (unprovisioned) untestable. Those three are most of what stands between this product and a cross-associated match at a multi-mat event, and FS §2.3 calls that a scoring-integrity failure rather than an inconvenience. A boot path added after the fact is also a boot path nothing ever exercised.

---

## 5. Validation ladder

**Work the rungs in order.** Each isolates one failure domain, and a failure high up is uninterpretable if a lower rung was skipped. Record the date and firmware version against each result in §9.2.

The interface "works" in the sense that a happy path completed once. That is a much weaker claim than "reliable", and the gap between them is where this class of system fails: at hour three, on a cable pull, on a backgrounded tab, on someone else's laptop.

**There are two ladders.** V0–V8 test the USB link and belong to M2; W0–W8 (§5.3) test the radio and are worked across M3, M4 and M7. They are separate because they isolate different domains, and the V ladder must be green **before** the radio exists — that no-radio baseline is what makes §3.10's regression list attributable rather than a list of suspicions.

**V1–V3 passed against protocol v2.0 and are void.** The message set they exercised no longer exists. They are cheap to re-run and must be, after M2.

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

### 5.2 Two standing traps

- **`TEST 3` suspends link supervision until `TEST 0` or reboot.** Left on, every supervision test in V5 passes for the wrong reason. Send `TEST 0` first and confirm the reply.
- **`CONFIG_DONGLE_FAKE_LINK=y` fabricates `LINK … CONNECTED`** with synthetic RSSI and battery. Any test that appears to validate link reporting is validating a constant until this is `n`.

### V0 — Parser unit tests (host, no hardware) — **NEVER RUN**

`protocol.c` has no Zephyr dependencies precisely so this can run anywhere. It has never executed: the development machine has no host C compiler.

```bash
cd dongle/tests/protocol && make check
```

**Pass:** all cases green, exit 0. Covers `PROTOCOL.md` §14 T1–T16, encoder round-trips, and the `LINK` RSSI constraint of §7.

**Priority: highest.** The firmware parser is currently trusted on inspection alone. Every bug caught here is a bug not chased over USB. Run it on any Linux box, WSL, macOS, or MinGW/MSYS2 install. The v3.0 cases T11–T16 are new and include the two that fail closed: T7, the v2.0-shaped gestureless `EVT`, and T16, the duplicate `seq`. Where this runs *permanently* is an open decision — §3.2.

The app's counterpart suite (`npm test` in `wrsl-app`) does run: **113 tests green** at M1, covering the same §14 cases from the other side. That asymmetry is worth naming — one end of this protocol is tested and the other is not, and they are supposed to agree.

### V1 — Manual terminal, no browser

Disconnect the app first. With a terminal on the port at 115200 8-N-1:

| Send | Expect |
|---|---|
| `INFO` | `HELLO 3.0 <fw> <set> 0`, then one `LINK` line per remote |
| `PING` | `PONG` |
| `ECHO hello` | `ECHO hello` |
| `STATE RED SOLID 00A0FF OFF 000000` | Indicator stand-in reflects it |
| `HAP BOTH LONG` | One long pulse |
| `CFG BOTH 50 50` | Accepted; subsequent haptics and LEDs at half scale |

**Pass:** all of the above. Observing `ERR APP_TIMEOUT` ~2.5 s after the last typed line is **correct** (`PROTOCOL.md` §8) and is itself strong evidence — it exercises RX, parse, state transition, timer and TX in one message.

### V2 — App handshake

**Pass:** the app reaches `ready`; the debug log shows `INFO` → `HELLO` → two `LINK` lines → two `CFG` → two `STATE` → `PING` every 1 s thereafter. The handshake **must** include the unprompted `STATE` assertion — that step is what makes set substitution work, and it is the easiest one to omit because nothing visibly breaks without it until a remote is swapped mid-match.

### V3 — Deterministic stimulus

Send `TEST 1`, then `TEST 4`.

**Pass, `TEST 1`:** exactly seven `EVT` lines, one per button, `PRESS`, alternating RED/GREEN, 500 ms apart, contiguous `seq`. The scoreboard responds to all seven according to the loaded ruleset. `ACK <seq>` goes back for every one, `SILENT` for any button the ruleset leaves inert. No sequence-gap warnings.

**Pass, `TEST 4`:** 21 events per remote covering every gesture on every button. `HOLD` on `TOGGLE_CLOCK` resets the period clock; `HOLD_REP` on `FORWARD`/`BACKWARD` repeats the clock adjustment and nothing else repeats.

### V4 — Reverse path (app → dongle)

Drive the scoreboard from its own UI.

| Action | Expect on the wire |
|---|---|
| Start the clock with a secondary clock owned | `HAP <owner> BEAT` once per second, on the owner only |
| Transfer secondary-clock ownership | Beats move to the other remote within one beat |
| Stop the main clock | Beats stop; no `STATE` change (ownership is retained) |
| Deassign the secondary clock | `STATE` for that remote with F1 `OFF` |
| Enter a burst of four `ADD_POINT` presses during accrual | Four `ACK` lines and **no `BEAT`** inside the suppression window |
| Let a period run to 0:00 | One `HAP BOTH LONG` |
| Reach the configured main-clock warning | One `HAP BOTH WARN` |

**Pass:** the beat appears on the owning remote only, suppression holds during a burst, and suppression **releases** — a sustained hold-repeat must not silence the beat indefinitely.

### V5 — Supervision and disconnection

The most important rung. Send `TEST 0` first.

| # | Procedure | Pass criteria |
|---|---|---|
| V5.1 | Clock running, secondary clock accruing; close the browser tab | Beats stop immediately; dongle emits `ERR APP_TIMEOUT` within 2.5 s and instructs both remotes to render link-lost |
| V5.2 | Unplug the dongle | App shows disconnected, stops the `PING` cadence, offers reconnect |
| V5.3 | Sleep the laptop 30 s and wake it | App either recovers or cleanly reports a stale link; the match clock shows the correct elapsed time or halts and asks (FS §8.1) |
| V5.4 | Kill the browser process outright | Same as V5.1 |
| V5.5 | Pull the dongle while idle | Clean disconnect, no spurious `ERR` |
| V5.6 | Reconnect after any of the above | Handshake re-runs from scratch **including `STATE` assertion**; indicators are correct before anything else happens |
| V5.7 | Trigger the app watchdog (FS §8.3) | App drops the serial link deliberately; remotes render link-lost; the fault is visible on the display |

V5.3 is materially harder at v3.0 than at v2.0 and worth extra attention: the clock must be immune to wall-clock adjustment across a suspend, and an implausible gap must halt the clock rather than be absorbed.

### V6 — Reconnect lifecycle

| # | Procedure | Pass criteria |
|---|---|---|
| V6.1 | Disconnect in the app, reconnect | No port picker — `getPorts()` reuses the grant |
| V6.2 | Unplug, replug, reconnect | Handshake re-runs in full |
| V6.3 | Set a score and a secondary-clock owner, disconnect, reconnect | **No match state is lost, and no dongle state is resumed.** Indicators reassert from the app's copy |
| V6.4 | Reconnect 10× in a row | No leaked readers or writers; no duplicate `PING` cadences (watch for `PING` faster than 1 s) |
| V6.5 | Mid-match, swap to a different dongle | Match state fully retained; new set serial displayed; `STATE` asserted to both new remotes before the clock restarts; the substitution appears in the match record |

V6.4 finds real bugs — a reader lock or interval leaked per reconnect is invisible until it isn't. V6.5 is the field-substitution procedure of `SCOPE.md` §8.6 and is the reason `STATE` exists.

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

**W0–W5 are M3 (one connection). W6–W7 are M4 (two). W8 is worked at M4 and re-run in full at M7.**

| Rung | Milestone | What it isolates | Pass |
|---|---|---|---|
| **W0** | M3 | **Frame codec, on a host, no hardware** | A1–A7, A11, A20 green. The radio's V0, and it exists only if the codec is written without Zephyr dependencies (§3.4 deliverable 5) |
| **W1** | M3 | **Association and security.** One connection, encrypted from the provisioned key, no pairing procedure performed | `RR_IDENTITY` read and validated, CCCD subscribed, `LINK … CONNECTED` with a real RSSI. Negative cases are the point: A12 no key, A13 set mismatch, A14 proto major, A19 unprovisioned. **A pass on the positive case alone is not a pass** |
| **W2** | M3 | **Uplink.** Button → `UP_INPUT` → `EVT` → scoreboard | All three gestures on the four DK buttons (§3.4); 600 ms hold and 150 ms repeat measured, not assumed; A4 duplicate, A5 gap, A6 wrap |
| **W3** | M3 | **Downlink.** `STATE` → `DN_INDICATOR`, `HAP` → waveform, `CFG` → scaling | Indicators assert idempotently (A11); **`ACK … SILENT` puts nothing on the air** (A20) — verify by frame count, not by watching an LED that was never going to light |
| **W4** | M3 | **Round trip and the deadline rule** | `EVT`→`ACK`→render measured as a distribution. A8 a late `ACK` is not sent at all, A9 a second `ACK` replaces rather than queues, A10 a `BEAT` never truncates a `TAP`. `taps_dropped_late` non-zero when provoked and zero when not |
| **W5** | M3 | **Link state, in all four supervision relationships** (`RADIO_PROTOCOL.md` §9.1) | **A15 is the rung** — app supervision expires, radio stays up, both remotes render link-lost. Also A16 boot-is-DOWN, A17 sub-2 s reconnect emits no `DISCONNECTED`, A18 press out of range, A7 reboot re-baselines with no false gap, and §9.4 debounce under repeated power-cycling at the range edge (B4) |
| **W6** | **M4** | **Two connections.** The rung M4 exists for | Which §12.2 rung is schedulable — reported in the setup `LOG` line so every later latency figure is attributable. **B6: taps land on the originating remote only.** R5: cross-connection arrival skew measured. Beat on the owner only, from real hardware. B2 with the transmit-ring drop counter already instrumented |
| **W7** | **M4** | **Range, link budget and density**, at the dongle **as deployed** | 12 m with body shadowing, dongle in a laptop port below table height — not a bench with line of sight. p99, not median. Whatever density can be synthesised. **Take this rung with the MDBT50Q-CX-40 as remote #2** (§4.9). Escalation order if it does not close is fixed: USB extension cable, then a placement constraint in the documentation, then transmit power |
| **W8** | M4, re-run M7 | **Radio soak, and the §3.10 regression list in full** | ≥4 h with both remotes connected and pressing. `radio_gap` and `radio_dup` accounted for rather than merely observed; no transmit-ring drops beyond `BEAT`; B1 workqueue contention re-measured with the radio live; V4 and V5 re-run underneath it |

**Three traps specific to this ladder**, in the spirit of §5.2:

- **`CONFIG_DONGLE_FAKE_LINK=y` invalidates W1, W5, W6 and W7 completely** and does so while producing entirely plausible output. Retiring it is an M3 deliverable (§3.4 item 10), and confirming `INFO` reports `DISCONNECTED` with no remotes powered is the check that it is gone (B3).
- **A synthetic `battery_pct` from the DK invalidates anything about the battery indicator**, for exactly the same reason and with none of the visibility — there is no Kconfig symbol to notice. It is real only from M5.
- **A one-connection latency figure is not a two-connection latency figure**, and the difference is the whole substance of the §12.2 ladder. Do not carry a W4 number forward past W6.

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
| **Firmware still at v2.0** | The two ends do not interoperate. Every hardware rung is blocked. M2 |
| Radio protocol specified, implemented nowhere | `RADIO_PROTOCOL.md` v1.0 has no implementation on either side and no conformance suite. Its §14 cases are the counterpart to `PROTOCOL.md` §14 and, like V0, will be trusted on inspection until something runs them. M3 |
| Every latency figure in `RADIO_PROTOCOL.md` §12 is arithmetic | The connection-interval ladder, the retransmission counts and the 2.5 ms target are predictions from documentation. They have never been near this hardware. R2 |
| **No provisioning record, reader, or tool** | Nothing can connect without one, so this is the first M3 deliverable rather than a manufacturing concern — §4.10. The tool is work no document had claimed |
| **No radio conformance harness** | A1–A20 have the same status V0 had: written, never run. Rung W0, and it is only cheap if the frame codec is written free of Zephyr dependencies from the start (§3.4) |
| **No DFU strategy for the remotes** | Absent from every document, and `RADIO_PROTOCOL.md` §10.3 makes it consequential — the case against manufacture-time bonding was that a firmware update can silently clear a settings partition. Decide at M6, at the latest |
| **No hardware design work of any kind** | Module, ERM and driver, PMIC production part, battery sizing, button mechanics, enclosure, APPROTECT. M6, and its inputs are M4 and M5 measurements — §3.3 |
| Host parser tests never executed | The firmware parser is trusted on inspection alone, and M2 rewrites it. Highest-value outstanding item; needs only a machine with a C compiler |
| V4–V8 never run | Supervision, reconnect and soak behaviour unverified |
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
- [ ] V0 green on a machine with a C compiler, and a decision on where it runs permanently (§3.2)
- [ ] V1–V8 pass at v3.0, recorded in §9.2 with dates and firmware version — **before any radio code exists**
- [ ] W0 green, and W1–W5 pass on one connection (M3)
- [ ] W6–W7 pass on two, with the §12.2 rung in use recorded against every latency figure (M4)
- [ ] R1–R6 measured, with mitigations applied where they fail — **R3, R4 and R6 before M6 opens** (§3.3)
- [ ] D1–D8 pass; real VID/PID assigned and `requestPort()` filtered
- [ ] V8 clean for ≥4 h with zero sequence gaps and zero applied duplicates, using real instrumentation
- [ ] W8 clean for ≥4 h with both remotes connected, `radio_gap` and `radio_dup` accounted for
- [ ] Every ruleset in the library checked against the published rulebook for the current cycle
- [ ] §3.10 re-run in full after the radio lands, and again at M7 on custom hardware
- [ ] `PROTOCOL.md` amended for any further constraint that proves real; `RADIO_PROTOCOL.md` likewise, and its §12 predictions replaced by measurements

### 9.2 Results log

| Date | FW | Proto | Rung | Result | Notes |
|---|---|---|---|---|---|
| 2026-08-07 | 0.1.0 | 2.0 | V1 | pass | *void at v3.0* — `ERR APP_TIMEOUT` observed and correct |
| 2026-08-07 | 0.1.0 | 2.0 | V2 | pass | *void at v3.0* — handshake and PING cadence confirmed |
| 2026-08-07 | 0.1.0 | 2.0 | V3 | pass | *void at v3.0* — `TEST 1`, seven events, confirmation round trip |
| 2026-08-09 | — | 3.0 | app | pass | M1: 113 tests, lint and build clean, browser-verified against `FakeDongleTransport`. **Not a ladder rung** — no hardware involved |
| | | | | | |

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
