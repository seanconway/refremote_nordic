# RefRemote — Plan, Status and Validation

**Status as of 2026-08-15.** `SCOPE.md` and `SYSTEM_FUNC_SPEC.md` are the authority on direction; `PROTOCOL.md` **v4.0** and `RADIO_PROTOCOL.md` **v2.0** answer to them. Both bumped major this session: `STATE`'s `<f1rgb>`/`<f2rgb>` (arbitrary hex, never calibrated against this hardware) replaced by `<f1colour>`/`<f2colour>`, one of a fixed `RED`/`GREEN`/`BLUE`/`YELLOW` palette the remote renders identically across all four indicators (`HISTORY.md` §9.3). `radio_proto_major` is now 2; a remote or dongle still on the old firmware refuses to connect (`ERR REMOTE_PROTO_MISMATCH`) rather than misrendering. The scoreboard application is at v4.0 (M1, complete). **M2 and M4's fast path are both done**, and **M5's GPIO harness (S13/S14) is done**: all seven buttons and all four RGB indicators are real, on hardware, mapped to the product's physical layout rather than DK button order, with `LED_F1`/`LED_F2`/`LED_LINK`/`LED_PWR` all genuinely rendering colour — including a same-session extension request that added a bench-only simulated-battery frame, a 3-colour `LED_LINK`, and cross-remote holder-colour rendering for the secondary-clock and tri-state-flag shapes (`HISTORY.md` §9.3, 2026-08-14). **The development bottleneck is PCB fabrication lead time, not bench validation** (§4.16): the near-term line is now S15–S19 (haptic driver, ERM, nPM1300-EK) → M6's PCB design and order. W4/W5's edge-case matrix, the full W6 characterisation, W7 range/density, and the rest of M4's deeper validation all still run — during the PCB fab wait, on firmware that has stopped changing shape (S20, S10–S12). One item found the previous session and parked undiagnosed — the remote fails to reconnect after a dongle power-cycle — is now root-caused and fixed (2026-08-16), prompted by the user reproducing it live rather than by the parked bench procedure: `remote/src/link.c`'s post-disconnect advertising restart discarded `bt_le_adv_start()`'s return value with no retry, a warm-path failure mode the earlier static read never exercised. `remote/build-red` rebuilds clean against the fix, but it is **not yet bench-confirmed** — the item stays parked in §3 pending a flash-and-repro, not blocking S15+. **The next step is S15.**

**This document is forward-looking.** It carries the current state (§1), the work queue of alternating agent and bench steps (§2), the parked items with their unlock conditions (§3), the decisions that still bind (§4), the validation reference — rungs, procedures, pass criteria (§5), deployment validation (§6), unmeasured risks (§7), known gaps (§8), and the definition of done (§9). **The record of completed work, the results log and the project history live in [`HISTORY.md`](HISTORY.md)**, which is append-only and preserves its original section numbering — a citation of the old `PLAN.md` §2.x or §9.2/§9.3 resolves there unchanged.

**Both documents are living.** When a queue step completes, its checkbox is ticked here, anything it changes moves in the same edit, and the results are appended to `HISTORY.md` §9.2 (and §9.3 for the narrative) **in the same change**. A plan that lags the code is worse than no plan.

| For | See |
|---|---|
| What the system is and why | [`SCOPE.md`](SCOPE.md) |
| How it behaves | [`SYSTEM_FUNC_SPEC.md`](SYSTEM_FUNC_SPEC.md) |
| The wire protocol | [`PROTOCOL.md`](PROTOCOL.md) |
| The radio protocol | [`RADIO_PROTOCOL.md`](RADIO_PROTOCOL.md) |
| **What to build — the dongle** | [`dongle/BUILD_SPEC.md`](dongle/BUILD_SPEC.md) |
| **What to build — the remote** | [`remote/BUILD_SPEC.md`](remote/BUILD_SPEC.md) |
| Build, flash, manual test | [`dongle/README.md`](dongle/README.md) |
| **Completed work, results log, history** | [`HISTORY.md`](HISTORY.md) |

---

## 1. Current state

| # | Milestone | State |
|---|---|---|
| **M0** | USB link at v2.0, working on hardware | ✅ done — `HISTORY.md` §2.1 |
| **M1** | Scoreboard application to v3.0, the functional specification, and the design system | ✅ done — `HISTORY.md` §2.3 |
| **M2** | **The firmware programme** — dongle wire v3.0, the radio, and a DK remote, to an end-to-end demonstration | ✅ done — 2026-08-13. The demonstration ran: press → score → tap, on that DK only. History: `HISTORY.md` §2.6–§2.8, §9.2 |
| **M3** | *Retired as a separate milestone — absorbed into M2* | number retired, not reused — see below |
| **M4** | The **2:1** link — two peripherals, two connections, one central | ✅ fast path done — 2026-08-13. Full W6 characterisation stays parked (S10–S12, §3) |
| **M5** | Full-feature remote firmware on the DK — GPIO buttons, RGB indicators, ERM, nPM1300 | queue §2.3, S13–S19 |
| **M6** | Custom remote PCB designed | §2.4 — gated on M4 and M5 closing, **not** on R3/R4/R6 being confirmed (§4.15 — they can't be, pre-PCB) |
| **M7** | Firmware ported to the custom remotes; system validated on production-shaped hardware | §2.4 |
| **M8** | Custom dongle, for BOM cost — **optional** | §2.4 |
| **M9** | Deployment validation and MVP hardening — USB identity, ruleset verification, §6 and §8 | §2.4 — runs alongside from M5 onward |

**M3 is retired rather than renumbered, and M4–M9 keep their numbers.** The last renumber left references pointing at milestones that had moved (`HISTORY.md` §9.3, 2026-08-10); a stable reference is worth more than a tidy sequence. The same rule governs the queue: **closed step numbers are never reused.**

| Component | State |
|---|---|
| `SCOPE.md`, `SYSTEM_FUNC_SPEC.md` | Current. Authoritative. |
| `PROTOCOL.md` v3.0 | Implemented both ends, byte-identical in both repos (§4.7) |
| `RADIO_PROTOCOL.md` v1.0 | Implemented both ends; W0 green, W1 positive case green on hardware. **Every latency figure in its §12 is a prediction awaiting measurement.** This repo only |
| Scoreboard app | v3.0, M1 complete. 137 tests, lint and production build clean |
| Dongle firmware | v3.0, flashed, answering on hardware. Wire layer V0–V6 green; radio central (`radio_ble.c`) connecting for real, round-robin connection scheduler, A13 `set_serial` check (`HISTORY.md` §9.2, 2026-08-13) |
| Remote firmware | Full DK remote per `remote/BUILD_SPEC.md`; RED flashed and confirmed connecting on hardware 2026-08-13. GREEN not yet built |
| Provisioning | Identity baked in at build time (§4.13), confirmed on hardware both sides. Tooling: `dongle/tools/provision.py`, `tools/build_set.ps1`, `tools/ncsenv.ps1` |
| Host suites | `dongle/tests/protocol` 131 checks and `dongle/tests/rframe` 67 checks, both green. **Nothing runs them for you** — `make check` after any change to `protocol.c` or `rframe.c` |

**Standing constraints, permanently true:**

- **The COM port is exclusive.** A terminal and the app cannot both hold it — terminal rungs and browser rungs are sequenced, never interleaved.
- **The dongle board has one fitted lamp, on the `GREEN` channel** (P0.06). `HAP RED`, `STATE RED` and every `ERR` are invisible on it, deliberately — *"no blink" never means "no error"*; errors always leave as `ERR` lines. [`dongle/BOARD.md`](dongle/BOARD.md) is the reference; the devicetree aliases are not.
- **The no-radio baseline is a build configuration kept for the life of the project.** `CONFIG_DONGLE_RADIO=n` runs the whole wire layer with the radio compiled out; every item in §5.4's regression list is diagnosed by asking *does this still happen with the radio compiled out?*

---

## 2. The work queue

**The development rhythm is alternating step types.** **[AGENT]** steps are executable at the keyboard — code, builds, host suites, tooling, procedure preparation. **[BENCH]** steps need hands on hardware and human eyes — flashing, pressing buttons, watching LEDs and the scoreboard. Each step names what it needs, what it closes (tagged with the rung and case IDs of §5, which are stable), and where its pass criteria live. Steps are worked in order; a bench step's procedure is prepared by the agent step before it, so bench time is spent observing rather than deciding.

**When a step completes:** tick it here, append the results to `HISTORY.md` §9.2/§9.3 in the same change. **When plans change:** insert new steps with fresh numbers; never renumber or reuse a closed step's number.

**The ordering principle, unchanged:** each step adds exactly one new thing that can be wrong. The phase map:

| Phase | Steps | What is new, and therefore what a failure means | Hardware |
|---|---|---|---|
| 1–2 | **M2** — S1–S7 | One radio connection, then the remote end: real presses, real rendering, a real end-to-end loop | Product dongle + nRF52840 DK |
| 3 | **M4** — S8–S9, fast path | The **second** connection. A failure is central scheduling, routing or skew — nothing else changed. Deeper validation (S10–S12) deferred to the PCB fab wait, §4.15 | + PCA10059 as remote #2, then the spare MDBT50Q-CX-40 (§4.9) |
| 4 | **M5** — S13–S19 | The remote's real peripherals — seven buttons, RGB, an ERM, a PMIC. A failure is hardware or drivers, not protocol. Also produces M6's design-target estimates for R3/R4/R6 (§4.15) | + GPIO harness, ERM, nPM1300-EK |
| 5 | **M6** | Nothing runs. Schematic, layout, BOM, enclosure — **inputs are M4's number and M5's estimates, not confirmed measurements** (§4.15) | — |
| 6 | **M7** | The custom board. A failure is the port or the board | Custom remote PCBs |
| 7 | **M8** | The custom dongle. Optional, cost-driven, deliberately last | Custom dongle |

**One ordering constraint runs backwards through this table:** M6 needs M4's connection-interval headroom number and M5's best-available ERM/current-draw estimates as **design inputs** — it does not wait for R3, R4 or R6 to be *confirmed*, because none of the three can be, before the enclosure and strap that determine them exist. §4.15, §7.

### 2.1 Finishing M2 — one connection, ending in the demonstration

**M2's exit is the end-to-end demonstration:** a physical press on the DK, scored on the scoreboard, acknowledged back as a rendered haptic on that DK, and on that DK only.

- [x] **S1 [AGENT] — Implement the A13 check.** `dongle/src/radio_ble.c` `handle_identity_read()` compares `RR_IDENTITY`'s reported `set_serial` against the dongle's own `PROV_RECORD.set_serial` and produces `ERR SET_MISMATCH` + disconnect on mismatch (`RADIO_PROTOCOL.md` §10.2, §14 A13). Rebuild all three configurations; both host suites green. *Closes the §8 A13 gap.* ✅ 2026-08-13 — `HISTORY.md` §9.2.
- [x] **S2 [AGENT] — Prepare the W1 negative cases.** Generate deliberately-wrong provisioning headers and DK builds: A12 (wrong key), A13 (mismatched set serial), A14 (protocol major mismatch), A19 (all-zero key, remote side). `provision.py`/`build_set.ps1` already support building against an arbitrary header (§4.13). Write the flash-order walkthrough and per-case expected observations into S3's entry here. *Needs: nothing but the toolchain.* ✅ 2026-08-13 — `HISTORY.md` §9.2.
- [x] **S3 [BENCH] — W1 negative cases on hardware.** ✅ 2026-08-13, all seven steps run in order, every case matching its predicted wire signature exactly — `HISTORY.md` §9.2. **Closes W1.**
- [x] **S4 [AGENT] — Fixes from S3; prepare W2/W3.** Nothing from S3 needed fixing — all seven steps passed on the first attempt, so this step is procedure prep only. Walkthrough written into S5 below. ✅ 2026-08-13.
- [x] **S5 [BENCH] — W2 uplink + W3 downlink.** ✅ 2026-08-13, all seven steps pass — `HISTORY.md` §9.2. **Closes W2 and W3.** One scope call made during the session, not before it: exact `HOLD_REP` cadence and the A6 wrap case were both judged not worth forcing at this rung — see the parked-items row and `HISTORY.md` for why.
- [x] **S6 [AGENT] — W4/W5 instrumentation and procedures.** ✅ 2026-08-13 — `HISTORY.md` §9.2/§9.3. Exported the app's full rolling ack-latency sample set (`ackLatenciesMs`, `wrsl-app` `DongleService.js`) alongside the existing p99/max, so `exportDiagnostics()` carries a real distribution rather than two summary numbers — 137 wrsl-app tests still green. **Found while preparing this, not while running it**: what that counter measures is the app's own EVT-received-to-ACK-sent turnaround only (25 ms of the 120 ms budget, RP §11) — the two radio legs of the full press-to-tap round trip have no wire timestamp on either end and aren't newly instrumented here. Also found A9 is not implemented (mechanism 2 needs a GATT primitive Zephyr doesn't have) — recorded as a known gap (§8), dropped from W4's pass criteria rather than written into a provocation procedure it can't pass. LE Flushable ACL Data investigated and recorded as a considered-and-deferred decision, §4.14. Walkthrough for A8, A10 and the W5 supervision matrix written into S7 below.
- [x] **S7 [BENCH] — the M2 demonstration.** ✅ 2026-08-13 — a physical press on the DK, scored on the scoreboard, acknowledged back as a rendered haptic on that DK, and on that DK only. **Closes M2.** `HISTORY.md` §9.2/§9.3.

### 2.2 M4 — the 2:1 link

One central, two peripherals, two connections. Everything else is unchanged from M2, which is the point: a failure here is scheduling, routing or skew. The hardware decision is §4.9 — **PCA10059 for bring-up, the spare MDBT50Q-CX-40 for range and density.**

What M4 exists to establish (each needs the second connection to mean anything):

| # | Question | Why it needs two connections |
|---|---|---|
| 1 | That 7.5 ms is schedulable on the **pair**, and the p99 latency it delivers at range | Central scheduling requires every link's timing-event to fit inside the common interval, so the interval is a property of the *pair*. **The latency figure is the one that would reopen SCI** (§4.8) |
| 2 | Acknowledgement routing to the correct remote (B6) | With one remote, a broadcast tap and a correctly routed tap are indistinguishable. This is the defect the whole `src`/pending-table mechanism exists to prevent |
| 3 | Cross-connection arrival skew (R5) | Ordering is guaranteed per connection and not between them (`RADIO_PROTOCOL.md` §6.4). There is no skew with one link |
| 4 | Downlink fan-out under load | 1 Hz beat to the owner only, plus asynchronous taps, plus `LINK` re-emission — B2's transmit-ring pressure is a two-remote condition |
| 5 | Beat-on-owner-only, on real hardware | The emulator validated the app's half (§5.5). The radio half is new |
| 6 | Density behaviour, first look | Two connections from one central is the smallest system with an aggregate to degrade |

**Fast path to M5** — the minimum that establishes the 2:1 link actually works, not the full validation ladder:

- [x] **S8 [AGENT] — PCA10059 second-remote firmware.** ✅ 2026-08-13 — `HISTORY.md` §9.2. New app `remote_dummy/`: `remote/src/link.c` reused unmodified by source reference (zero DK-specific code in it — every board dependency is through `haptic.c`/`indicators.c` function signatures, which this app supplies its own PCA10059-shaped implementations of); new `buttons.c` (the one real button, `remote/src/buttons.c`'s state machine reduced to one entry, plus a self-stimulus timer on `CONFIG_REMOTE_DUMMY_STIM_INTERVAL_MS` firing synthetic `PRESS`es through the identical `gesture_cb` path a real edge would use); new `indicators.c` (the RGB LED renders `DN_INDICATOR` **F1 colour** — confirmed against the installed board devicetree, not alias names, per CLAUDE.md §6's standing trap — the plain green LED renders `LED_LINK`; F2 has no third surface and is not rendered, recorded rather than silently dropped); new `haptic.c` (true no-op — no ERM, no LED budget left for a proxy, `DN_HAPTIC` is consumed per S8's own "hold a connection, consume the downlink" scope, not rendered). Builds clean, 0 warnings, both roles: `remote_dummy/build-green` (GREEN, 167,776 B flash) and `remote_dummy/build-red` (RED, identical size). `remote/build-green` also freshly rebuilt against the current RR-0006 set for the colour-swap step below.
- [x] **S9 [BENCH] — the 2:1 demonstration.** ✅ 2026-08-13 — both pairings connected and routed correctly, including after the colour swap. **Closes M4's gate on M5.** `HISTORY.md` §9.2/§9.3.

  *Pass criteria: both pairings connect clean and route correctly. Closes M4's gate on M5.* The exhaustive W6 checks (7.5 ms interval cleanliness, transmit-ring pressure, R5 skew) stay parked at S10–S12 (§3) — this step only needs to prove the pairing works and routes by identity, not characterise it.

**Deferred, not skipped** — moved to the parked table (§3), re-admitted by the PCB fab wait (after M6 ships) or M9: the old S10 (scripted V6.4 + soak tooling), S11 (W7 range/density, V6.5 dongle swap, V7 version guard), and S12 (the overnight V8/W8 soak). None of these gate M5 or M6; they gate confidence in a system that, by M6, is about to change shape anyway (§4.15).

**One trap on M4 numbers:** a one-connection latency figure is not a two-connection latency figure. Do not carry a W4 number forward past W6.

### 2.3 M5 — the full-feature remote, on the DK

The DK's GPIO carries what its onboard peripherals could not: the three remaining buttons and four RGB indicators, a real ERM and driver IC, and the nPM1300-EK. **This is where R3, R4 and R6 get their design-target estimate for M6** (§4.15) — the most representative rig that can exist before the enclosure does, understood as a number M6 designs against, not a closed measurement. Order follows §4.15's own logic: build what's fully specified first (buttons, indicators), then the two items still needing a real-world component decision (haptic driver, PMIC bring-up), so the open decisions don't block the closed ones.

- [x] **S13 [AGENT] — GPIO harness driver: the remaining buttons and indicators.** ✅ 2026-08-14 — `remote/src/buttons.c` extended for all seven buttons (three external, P0.02/03/04, plus the four built-in remapped to the product's physical layout — `BACKWARD`/`FORWARD`/`F2`/`F1` on the DK's own buttons, `ADD_POINT`/`TOGGLE_CLOCK`/`REMOVE_POINT` external); `remote/src/indicators.c` renders real RGB on all four `DN_INDICATOR` positions via three new PWM instances (`remote/boards/nrf52840dk_nrf52840.overlay`). Wiring plan recorded in `remote/BUILD_SPEC.md` §2. `HISTORY.md` has the full narrative.
- [x] **S14 [BENCH] — Breadboard the harness.** ✅ 2026-08-14 — seven buttons and four RGB indicators wired and confirmed on hardware: button remap verified after flashing, RGB wiring confirmed common-anode by diode test, and one real bug found and fixed in the same pass (`LED_PWR` wired but never rendered — see `HISTORY.md`). `DN_INDICATOR`'s actual colour renders (closing the gap `PLAN.md` §5.6 listed as unclaimable since M2), extended further this same session per user request: `LED_PWR`'s 3-band ladder (driven by a new bench-only `DN_SIMSOC` frame, `RADIO_PROTOCOL.md` §7.5), `LED_LINK`'s 3-state red/yellow/green rendering, and `LED_F1`/`LED_F2` now rendering the holding athlete's colour on both remotes (`wrsl-app` `matchReducer.js`) rather than only the owning remote. Not yet exhaustively re-walked: every gesture on all seven buttons individually confirmed on the bench one at a time — worth a pass before treating S14 as fully closed rather than functionally closed.
- [ ] **S15 [AGENT] — Haptic driver + ERM.** `SCOPE.md`/`SYSTEM_FUNC_SPEC.md` leave the driver IC unspecified — "an ERM plus driver IC" — so this step's first job is a real candidate recommendation (nordic-mcp / datasheet check for something DK-breadboard-friendly), not just code. **Procurement**: the part isn't on the bench yet unless already ordered — flag before writing driver code against a part nobody has. Once chosen, replace `remote/src/haptic.c`'s `pwm_led0` proxy with the real driver, and extend `DN_CONFIG` scaling (`RADIO_PROTOCOL.md` §7.3) to whatever amplitude control the real driver exposes, still preserving the `BEAT`:`TAP` ratio.
- [ ] **S16 [BENCH] — Wire the ERM, strap it to a wrist.** Confirm the waveform table renders with the right character end to end, and take R3/R4's first real reading — **a design-target estimate for M6, not closure** (§4.15); M7 is where it closes.
- [ ] **S17 [AGENT] — nPM1300-EK integration.** Charge, fuel gauge, regulator, USB-C — check nordic-mcp for an existing NCS nPM1300 sample before writing this from scratch. Retires `SYNTHETIC_BATTERY_PCT`, the last fake value anywhere in the system, with a real fuel-gauge reading.
- [ ] **S18 [BENCH] — Wire the nPM1300-EK.** Confirm charging behaviour and a real `battery_pct` on the wire that actually tracks a battery under load. Take R6's first real reading — again a design-target estimate (§4.15), not the ten-hour M7 measurement.
- [ ] **S19 [BENCH] — The M5 demonstration.** All of it together on one DK — seven buttons, four RGB indicators, real haptic, real battery — modelling the complete remote FS §3.1/§3.2 describes. **This is what M6's component selection and layout get designed against.** *Closes M5.*

### 2.4 Later phases — coarse until their predecessor closes

Planned deliberately at low resolution; detailing them now would re-create the deferral noise this document was restructured to remove. Each gets its own queue steps when its predecessor closes — the same principle that just turned M5 from a paragraph into S13–S19.

**M6 — the custom remote PCB.** No firmware runs. Inputs are M4's connection-interval headroom and M5's ERM/current-draw estimates — best-available numbers, designed against, not measurements the board waits to be confirmed. Nothing about the hardware design is recorded anywhere in this repository yet. Open items at least: module selection (an MDBT50Q variant keeps the RF characterisation), the ERM and driver chosen at M5 or the dual-motor contingency, nPM1300 as the production part, battery chemistry and capacity sized from R6's estimate with headroom (§4.8), the FS §3.1 button mechanics and oversized `TOGGLE_CLOCK` datum, four RGB indicators adjacent to their buttons, USB-C charging, APPROTECT as a manufacturing step, and **a DFU strategy for the remotes, which exists in no document** (§8). **If M7 finds an estimate wrong, that is a second PCB spin** — an accepted cost of this ordering, not evidence the ordering was wrong (§4.15).

**M7 — port and validate on custom hardware.** The firmware is M5's with the board layer swapped; M7's job is to test that claim rather than assume it: the full radio ladder and §5.4 regression list re-run on production-shaped hardware, then the parts of §7 only real remotes reach — B6 on two wrists, B8's collision, **R3/R4 through the real enclosure and strap and R6 over a full ten-hour day, closed for real here rather than estimated at M5** (§4.15), R5 during live matches. First point a complete officiating set exists, so V6.5 and `SCOPE.md` §8.6 become testable end to end.

**M8 — a custom dongle, optional.** Cost-driven, deliberately last. It changes the RF platform underneath a validated system: everything measured at M4 and M7 about range and density is a property of the MDBT50Q module, and a custom dongle re-opens all of it. If BOM cost justifies that, the re-measurement is part of the milestone.

**M9 — deployment validation and MVP hardening.** Not a phase at the end; **runs alongside from M5 onward.** D1–D8 (§6) — D5 is blocked on a real USB VID/PID, a procurement item to start early; D6 requires deciding the production domain before deployment. Plus the §8 gaps M2–M8 do not close: `requestPort()` filters, connection-error language, the Web Worker heartbeat if R1's test demands it, self-hosted fonts, and **ruleset verification against the published rulebooks** — which needs a rules-literate reviewer and is therefore the item most likely to be left until it blocks a real event. Definition of done: §9.

---

## 3. Parked items

Deferred work in one place, each with the condition that re-admits it. **A parked item is not a closed item** — when its unlock condition is met, it enters the queue; nothing here is quietly dropped. (Ladder rungs already scheduled in §2 are not parked — they are queue steps.)

| Item | Why parked | Unlock condition | Re-entry |
|---|---|---|---|
| **S10–S12** — scripted V6.4/soak tooling, W7 range/density, V6.5 dongle swap, V7 version guard, the V8/W8 overnight soak | None of it gates M5 or M6 (§4.15) — it's confidence-building on a system about to change shape at M6 anyway | The PCB fab wait (after M6 ships) | Re-enters the queue as **S10, S11, S12** unchanged; only their timing moved |
| **S20** — S7's original W4/W5 edge-case matrix: A8, A10, the ack-latency distribution check, and the four-relationship supervision matrix (A15–A18, A7, B4) | None of it gates M2's actual exit criterion (the demonstration, which already ran informally); A10 tests LED-proxy `haptic.c` code M5 replaces wholesale (§4.16) | The PCB fab wait, alongside S10–S12 | Full walkthrough preserved below, §3.1 |
| **V4 row 7** — burst suppression observed on the wire | An operator click dispatches the same `INPUT` a press would but produces no wire `EVT`, so there is nothing to suppress against; mechanism is unit-tested (`DongleService.test.js`) | Dongle-originated `EVT`s under app load — real presses or `TEST` modes | S12 soak (PCB fab wait), or any W2+ session |
| **V5.1, remote-render half** — remotes render link-lost on app timeout | No remote existed; `radio_null` stub | DK remote connected — met, but the check itself is A15, moved to **S20** | **S20**, PCB fab wait |
| **V5.3 at 30 s+** — sleep long enough that the OS tears down the USB device | 15 s pass pinned the short-sleep case only | Nothing — cheap bench add-on | Any bench session; fold into S12 (PCB fab wait) |
| **V6.4** — 10× reconnect, no leaked readers/writers | A stopwatch on a `PING` interval can't catch a one-interval leak; needs scripted observation | Scripting, not hardware | **S10**, PCB fab wait |
| **V6.5** — mid-match dongle swap | Needs a second flashed dongle | Second dongle flashed (M4) | **S11**, PCB fab wait |
| **V7** — version guard | Cheap but low-yield until a mixed-firmware fleet is possible | Second dongle exists | **S11**, PCB fab wait |
| **V8 / W8** — soak | Runs overnight once the configuration is stable, rather than blocking progress | S1–S9 stable | **S12**, PCB fab wait |
| **R1 test** — background-tab throttling vs the heartbeat | Wake lock + banner applied at M1; the test establishing whether that suffices has not run | Nothing — runnable today, needs a 6+ min procedure | M9, or any idle bench slot; the Web Worker is built only if the test fails |
| **Emulator: no `LOG counters` lines** | Surfaced by the wire-log diff; app's counter path never exercised against the emulator | wrsl-app work, any time | With the next emulator change |
| **Emulator: `ECHO` collapses runs of spaces** | `args.join(' ')` vs the firmware's verbatim raw line; firmware is correct | wrsl-app work, any time | With the next emulator change |
| **Host suites as a routine build step** | `make check` is not wired into `west build`; no CI. A remembered step | A CI runner, or a build-system hook decision | Open — the standing mitigation is the §1 reminder |
| **Exact `HOLD_REP` cadence measurement** (150 ms) | S5's wire log is second-resolution — a stopwatch on a human thumb is no more precise than what it already showed (repeats while held, stops on release). Real precision needs the same kind of instrumentation W4's latency distribution needs, not a one-off script | S6's W4 latency-distribution tooling, if it turns out to be cheap to extend | Ride along with **S6**, only if free; not worth building standalone |
| **A6 (`CTR` wrap) on real hardware** | Not run — S5's `FORWARD` hold was ~2 s, not the ~40 s needed to walk 255→0. The arithmetic itself is already host-suite-proven (`dongle/tests/rframe`, W0); a hardware-only wrap defect is low-probability and nothing currently suggests one | A suspected wrap-boundary bug, or an idle moment at a future bench session | Any future bench session with radio up; not a blocker for anything |
| **Rulesets vs published rulebooks** | Needs a rules-literate reviewer, not an engineer | Reviewer availability | M9 — **before any real match, and again each rules cycle** |
| **Real USB VID/PID + `requestPort()` filters + D5** | Procurement/manufacturing item | VID/PID assigned | M9 — start early, long-lead |
| **Connection-error language** (policy block vs cancelled picker, D4) | App work, low urgency | Nothing | M9 |
| **Self-hosted fonts** | Licensed `.woff2` binaries needed; pre-event load caches meanwhile | License purchase | M9 |
| **Remote DFU strategy** | Absent from every document; consequential (`RADIO_PROTOCOL.md` §10.3) | Decision needed | **Decide at M6, at the latest** |
| **LE Flushable ACL Data** — usable in v3.4.0? | Experimental; the deadline rule must hold without it | Investigation | **S6**, in passing |
| **Remote fails to reconnect after a dongle power-cycle** (reported 2026-08-14, root-caused and fixed 2026-08-16) | The 2026-08-14 static read checked the dongle's connect/retry symmetry and RP §9.5's RED/GREEN shared-slot round-robin and correctly cleared both — it never looked at what differs between `link_init()`'s one cold `start_advertising()` call at boot (always succeeds) and `handle_disconnected()`'s warm one after a live connection is torn down (the case this scenario actually exercises). `remote/src/link.c`'s `start_advertising()` discarded `bt_le_adv_start()`'s return value unconditionally with no retry, and this board has no LOG channel to have ever surfaced a failure there — `LED_LINK` staying **red** rather than yellow was the tell (RP §9.2: red means the radio connection itself is down, not just `DN_HOST` catching up). Fixed: check the return code, retry after 500 ms (`adv_retry_work`, mirroring `radio_ble.c`'s own `retry_work` idiom), cancel the retry on a successful connect. `remote/build-red` rebuilt clean, 0 warnings — `HISTORY.md` §9.2/§9.3, 2026-08-16 | Flash `remote/build-red` and reproduce the original scenario (dongle unplug/replug, DK left powered throughout) to confirm the fix resolves it on real hardware | Next bench session touching the radio; not blocking S15+ |

### 3.1 S20, in full — preserved from the original S7

Written during S6, displaced from S7 by §4.16. Nothing here changed; only when it runs did.

**W4 — the deadline rule (A9 excluded, §8):**

1. **A8 (late `ACK` never sent).** From DevTools' console on the scoreboard tab, block the JS event loop synchronously for >120 ms timed around a press (e.g. a busy `for` loop run right as a button is pressed) — the same technique V5.7 used to trigger the watchdog. Send `INFO` from the raw command console before and after; confirm `LOG counters late=N ...` incremented by exactly the number of presses caught in the stall, and that no `HAP` fired for those presses. Then press normally again and confirm `late=` stops incrementing — the deadline rule must be provoked *and* shown absent under normal operation, not just provoked once.
2. **A10 (`BEAT` never truncates a `TAP`).** Start a secondary clock owned by RED (riding time in NCAA or folkstyle) so `HAP RED BEAT` fires once a second — LED1 shows a faint 1 Hz pulse. Time an `ADD_POINT` press to land as close as possible to a beat's edge; a `TAP` pulse is only 40 ms (`remote/src/haptic.c`), so this needs a few tries. *Expect:* the `TAP` always plays out its full, unmistakably-brighter pulse — never visibly cut short or replaced by a dim one mid-flash. **Re-run against M5's real ERM, not the LED proxy — the code under test will be different by then.**
3. **The distribution.** After a few minutes of ordinary pressing (no deliberate stall), export diagnostics and check `counters.ackLatenciesMs` — this is the app's own share of the budget as an actual sample set, not just the p99/max already shown in the panel. A skew or a p99 that's crept up is worth a second look; a single number never would have shown that.

**W5 — the four supervision relationships** (`RADIO_PROTOCOL.md` §9.1; the USB-side two are already proven from V1/V5 and are listed for completeness, not re-run):

4. **Dongle watches remote** *(new)*. With RED connected, move it out of range or power it off. *Expect:* `LINK RED DISCONNECTED` after the 2 s debounce (§9.4), not immediately.
5. **Remote watches dongle** *(new — A15/A16)*. Power RED on **before** the dongle, or with it unplugged. *Expect:* `LED_LINK` off and the repeating double buzz from boot — never a false "connected" while waiting for first contact (A16). Then, with RED already connected and rendering `LED_LINK` solid, **close the scoreboard tab or unplug the dongle from the laptop while leaving the DK powered.** *Expect (A15):* the DK's radio connection to the dongle stays up — the dongle keeps sending `LINK RED CONNECTED` if a terminal is watching — but the DK still renders link-lost, because the dongle sent `DN_HOST DOWN` on its own app-supervision expiry. This is the case RP §9's own text calls "most likely to be missed, because everything about the radio looks healthy while it happens" — confirm by watching the DK, not the dongle.
6. **A17 (sub-2 s reconnect).** Move RED out of range for roughly 1.5 s, then back. *Expect:* the app shows `CONNECTING` then `CONNECTED` with **no `DISCONNECTED` line at all** in between — the debounce doing its job.
7. **A18 (press while out of range).** With RED out of range, press a button, then bring it back. *Expect:* no `EVT` while it's out, and the dongle's `LOG counters gap=N` incremented once RED reconnects — the press wasn't silently lost, it shows up as a real, attributable gap.
8. **A7 (reboot re-baselines).** Power-cycle RED while connected. *Expect:* it reconnects and the first press afterward produces a normal `EVT`, with **no gap logged** — a fresh connection re-baselines `CTR` (`UP_READY`'s `ctr_base`) rather than reporting a false gap against the old session's counter.
9. **B4 (debounce under repeated power-cycling).** Power-cycle RED three or four times at the range edge in quick succession. *Expect:* the debounce behaves the same every time — no spurious `DISCONNECTED` on a fast reconnect, no missed real disconnect on a slow one.

*Pass criteria: §5.3 rungs W4 (A9 excluded, §8), W5.*

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

**The consequence that runs backwards into hardware, taken knowingly.** The interval is an input to the power budget (R6) and through it to battery sizing and the PCB (§2's phase map). A board sized against rung 3 and later moved to rung 1 sees roughly three times the connection events per second, so a battery sized exactly to rung 3 would need a respin. Size with headroom, and keep the interval a single named constant.

Three standing invitations to do the ordinary thing, each of which fails silently:

- **Do not raise the ATT MTU or enable Data Length Extension.** At rung 3 the 27-byte payload is no longer a *precondition* for anything, so the argument is now the simpler one: there is nothing to carry. The largest frame is 10 bytes, a larger MTU lengthens air time against the density requirement, and it would quietly foreclose the SCI contingency.
- **Do not enable peripheral latency or connection subrating.** They are the standard BLE power levers and they work by skipping connection events, which is exactly what delays an acknowledgement tap. Subrating is not needed at all now that SCI is deferred.
- **Do not add an application-level retry to the press path.** Link-layer retransmission inside the connection event is the bounded effort this design wants. Anything above it fires only when the press is already worthless, and delivers the late tap `PROTOCOL.md` §11 rules out.

### 4.9 The 2:1 link is tested with two physical peripherals, asymmetric

**One nRF52840 DK as remote #1, one spare nRF52840 dongle (PCA10059) as remote #2.** Not one DK holding two connections, and not a second DK. Recorded in full here because the rejected options are the cheaper-looking ones and will look attractive again.

**Why not one DK emulating two remotes.** It is achievable — Zephyr supports multiple local identities and multiple advertising sets, so one nRF52840 can hold two peripheral connections to one central. It should still be rejected, and not on grounds of effort:

- **It tests the wrong scheduler.** M4 exists to find out whether *central* scheduling of two links works. Two connections terminating on one peripheral radio adds a *peripheral*-side scheduling problem that does not exist in the product, on the node whose budget the product never constrains. A failure would be uninterpretable — peripheral contention and central contention produce the same symptom, and the product only has one of them. An unattributable failure at M4 propagates into the power budget and from there into the M6 board.
- **There is one antenna, in one place.** Cross-connection arrival skew (R5), body shadowing, spatial diversity and the 12 m link budget all require two devices in two positions. A single device cannot be shadowed from the dongle by one torso and not the other, which is the actual field condition.
- **The firmware is thrown away and diverges.** Dual-identity peripheral firmware is not remote firmware. The code M4 validated would not be the code M5 extends, which forfeits the reason for prototyping on real silicon at all.

**Why a spare dongle is sufficient, and why it does not need to be a full remote.** Remote #2's job in M4 is to hold a second connection, consume the downlink, generate uplink traffic at realistic rates, and report telemetry. It does not need seven buttons or a wrist. One button covers all three gestures, and a self-stimulus timer covers sustained load — the same trick `TEST 2` already plays on the dongle. Every question in §2.2's table is answerable with an asymmetric pair.

**Use the PCA10059 rather than the spare MDBT50Q-CX-40 for bring-up**, on a concrete difference confirmed in the board devicetree: the PCA10059 carries a green LED *and an RGB LED with all three channels on PWM*, where the MDBT50Q-CX-40 has two mono LEDs and one button (§4.2). That RGB is the only surface in the system before M5 that can render `DN_INDICATOR` colour at all — one of the three things §5.6 lists the DK stage as unable to claim. **Then swap in the spare MDBT50Q-CX-40 for the range and density measurements**, because those are antenna-and-module measurements and the MDBT50Q is the module the product is likely to carry. Two boards, two purposes, and the swap costs a flash.

**Would a second DK be justified?** Not for M4 — nothing in §2.2's table needs one, and the asymmetric pair answers all six questions. The honest case for a second DK is narrower and later: **two *haptic-capable* remotes**, for B6 as a felt experience rather than a routing assertion, B8's beat-and-tap collision, and R4's amplitude separation. Three points against buying one for that:

- Those three are wrist-and-strap questions. The variables that decide them are motor mass, mounting compliance and enclosure coupling — none of which a bare DK on a bench has, and all of which the M6 board and enclosure do. A second DK would not settle them; it would produce a number that gets re-measured at M7 anyway.
- You have one nPM1300-EK. A second haptic-capable remote means a second PMIC evaluation board and a second ERM before it means a second DK, so the DK is not even the binding purchase.
- R4 is a *perceptual* judgement, and the single most useful instance of it is one referee wearing one well-made remote. That is an M5 experiment with one DK.

So: **no second DK for M4, and probably not for M5 either.** Revisit only if the M6 spin slips far enough that two-wrist evaluation on breadboards becomes the critical path — and if that happens, price a second nPM1300-EK and ERM in the same breath, because a DK alone would not unblock it.

### 4.10 Provisioning is built for real at stage 2, with a bench tool rather than a process — **superseded 2026-08-12, §4.13**

`RADIO_PROTOCOL.md` §10.1 describes a record written once at manufacture. Read at bring-up time that sounds like permission to defer it, and it is not — it is stage 2, before any radio code, because nothing connects without it.

**Built now:** the record format, the CRC check, and the refuse-to-operate-unprovisioned path (A19) — code-complete 2026-08-12, `dongle/BUILD_SPEC.md` §9. **The LTK installation is stage 3**, not stage 2 — it needs `radio_ble.c` to exist, which reads `set_key` from the same record but doesn't yet. **Deferred:** the manufacturing process around it — a script generating a partition hex is sufficient and correct for three units, and `dongle/tools/provision.py` is that script.

A key compiled into the firmware as a `#define` would be faster and would make A12 (no key), A13 (set mismatch) and A19 (unprovisioned) untestable. Those three are most of what stands between this product and a cross-associated match at a multi-mat event, and FS §2.3 calls that a scoring-integrity failure rather than an inconvenience. A boot path added after the fact is also a boot path nothing ever exercised.

**This reasoning held for a manufacturing model this project doesn't have — §4.13 reverses it, on the same evidence that closed W0, not because the reasoning above was wrong for the model it assumed.**

### 4.11 `CONFIG_DONGLE_FAKE_LINK` is deleted, not defaulted off

It fabricated `LINK … CONNECTED` with a fixed RSSI and battery so the app's indicators could be exercised before a radio existed. That was reasonable then and is a hazard now: **it fabricates precisely the values every link test is trying to measure, and it does so plausibly.** §5.2 lists it as a standing trap and `RADIO_PROTOCOL.md` notes it invalidates four rungs while producing entirely believable output.

A Kconfig default is not protection against that, because the failure mode is forgetting, and a forgotten `y` produces a passing test. Deletion is. Its replacement is `radio_null` (`CONFIG_DONGLE_RADIO=n`), which reports both remotes `DISCONNECTED` — **which is true** — and renders the downlink on the board LEDs. The app shows two disconnected remotes, correctly, and no result needs a caveat attached to it.

The same trap exists on the remote wearing different clothes and with none of the visibility: the DK has no battery, so `UP_TELEMETRY.battery_pct` is synthetic, and **there is no Kconfig symbol whose name gives it away**. `remote/BUILD_SPEC.md` §8.3 requires the synthetic value to be obviously synthetic rather than plausible.

### 4.12 Implementation detail lives in the build specs, not here

[`dongle/BUILD_SPEC.md`](dongle/BUILD_SPEC.md) and [`remote/BUILD_SPEC.md`](remote/BUILD_SPEC.md) are the implementable contracts, written 2026-08-10. They answer to `PROTOCOL.md` and `RADIO_PROTOCOL.md`, which answer to `SCOPE.md` and `SYSTEM_FUNC_SPEC.md`.

The split is that **this document says what state the project is in and why the work is ordered as it is; the build specs say what to build.** Module boundaries, function-level interfaces, state layouts, algorithms and acceptance criteria belong there. When they and this document disagree about a mechanism, they are the more specific and they win; when they disagree about *sequence or status*, this document wins.

### 4.13 Provisioning identity is baked into the firmware image at build time — §4.10 reversed

Attempting W1's first hardware pass surfaced that `dongle/tools/provision.py`'s DFU write of the provisioning record to `storage_partition` doesn't reach it: the dongle's factory "Open bootloader" is a fixed-application-slot Serial DFU mechanism (confirmed against Nordic's own DFU documentation and the board's own dual-slot flash map, `dongle/BOARD.md` §4) — it activates whatever it receives into the application slot regardless of the address a hex file's own Extended Linear Address record claims, so every provisioning write was silently landing on top of the application rather than at `0xf0000`. That explained both symptoms at once: the app failing to enumerate right after a provisioning write (a 55-byte record isn't a valid image), and the record itself never actually changing.

The dongle is sealed — enclosure only exposes USB-C, no SWD probe on the bench, and opening it to reach test pads was ruled out as a standing operational step. That eliminates the fix that would otherwise apply (write `storage_partition` directly via SWD, bypassing DFU's application-slot limitation) as a real option for this hardware, not just an inconvenient one.

**Re-examined against §4.10's own reasoning and found it doesn't hold for this project's actual manufacturing model:**

- **One person builds and flashes every unit.** §4.10's argument assumed a build system decoupled from per-unit secrets; there is no such separation to protect here, and a `west build` per unit is not meaningfully more expensive than the two-step flash §4.10 chose instead.
- **No post-sale firmware updates are planned at all.** Firmware is meant to stabilize before a single unit ships; all further iteration happens in the scoreboard web app, which is the one piece with a real update channel. §4.10's strongest argument — a baked-in identity turns every future field update into a per-unit-secret operation — doesn't apply to a firmware that, by design, is never updated once sold.
- **The threat model is a youth/amateur grappling event, not an adversarial one.** Robust pairing (no cross-talk between sets at a multi-mat event) is still a real requirement and is unaffected by where the key lives; defending the key itself against a sophisticated attacker is not a stated requirement, and enclosed units built and flashed by one person were never exposed to the supply-chain threat §4.10's secrecy argument was really defending against.
- **Misprovisioning is now cheaper to fix, not more expensive.** Labelling each enclosure with its serial makes correcting a wrong unit a single rebuild-and-reflash — one DFU operation, using the one mechanism already proven to work over USB-C — rather than today's two-step dance, one leg of which turned out not to work at all.

**What changed, mechanically:** `common/provisioning.h`/`.c` dropped the CRC/magic/version wire-format machinery — corruption-in-transit is a risk that doesn't exist once the record is compiled into the same image as the code that reads it, and DFU's own transfer integrity check already covers the image as a whole. What's left, `provisioning_validate()`, is a structural sanity check (serial charset, role range, an all-zero `set_key` sentinel) run against an already-populated `struct provisioning_record`, not a parser for bytes off a fallible medium. `dongle/tools/provision.py` now emits a small generated header, `provisioning_data.h`, per unit — a `static const struct provisioning_record PROV_RECORD` literal — instead of an Intel HEX file targeting a flash address. `dongle/src/provisioning_flash.c` and `remote/src/prov_flash.c` are deleted outright; there is nothing left to read from flash.

**The safety property A19 protected — never operate with a meaningless identity — is kept, at two points rather than one, deliberately:**

- **Build-time, the primary guard:** `CMakeLists.txt` (both dongle and remote) refuses to configure at all without `-DCONFIG_PROVISIONING_HEADER_DIR=<dir>` pointing at a real `provisioning_data.h`. There is no default to silently fall back to — omitting it is a build error, not a boot-time state, which is the right place for it now: the failure mode this design can still produce is "the wrong build got flashed to this unit," a build-time/labelling mistake, not flash corruption.
- **Boot-time, belt and braces:** `engine.c` and `main.c` still call `provisioning_validate()` against `PROV_RECORD` before doing anything else, exactly where `provisioning_load()`/`prov_flash_load()` used to be called. This is what catches a header that exists but is wrong — a generator bug, a hand edit — the one thing the build-time check can't see.

**Consequence for validation:** A19 and A12/A13 remain testable exactly as before — generate a deliberately-wrong `provisioning_data.h` (bad role, mismatched set) and build against it, in place of generating a mismatched hex (queue S2–S3). W0 (`dongle/tests/rframe`) is unaffected; `dongle/tests/provisioning`'s 54 CRC/byte-layout checks are replaced by 9 checks against `provisioning_validate()` directly (`HISTORY.md` §9.2). Both firmwares rebuild smaller with the flash-reading machinery gone, and `CONFIG_FLASH`/`CONFIG_FLASH_MAP` are dropped from both `prj.conf`s — nothing else on either board reads a flash partition.

### 4.14 LE Flushable ACL Data stays off — confirmed available, and confirmed not free

`RADIO_PROTOCOL.md` §8.3's mechanism 3 names LE Flushable ACL Data as an available optimisation, marked experimental in v3.4.0, and explicitly not what the deadline guarantee rests on. S6 (2026-08-13) chased down what turning it on would actually cost, since "investigate" had never been followed up with a real answer.

**Confirmed present**: `CONFIG_BT_CTLR_LE_FLUSHABLE_ACL_DATA`, in the SoftDevice Controller since NCS v3.3.0 (nordic-mcp, `nrfxlib/softdevice_controller/CHANGELOG.html`), still experimental in v3.4.0. **Confirmed not a free switch**: the feature is listed only under the controller's **Multirole** column, never Central-only or Peripheral-only (`nrfxlib/softdevice_controller/README.html`'s feature table) — and enabling it is itself one of the OR-conditions that select `CONFIG_BT_LL_SOFTDEVICE_MULTIROLE` (`kconfig_diff.html`). The dongle's `prj.conf` sets no explicit controller-role Kconfig today, which is consistent with a Central-only resolution — meaning flipping this on would move the controller into a different role configuration entirely, with its own scheduling behaviour, not just add a flush timeout to the existing one.

**Decision: stays off through M2 and M4.** A controller-mode change is exactly the kind of thing that should be evaluated alongside M4's actual two-connection scheduling measurements (S9, W6), not folded into a one-line Kconfig flip at M2 on the strength of a single connection. Mechanisms 1 and 2 (§8.3) do not depend on it, and mechanism 1 is confirmed working end to end (S3–S5). Revisit only if a measured latency shortfall at M4 or M6 specifically implicates the transmit queue holding a stale frame — nothing so far does.

### 4.15 R3, R4 and R6 are design targets for M6, not gates on it — reverses the framing in §1/§2/§7 as they stood through 2026-08-13

Every prior version of this document treated R3 (ERM haptic range), R4 (`BEAT`/`TAP` perceptibility) and R6 (ten-hour battery life) as risks M5 would *measure*, closing them before M6 — the custom PCB — was allowed to start. That framing doesn't survive contact with what a DK-tethered bench rig actually is: a motor wired to a development board, on a bench strap, is not the enclosure, is not the production strap, and is not the production PCB's antenna or regulator. None of the three questions R3/R4/R6 ask can be fully answered by anything that exists before M6 ships a board, because the mechanical and electrical coupling each one depends on — housing damping, strap tension and material, antenna efficiency, regulator losses — **is what M6 produces**, not something available earlier to measure against.

**The reframing:** M5's rig produces the best estimate obtainable before the enclosure exists, and that estimate is what M6 designs the board against — a design target, not a pass/fail gate. Real closure happens at M7, once the actual PCB, enclosure and strap exist to measure against. If M7 finds an M5 estimate wrong — the wrong motor, insufficient battery headroom — that is a second PCB spin. **A second spin is an accepted, ordinary cost of this sequence, not a failure the old gate was trying to prevent.** The old framing's real effect was to hold M6 open indefinitely waiting for a confirmation that structurally could not arrive before M6 itself did — a gate that could never be satisfied honestly, only worked around by treating a provisional bench number as if it were final.

**What this does not change:** M5 still measures R3/R4/R6 as carefully as a bench rig allows — the estimate still has to be a real one, not a shrug — and M7's re-measurement against production-shaped hardware is still on the definition of done (§9), not optional. What changes is only whether M6 *waits* for a confirmation that was never going to be honest before M6 existed.

**Consequence for the queue:** M4 (§2.2) is trimmed to the minimum that establishes the 2:1 link actually works — S8 and a leaner S9 — with the deeper validation work (scripted reconnect tooling, range/density, dongle-swap and version-guard rungs, the overnight soak) moved to the parked table (§3) rather than sitting between "now" and M5/M6. The PCB fabrication wait after M6 ships is explicitly one of the windows that re-admits them, alongside M9.

### 4.16 Product-level validation moves to the PCB fab wait; build-out is the near-term line

§4.15 settled *what* R3/R4/R6 mean. This settles *when the rest of the ladder runs*, prompted directly: the development bottleneck is PCB fabrication lead time, not bench validation — the scoreboard app, the wire protocol, and the dongle↔DK radio link all already work. The stated intent is: prove the 2:1 link with dummy GREEN firmware, build out the DK into a full-feature remote model (buttons, haptics, nPM1300), design and order the PCB, run whatever validation makes sense **while the boards are in fabrication**, then port and run the full product-level validation once two remotes exist on their intended hardware.

**M2's exit criterion is the demonstration, not the edge-case matrix.** `PLAN.md`'s own definition of done (§9) only ever required "a physical press on the DK, scored on the scoreboard, acknowledged back as a rendered haptic" — S5's closing positive control already produced this informally. The W4/W5 walkthrough S6 wrote (A8, A10, the four-relationship supervision matrix) is real, correctly-designed validation, but it was never actually part of that criterion; it had accreted onto S7 as "since we're at the bench anyway." **Moved to §3 as S20**, unlocked by the PCB fab wait, with one case-by-case note: A10 specifically exercises `haptic.c`'s LED-proxy rendering, which M5 replaces wholesale — running it now would test code with no future, and running it again post-M5 is free once S16 is at the bench regardless.

**M5 is promoted from a coarse paragraph to real queue steps (§2.3, S13–S19)** — not because the old low-resolution treatment was wrong in principle (§2.3's own header still says "detail arrives when the predecessor closes," and M4 closing is exactly what triggered this), but because the user's stated intent makes M5 the immediate next build target, not a distant one.

**What stays near-term regardless:** S8/S9 (does the 2:1 link work at all — a scoring-integrity check, not a nice-to-have) and S13–S19 (M5's build-out) are not deferred by this decision. Only *validation whose absence doesn't block building the next thing* moves to the wait — the same test §4.15 already applied to R3/R4/R6, now applied to the rest of the ladder.

---

## 5. Validation reference

**There are two ladders.** V0–V8 test the USB link, W0–W8 (§5.3) test the radio. They are separate because they isolate different domains, and the rungs are worked in order — a failure high up is uninterpretable if a lower rung was skipped. **Rung and case names are stable identifiers**: the queue (§2) schedules them, this section defines them, and `HISTORY.md` §9.2 records every execution with date and firmware version.

The interface "works" in the sense that a happy path completed once. That is a much weaker claim than "reliable", and the gap between them is where this class of system fails: at hour three, on a cable pull, on a backgrounded tab, on someone else's laptop.

**Ladder status at a glance:** V0–V6 green (V4 row 7, V6.4, V6.5 parked — §3); V7, V8 never run (queued S11, S12, PCB fab wait). W0–W3 green; W4/W5 written and parked (S20, PCB fab wait); W6 basic case green (S9, 2026-08-13), full characterisation parked (S10–S12, PCB fab wait); W7–W8 never run (S11–S12, PCB fab wait).

### 5.1 Test rig

| Item | Value |
|---|---|
| Board target | `raytac_mdbt50q_cx_40_dongle/nrf52840` (dongle), `nrf52840dk/nrf52840` (remote) |
| SDK | nRF Connect SDK **v3.4.0** |
| Build | `tools/build_set.ps1` for a provisioned set; or `source dongle/tools/ncsenv.sh` then `west build` per `dongle/README.md` |
| Flash (dongle) | Hold the button while plugging in (LED fades), then DFU per `dongle/README.md` |
| App | `npm run dev` in `wrsl-app`, `http://localhost:5173/` |
| Browser | Chromium-based, desktop |

**The COM port is exclusive.** A serial terminal and the app cannot both hold it. Commands go to the dongle from the app's debug panel (`DetailPanel.jsx`, System tab) when the app has the port.

### 5.2 Standing traps

- **`TEST 3` suspends link supervision until `TEST 0` or reboot.** Left on, every supervision test in V5 passes for the wrong reason. Send `TEST 0` first and confirm the reply. With the dongle-side clock deleted, `TEST 3` is the only way to keep a bench terminal quiet, so it is reached for often.
- **`CONFIG_DONGLE_RADIO=n` means the link rungs are not being tested.** V1–V6 are wire-layer rungs and are valid in that configuration; anything about `LINK` state, RSSI or battery is not. The difference from the retired `CONFIG_DONGLE_FAKE_LINK` trap (§4.11): a null radio reports `DISCONNECTED`, which is true and visibly so.
- **A synthetic `battery_pct` from the DK has no Kconfig symbol to notice.** The same class of trap with none of the visibility — §4.11. Real only from M5.
- **Chrome DevTools' "Create live expression" cannot trigger the app watchdog (V5.7).** It catches a thrown exception internally and never dispatches it to `window.onerror` — the app sees nothing, with no indication the test itself failed. Use `window.dispatchEvent(new ErrorEvent('error', {message: '...'}))` from the console or a bookmarklet.
- **Building the radio rungs with `CONFIG_DONGLE_RADIO=n` invalidates all of them** — the check worth running is B3's: confirm `INFO` reports `DISCONNECTED` with no remotes powered, and that it does so because nothing is connected rather than because a symbol says so.

### V0 — Parser unit tests (host, no hardware) — ✅ green since 2026-08-11

`protocol.c` has no Zephyr dependencies precisely so this can run anywhere.

```bash
source dongle/tools/hostenv.sh
cd dongle/tests/protocol && make check
```

**Pass:** 131 checks, 0 failures — `PROTOCOL.md` §14 T1–T16, every button × gesture encoder round-trip, the `LINK` RSSI constraint. Results and the three cases worth naming (T7, T11, T13): `HISTORY.md` §2.6, §9.2. **`make check` is a remembered step** — nothing runs it for you; run it after any change to `protocol.c`.

### V1 — Manual terminal, no browser — ✅ green 2026-08-11 (all this board can show)

Disconnect the app first. With a terminal on the port at 115200 8-N-1:

| Send | Expect |
|---|---|
| `INFO` | `HELLO 3.0 <fw> <set> 0`, then one `LINK` line per remote |
| `PING` | `PONG` |
| `ECHO hello` | `ECHO hello` — verbatim, including runs of spaces |
| `STATE RED SOLID 00A0FF OFF 000000` | Indicator stand-in reflects it |
| `HAP BOTH LONG` | One long pulse |
| `CFG BOTH 50 50` | Accepted; subsequent haptics and LEDs at half scale |

**Pass:** all of the above, plus `ERR APP_TIMEOUT` ~2.5 s after the last typed line — which is **correct** (`PROTOCOL.md` §8) and exercises RX, parse, state transition, timer and TX in one message. Malformed lines rejected with a `LOG` naming the reason.

Results (measured 2512 ms supervision; haptic routing proven by the `RED`-dark/`GREEN`-lit asymmetry): `HISTORY.md` §2.7, §9.2. `CFG` and anything addressed to `RED` are structurally unobservable on this board and are covered at S5 on the DK (§3).

### V2 — App handshake — ✅ green 2026-08-11

Connect from the scoreboard. **Pass:** the app reaches `ready` with set serial, firmware version and protocol version matching the dongle's `HELLO` exactly, no version-guard warning, from a fresh port grant. Results: `HISTORY.md` §9.2.

### V3 — Deterministic stimulus — ✅ green 2026-08-11, both halves

`TEST 1` then `TEST 4` from the app's raw-command console; `TEST 0` after.

**Pass:** `TEST 1` — seven `EVT`s, one per button, all `PRESS`, alternating RED/GREEN, `seq` contiguous, every press acknowledged with the correct `ACK`/`SILENT` split for the loaded ruleset. `TEST 4` — **32 events, 16 per remote** (`HOLD_REP` on `FORWARD`/`BACKWARD` only), zero gaps, zero duplicates. **A sweep producing 21 is a defect, not a variant** — `HISTORY.md` §2.9. Results: `HISTORY.md` §9.2.

### V4 — Reverse path (app → dongle) — ✅ green 2026-08-11, 6/7 (row 7 parked — §3)

Driven from the scoreboard's own UI, with a ruleset carrying an active secondary clock and a shortened period.

| Action | Expect on the wire |
|---|---|
| Start the clock with a secondary clock owned | `HAP <owner> BEAT` once per second, on the owner only |
| Transfer secondary-clock ownership | Beats move to the other remote within one beat |
| Stop the main clock | Beats stop; no `STATE` change (ownership is retained) |
| Deassign the secondary clock | `STATE` for that remote with F1 `OFF` |
| Let a period run to 0:00 | One `HAP BOTH LONG` |
| Reach the configured main-clock warning | One `HAP BOTH WARN` |
| Enter a burst of four `ADD_POINT` presses during accrual | Four `ACK` lines and **no `BEAT`** inside the suppression window — *parked: not reachable from the operator UI (§3)* |

Results: `HISTORY.md` §9.2.

### V5 — Supervision and disconnection — ✅ green 2026-08-11, all checkable

The most important rung. `TEST 0` sent first.

| # | Procedure | Pass criteria |
|---|---|---|
| V5.1 | Clock running, secondary clock accruing; close the browser tab | Beats stop immediately; dongle emits `ERR APP_TIMEOUT` within 2.5 s and instructs both remotes to render link-lost — *the remote-render half is S7/A15 (§3)* |
| V5.2 | Unplug the dongle | App shows disconnected, stops the `PING` cadence, offers reconnect |
| V5.3 | Sleep the laptop 30 s and wake it | App recovers or cleanly reports a stale link; the match clock shows correct elapsed time or halts and asks (FS §8.1) — *15 s pinned; 30 s+ parked (§3)* |
| V5.4 | Kill the browser process outright | Same as V5.1 |
| V5.5 | Pull the dongle while idle | Clean disconnect, no spurious `ERR` |
| V5.6 | Reconnect after any of the above | Handshake re-runs from scratch **including `STATE` assertion**; indicators correct before anything else happens |
| V5.7 | Trigger the app watchdog (FS §8.3) via a dispatched `ErrorEvent` (§5.2 trap) | App drops the serial link deliberately; fault visible on the display; remotes render link-lost |

V5.3 is materially harder at v3.0 than at v2.0: the clock must be immune to wall-clock adjustment across a suspend, and an implausible gap must halt the clock rather than be absorbed. Results, including the 15 s port-handle-survival finding: `HISTORY.md` §9.2.

### V6 — Reconnect lifecycle — ✅ 3/5 green 2026-08-11; V6.4 → S10, V6.5 → S11

| # | Procedure | Pass criteria |
|---|---|---|
| V6.1 | Reconnect after an external disconnect | No OS port picker — `getPorts()` reuses the grant. (The app has no in-UI disconnect control; the disconnect half arrives externally via V5) |
| V6.2 | Unplug, replug, reconnect | Handshake re-runs in full |
| V6.3 | Set a score and a secondary-clock owner, disconnect, reconnect | **No match state is lost, and no dongle state is resumed.** Indicators reassert from the app's copy |
| V6.4 | Reconnect 10× in a row | No leaked readers or writers; no duplicate `PING` cadences. *Needs scripted observation — queued S10* |
| V6.5 | Mid-match, swap to a different dongle | Match state fully retained; new set serial displayed; `STATE` asserted to both new remotes before the clock restarts; the substitution appears in the match record. *Needs a second flashed dongle — queued S11* |

V6.4 finds real bugs — a reader lock or interval leaked per reconnect is invisible until it isn't. V6.5 is the field-substitution procedure of `SCOPE.md` §8.6 and is the reason `STATE` exists.

### V7 — Version guard — queued S11

Emit `HELLO 4.0 …`, flash, connect. **Pass:** the app refuses to operate and says to update the dongle firmware. Then `HELLO 3.1 …`: the app warns and **continues**. Revert afterwards. This is the only mechanism protecting against a mixed-firmware fleet — live the moment a second dongle exists.

### V8 — Soak — queued S12

`TEST 2`, with the app connected and supervision **on**. **Minimum 4 hours, preferred overnight.**

**Pass:**

- **Zero sequence gaps.** On 3 cm of USB there should never be one. A gap is a real finding.
- **Zero duplicate `seq` applied.** The dedupe counter may be non-zero; the applied-twice counter must be zero.
- Acknowledgement latency p99 well inside budget, and stable over the run.
- No RAM growth; no transmit-ring drops beyond `BEAT`.
- COM port never drops; the app never goes stale; the UI stays responsive.

**Do not judge scoreboard correctness during V8.** `TEST 2` fires at random and the board will look nonsensical by design. V8 measures throughput, `seq` integrity and latency; V3 is the behavioural test.

**Export the diagnostics JSON at the start of the soak as well as the end** — the counters are cumulative and monotonic, so a single reading at the end cannot distinguish a fault at hour one from a fault at hour four.

### 5.3 The radio ladder

The counterpart to the V-ladder, for `RADIO_PROTOCOL.md`. The `A`-references are the conformance cases of `RADIO_PROTOCOL.md` §14; the `B`-references are §5.4 and the `R`-references §7.

| Rung | Queue | What it isolates | Pass |
|---|---|---|---|
| **W0** ✅ | done 2026-08-12 | **Frame codec, on a host, no hardware** | A1–A7, A11, A20 green. The radio's V0 — `cd dongle/tests/rframe && make check`, 20 checks, 67 assertions. Re-run after any change to `rframe.c` |
| **W1** ✅ | done 2026-08-13 | **Association and security.** One connection, encrypted from the provisioned key, no pairing procedure performed | `RR_IDENTITY` read and validated, CCCD subscribed, `LINK … CONNECTED` with a real RSSI — positive case 2026-08-13. All four negative cases run 2026-08-13, each matching its predicted wire signature exactly: A12 (wrong key) — no `ERR`, connection never completes, only repeated `LINK … CONNECTING`; A13 (set mismatch) — `ERR SET_MISMATCH` + `LOG … set_serial mismatch`; A14 (proto major) — `ERR REMOTE_PROTO_MISMATCH` + `LOG … proto major mismatch`; A19 (unprovisioned remote) — nothing on the wire at all, all four DK LEDs blinking. Positive control reconfirmed afterward, including live button presses reaching the scoreboard. `HISTORY.md` §9.2 |
| **W2** ✅ | done 2026-08-13 | **Uplink.** Button → `UP_INPUT` → `EVT` → scoreboard | All three gestures on the four DK buttons (§5.6) confirmed on the wire, `HOLD` firing on threshold-cross not release, non-repetition proven on `TOGGLE_CLOCK`, `HOLD_REP` firing repeatedly on `FORWARD` and stopping on release. **Exact cadence and A6 (wrap) not forced this session** — §3, parked |
| **W3** ✅ | done 2026-08-13 | **Downlink.** `STATE` → `DN_INDICATOR`, `HAP` → waveform, `CFG` → scaling | Indicators assert and clear correctly; **`ACK … SILENT` puts nothing on the air** (A20), confirmed by reading the wire log, not by watching an LED that was never going to light; `HAP RED` reached a fitted lamp for the first time, `TAP`/`BEAT` distinctly separated; `CFG` scaling visibly reduces amplitude |
| **W4** | S20, PCB fab wait | **Round trip and the deadline rule** — not required to close M2 (§4.16) | The app's own EVT→ACK turnaround measured as a distribution (not the full radio-inclusive round trip — neither leg has a wire timestamp, `HISTORY.md` §9.2). A8 a late `ACK` is not sent at all, `taps_dropped_late` non-zero when provoked and zero when not. A10 a `BEAT` never truncates a `TAP` — re-run against M5's real ERM, not the LED proxy. **A9 excluded — mechanism 2 is not implemented, §8** |
| **W5** | S20, PCB fab wait | **Link state, in all four supervision relationships** (`RADIO_PROTOCOL.md` §9.1) — not required to close M2 (§4.16) | **A15 is the rung** — app supervision expires, radio stays up, both remotes render link-lost. Also A16 boot-is-DOWN, A17 sub-2 s reconnect emits no `DISCONNECTED`, A18 press out of range, A7 reboot re-baselines with no false gap, and §9.4 debounce under repeated power-cycling at the range edge (B4) |
| **W6** ◐ | basic case done — S9, 2026-08-13 | **Two connections.** The rung M4 exists for | **Basic case (S9): both connections hold, routing (B6) confirmed correct including after a colour swap.** Full characterisation — 7.5 ms interval cleanliness, event-length overruns, R5 skew measurement, B2 transmit-ring pressure — stays parked, S10–S12 (§3) |
| **W7** | S11 | **Range, link budget and density**, at the dongle **as deployed** | 12 m with body shadowing, dongle in a laptop port below table height — not a bench with line of sight. p99, not median. **Take this rung with the MDBT50Q-CX-40 as remote #2** (§4.9). Escalation order if it does not close is fixed: USB extension cable, then a placement constraint in the documentation, then transmit power |
| **W8** | S12, re-run M7 | **Radio soak, and the §5.4 regression list in full** | ≥4 h with both remotes connected and pressing. `radio_gap` and `radio_dup` accounted for rather than merely observed; no transmit-ring drops beyond `BEAT`; B1 workqueue contention re-measured with the radio live; V4 and V5 re-run underneath it |

**The link budget is taken at the dongle as deployed.** The MDBT50Q-CX-40 carries a PCB trace antenna and sits in a USB port on a laptop at the scoreboard table: close to the host's own 2.4 GHz radios, often below table height, frequently with bodies between it and the mat. W7's escalation order above exists because of this, in that order of preference.

### 5.4 The radio regression list — how the radio can regress the USB link

**Read this before writing radio code, not after.** The USB interface was validated in an environment with no radio. Adding one can degrade it without touching a line of USB code. Every item is diagnosed by asking *does this still happen with `CONFIG_DONGLE_RADIO=n`?* — the baseline that configuration exists to preserve.

| # | Risk | Test |
|---|---|---|
| B1 | **Workqueue contention.** Every engine timer and the RX drain run on the engine workqueue (§4.6). Radio work on the same queue delays them. Shows up as acknowledgement latency, not as an error. | Re-run V4 and V5 with the radio active and both remotes connected. Measure the `EVT`→`ACK`→tap path, p99. |
| B2 | **Transmit ring saturation.** The TX ring is 1024 bytes and drops whole lines when full. Two remotes at 1 Hz heartbeat, plus 10 s `LINK` re-emission, plus event traffic, raises the line rate well above bench conditions. | Run V8 with both remotes connected and pressing. The drop path is instrumented with a counter — watch it. |
| B3 | **Fabricated link state.** `CONFIG_DONGLE_FAKE_LINK` reported synthetic `CONNECTED` while real remotes were disconnected. **Deleted** (§4.11), which closes this by construction rather than by discipline. | Confirm `INFO` reports `DISCONNECTED` with no remotes powered — and that it does so because nothing is connected, not because a symbol says so. |
| B4 | **`LINK` state churn.** Real connections flap at the edge of range. Each transition is a line, and the app renders link loss as a primary-tier alarm. | Power-cycle a remote repeatedly at the edge of range. Confirm no flood and no strobing indicator. |
| B5 | **Real RSSI and battery values.** Bench values are constants. Real ones can fall outside the ranges the app accepts, and an out-of-range value is dropped silently. | Verify at various distances and charge levels. |
| B6 | **Acknowledgement routing.** The acknowledgement must reach the **originating remote only**, routed by the `src` recorded against that `seq`. A broadcast tap is indistinguishable from a correct one in single-remote testing and wrong in every real match. | Press RED and GREEN in quick succession; confirm each tap lands on the correct wrist. |
| B7 | **The 120 ms budget now includes two radio hops.** This is the risk most likely to bite in a real match. | Re-measure after the radio lands. Distribution, not median. |
| B8 | **Heartbeat contention with acknowledgement.** The beat and the tap share one motor, and a four-press burst takes about a second, so they *will* collide (FS §11.1). | Confirm burst suppression in the app, and confirm amplitude separation on real hardware with a real strap. |

### 5.5 The dongle emulator — the reference instrument

`wrsl-app/src/emulator/` implements the dongle half of `PROTOCOL.md` v3.0 in JavaScript and drives it over a real serial link, with an interactive mockup of **both remotes**: seven pressable buttons per remote carrying the firmware's gesture timing, four indicators rendering `STATE`, and a haptic motor showing waveform and amplitude. The scoreboard connects through its ordinary Web Serial path and cannot distinguish it from firmware.

**Its role is the reference trace.** Run a scenario against the emulator, run the same scenario against the firmware, diff the wire logs (`dongle/tools/wirediff/`). A difference is a firmware defect, localised to one segment. The first run (2026-08-11) was clean — 39 of 39 `EVT` lines identical, every remaining difference accounted for — `HISTORY.md` §2.7. It is wanted again every time the firmware changes underneath the wire layer.

**What it validates:** the whole application against an executable copy of the protocol — handshake, supervision and recovery, `JOIN` → forced `STATE`, indicator assertion per remote, acknowledgement routing by `src`, the inert/no-op distinction, the beat-on-owner-only rule and burst suppression, and the real Web Serial path including reconnect.

**What it does not validate, and must not be read as validating:** anything about firmware timing (desktop-grade, does not bound R2), anything haptic on a wrist (R3, R4), the radio (B4–B8), or real link state.

**Running it.** `npm run dev` in `wrsl-app`, then the emulator at `/emulator.html` and the scoreboard at `/?anyport`, each holding one end of a virtual serial pair. Use **Free Virtual Serial Ports**, not com0com (unmaintained 2017 driver signature, Code 52 on current Windows). Because the halves talk over the serial pair rather than HTTP, a **deployed** scoreboard can be pointed at a local emulator — the honest way to run D1 and D7 against the artefact that shipped.

**Standing obligation:** `dongleModel.js` is a third place the wire contract lives, alongside `PROTOCOL.md` and `DongleService`. A protocol change that skips the model validates the application against a contract the firmware will not honour. Two known model gaps are parked (§3): no `LOG counters` lines, and `ECHO` space collapse.

### 5.6 The DK reduced surface — what stage-4 rungs can and cannot claim

The nRF52840 DK offers **four buttons and four single-colour LEDs** (one on PWM); the remote specifies **seven buttons and four RGB indicators** plus an ERM (FS §3.1, §3.2). The mapping (`remote/BUILD_SPEC.md` §2) covers **gesture and semantic coverage, not button coverage**: `ADD_POINT` for `PRESS`, `TOGGLE_CLOCK` for `HOLD`, `FORWARD` for `HOLD_REP`, `F1` for the inert/no-op distinction. `REMOVE_POINT`, `BACKWARD` and `F2` wait for M5 as real GPIO — **a shift or bank modifier to reach seven from four is rejected** (a second input path; the same objection `HISTORY.md` §2.3 records against an operator shortcut, one layer down). The haptic proxy is on **LED 1** — the stock DK devicetree PWMs only `led0` (`HISTORY.md` §2.9).

**Three things the DK rungs therefore cannot claim:**

1. **Indicator colour.** `DN_INDICATOR` carries RGB per indicator; single-colour LEDs show mode only. Verify the colour fields over RTT; the rendering itself is unvalidated until the PCA10059 (S8–S9) and M5.
2. **`LED_PWR`.** The DK is bus-powered; `UP_TELEMETRY.battery_pct` is synthetic until M5 — §4.11's trap with no Kconfig symbol to notice.
3. **Anything haptic.** LED brightness is not amplitude. R3 and R4 are untouched until M5.

**Bench diagnostics:** the dongle has no diagnostic channel but protocol `LOG`/`ERR` lines (§4.4) and that is kept — its traffic is fully observable at the app end. **RTT goes on the DK only**, where the onboard debugger makes it free. Converting a product dongle to RTT costs the stock bootloader and with it the flash procedure every document assumes; if one ever becomes necessary, dedicate and label one dongle permanently rather than converting and reverting the unit under test.

---

## 6. Deployment validation — **NOT RUN**

The product ships from a website onto organisation-managed computers. That introduces failure modes no bench test reveals.

| # | Test | Pass criteria |
|---|---|---|
| D1 | Serve over real HTTPS (not localhost) | Works. Plain `http://` on a LAN IP will **not** |
| D2 | Chromium-based browsers, **on a stock profile** (`HISTORY.md` §2.4) | Connects. Non-Chromium degrades with a clear message rather than a broken page |
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

**Test:** with `TEST 2` as background load, pair each `RX EVT … <seq>` with its `TX ACK <seq>` and take the difference. **Measure the distribution, not the median** — p99 is what matters. The app's ack-latency counters make the app share measurable now.

**Fail:** any sample approaching the app's 25 ms allocation, or any total approaching 120 ms once the radio is in the path.

**Why it matters:** exceeding the window means no tap. The referee follows the rule correctly — *no tap means the press did not land, press again* — and scores twice. The failure is silent and looks like referee error.

### R3 — The ERM has to cover the whole haptic range — **design target, closed at M7, §4.15**

FS §3.3 assumes one ERM plus driver IC delivers unmistakable expiry amplitude, a countable reduced-amplitude heartbeat, and an acknowledgement inside 120 ms. Motor spin-up alone is allocated 20 ms of the budget and is the hard floor. This cannot be settled from datasheet figures, **and it cannot be fully settled from a DK-wired breadboard motor either** — the mechanical coupling that actually delivers the sensation (housing, strap material and tension) doesn't exist until the enclosure does. M5 measures the best rig that can exist before then and treats the result as a design target, not a closed question; M7 is where it closes for real, against the actual enclosure and strap. Contingency is an LRA or a dual-motor revision, which affects enclosure and cost — cheaper to discover at M7 than to have guessed wrong at M6, but M6 has to design against *something*, so M5's estimate is what it gets.

### R4 — Amplitude separation has to be perceptible — **design target, closed at M7, §4.15**

Whether the difference between `BEAT` and `TAP` is reliably distinguishable on the wrist, in motion, through a strap, by a referee not attending to it. This is the assumption repeated-press scoring rests on (FS §11.1, §15.5), and it fails quietly: a referee who miscounts a near fall has no way to know. Same limitation as R3: a bench rig can confirm the waveform table's *character* is right, not that it survives a real strap on a moving wrist. M5's confirmation is provisional; M7's is the one that counts.

### R5 — Event ordering under rapid exchange

Order of receipt is authoritative, assuming referee input intervals comfortably exceed transit variance. Log inter-press intervals during live matches against measured transit jitter. If it fails, remote-side sequencing is required, which adds protocol complexity (FS §15.1). Unlike R3/R4/R6, this is a radio-timing property rather than a mechanical or enclosure-dependent one, so it is measurable meaningfully before the custom PCB exists — the DK and PCA10059/MDBT50Q-CX-40 stand-ins carry the same radio stack.

### R6 — Ten-hour battery life — **design target, closed at M7, §4.15**

Measure average current attributable to the radio at the connection cadence the acknowledgement budget requires, and to a reduced-amplitude 1 Hz beat over a representative match. The motor is expected to dominate. Heartbeat suppression is **not** available as an unconditional mitigation, because the beat carries the running/paused distinction — any reduction in beat density must be accompanied by the LED taking that distinction over, which is the basis of the planned power-saving mode (FS §11.2). **A DK-tethered current measurement is a component-level estimate, not a system one** — real draw depends on the production PCB's antenna efficiency and regulator losses, neither of which exist until M6 ships a board. M5 gives M6 a sizing target with headroom (§4.8); M7's ten-hour session is the actual measurement.

---

## 8. Known gaps and debt

Gaps that are already scheduled point at their queue step or parked row rather than repeating the narrative.

| Item | Impact |
|---|---|
| **Fault indication is invisible on the dongle** | `indicator_error()` drives the unfitted P0.08, so **no `ERR` produces any visible signal** — `APP_TIMEOUT` included. Kept deliberately (`dongle/BOARD.md` §2.2): errors already report on the wire, and moving them to the fitted lamp would destroy the asymmetry that proves routing. **"No blink" never means "no error"** |
| **Every latency figure in `RADIO_PROTOCOL.md` §12 is arithmetic** | Predictions from documentation, never near this hardware. **Load-bearing**: §4.8 chooses rung 3 partly *because* the retransmission rate at 12 m is unmeasured, so measuring it (W7) is what would reopen the decision. R2 |
| **Host suites are a remembered step** | `make check` is not wired into `west build` and there is no CI — nothing fails if it is skipped. §3 |
| **Emulator gaps** — no `LOG counters`, `ECHO` space collapse | §3. The app's counter path is never exercised against the emulator, the one place it is cheap to exercise |
| **A9 (`RADIO_PROTOCOL.md` §8.3 mechanism 2, "replace rather than append") is not implemented** | `radio_ble.c`'s own comment on `radio_send_haptic()` says so directly: Zephyr's GATT/ATT layer offers no way to cancel a queued Write Without Response once submitted, so a second `ACK` cannot actually bump a first `TAP` out of the controller's transmit queue. Found 2026-08-13 while preparing W4's provocation procedures — not by running A9 and failing it. **Not currently blocking**: RP §8.3 itself frames mechanism 1 (implemented, confirmed on hardware) as the one the guarantee rests on, and the residual exposure mechanism 2 would close is bounded to at most one connection interval (7.5 ms) of an already-late frame. A9 is dropped from W4's pass criteria until a design for it exists — it is not silently being called green |
| **No DFU strategy for the remotes** | Absent from every document, and `RADIO_PROTOCOL.md` §10.3 makes it consequential. **Decide at M6, at the latest** — §3 |
| **No hardware design work of any kind** | Module, ERM and driver, PMIC production part, battery sizing, button mechanics, enclosure, APPROTECT. M6, and its inputs are M4 and M5 measurements — §2 |
| **Rulesets not verified against current rulebooks** | The library is implementation-accurate, not authoritative. **Before any real match, and again each rules cycle** — §3 |
| **USB identity is Zephyr's test VID/PID; `requestPort()` has no `filters`** | Blocks the enterprise deployment path (D5); users can select the wrong serial device. §3, long-lead |
| **Connection errors surface raw DOMException text** | A policy block is indistinguishable from a cancelled picker (D4). §3 |
| **Heartbeat survives only in the foreground** | Wake lock and banner applied; the Web Worker that would actually keep it running is not. R1's test decides — §3 |
| **Haptics and indicators unvalidated on the surface that carries them** | No real ERM, RGB or strap until M5, and even M5's rig is a design-target estimate, not closure — R3/R4/R6 don't close until M7, against the real enclosure (§4.15) |
| **Design-system fonts fetch from Google Fonts** | Survivable (pre-event load caches; system-face fallback), but self-hosted `.woff2` is the correct fix when licensed binaries exist. §3 |

---

## 9. Definition of done

- [x] Application at v3.0, tested and browser-verified (M1)
- [x] Soak instrumentation exists — counters and diagnostics export
- [x] Build specs written — `dongle/BUILD_SPEC.md`, `remote/BUILD_SPEC.md` (§4.12)
- [x] Host C compiler installed and `dongle/tools/hostenv.sh` recorded — 2026-08-11
- [x] **V0 green** — 131 checks, 0 failures, 2026-08-11
- [x] **W0 green** — `dongle/tests/rframe`, 20 checks (67 assertions), 0 failures, 2026-08-12
- [x] V1–V6 pass at v3.0 with `CONFIG_DONGLE_RADIO=n` — the no-radio baseline, **and it stays re-runnable** (parked residue: §3)
- [ ] Both host suites part of the routine build rather than a remembered step — **still remembered** (§3)
- [x] W1 pass *including* its negative cases — positive case green 2026-08-13, all four negative cases green 2026-08-13
- [x] W2 and W3 pass, including A20 — 2026-08-13
- [x] **The demonstration:** press on the DK → score on the scoreboard → tap rendered on that DK, and on that DK only — 2026-08-13. M2's actual exit criterion (§4.16)
- [ ] W4–W5 pass, with A15 (A9 excluded, §8) — **S20, PCB fab wait** — not required for M2 (§4.16)
- [x] W6 (basic case) pass on two connections — 2026-08-13; W7 (range/density, full latency-figure recording) — **S11, PCB fab wait**
- [ ] The DK models a complete remote — seven buttons, four RGB indicators, real ERM, real battery — **S19**. Closes M5
- [ ] V8 clean for ≥4 h with zero sequence gaps and zero applied duplicates; W8 clean with both remotes connected, `radio_gap`/`radio_dup` accounted for — **S12, PCB fab wait**
- [ ] R1, R2, R5 measured, with mitigations applied where they fail — before M6 opens (§2)
- [ ] R3, R4, R6 given a design-target estimate at M5 for M6 to build against — **not** a gate; closed for real at M7 (§4.15)
- [ ] D1–D8 pass; real VID/PID assigned and `requestPort()` filtered — M9
- [ ] Every ruleset in the library checked against the published rulebook for the current cycle — M9
- [ ] §5.4 re-run in full after the radio lands (S12), and again at M7 on custom hardware
- [ ] `PROTOCOL.md` amended for any further constraint that proves real; `RADIO_PROTOCOL.md` likewise, and its §12 predictions replaced by measurements
- [ ] **The rung 3 decision revisited against a measurement** — either confirmed by a p99 that closes 25 ms at 12 m through a torso, or reopened in favour of SCI (§4.8)
