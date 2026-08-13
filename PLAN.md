# RefRemote — Plan, Status and Validation

**Status as of 2026-08-13.** `SCOPE.md` and `SYSTEM_FUNC_SPEC.md` are the authority on direction; `PROTOCOL.md` **v3.0** and `RADIO_PROTOCOL.md` **v1.0** answer to them. The scoreboard application is at v3.0 (M1, complete). The dongle firmware is at v3.0, flashed and answering on hardware, with the no-radio baseline (V0–V6) closed. The radio is implemented on both sides and **confirmed on hardware, W1 closed**: a DK remote (RED) connects to the dongle over BLE, encrypted from the provisioned key, and reports `CONNECTED` to the scoreboard, and all four negative cases (A12–A14, A19) refuse exactly as specified. **The next step is S5 in the work queue (§2) — hands on hardware.**

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
| **M2** | **The firmware programme** — dongle wire v3.0, the radio, and a DK remote, to an end-to-end demonstration | ▶ in progress — queue §2.1. Wire layer, provisioning and W1 (both cases) all green on hardware; W2–W5 remain. History: `HISTORY.md` §2.6–§2.8, §9.2 |
| **M3** | *Retired as a separate milestone — absorbed into M2* | number retired, not reused — see below |
| **M4** | The **2:1** link — two peripherals, two connections, one central | queue §2.2 |
| **M5** | Full-feature remote firmware on the DK — GPIO buttons, RGB indicators, ERM, nPM1300 | §2.3 |
| **M6** | Custom remote PCB designed | §2.3 — **gated on R3, R4, R6 measurements** |
| **M7** | Firmware ported to the custom remotes; system validated on production-shaped hardware | §2.3 |
| **M8** | Custom dongle, for BOM cost — **optional** | §2.3 |
| **M9** | Deployment validation and MVP hardening — USB identity, ruleset verification, §6 and §8 | §2.3 — runs alongside from M5 onward |

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
| 3 | **M4** — S8–S12 | The **second** connection. A failure is central scheduling, routing or skew — nothing else changed | + PCA10059 as remote #2, then the spare MDBT50Q-CX-40 (§4.9) |
| 4 | **M5** | The remote's real peripherals — seven buttons, RGB, an ERM, a PMIC. A failure is hardware or drivers, not protocol | + GPIO harness, ERM, nPM1300-EK |
| 5 | **M6** | Nothing runs. Schematic, layout, BOM, enclosure — **inputs are M4 and M5 measurements** | — |
| 6 | **M7** | The custom board. A failure is the port or the board | Custom remote PCBs |
| 7 | **M8** | The custom dongle. Optional, cost-driven, deliberately last | Custom dongle |

**Two ordering constraints run backwards through this table:** M6 cannot start before R6 has a number (battery capacity is a PCB decision and the connection interval is its dominant input), and cannot start before R3 and R4 have an answer (the dual-haptic contingency of `SCOPE.md` §9.2 must resolve while it is still a breadboard question). §7.

### 2.1 Finishing M2 — one connection, ending in the demonstration

**M2's exit is the end-to-end demonstration:** a physical press on the DK, scored on the scoreboard, acknowledged back as a rendered haptic on that DK, and on that DK only.

- [x] **S1 [AGENT] — Implement the A13 check.** `dongle/src/radio_ble.c` `handle_identity_read()` compares `RR_IDENTITY`'s reported `set_serial` against the dongle's own `PROV_RECORD.set_serial` and produces `ERR SET_MISMATCH` + disconnect on mismatch (`RADIO_PROTOCOL.md` §10.2, §14 A13). Rebuild all three configurations; both host suites green. *Closes the §8 A13 gap.* ✅ 2026-08-13 — `HISTORY.md` §9.2.
- [x] **S2 [AGENT] — Prepare the W1 negative cases.** Generate deliberately-wrong provisioning headers and DK builds: A12 (wrong key), A13 (mismatched set serial), A14 (protocol major mismatch), A19 (all-zero key, remote side). `provision.py`/`build_set.ps1` already support building against an arbitrary header (§4.13). Write the flash-order walkthrough and per-case expected observations into S3's entry here. *Needs: nothing but the toolchain.* ✅ 2026-08-13 — `HISTORY.md` §9.2.
- [x] **S3 [BENCH] — W1 negative cases on hardware.** ✅ 2026-08-13, all seven steps run in order, every case matching its predicted wire signature exactly — `HISTORY.md` §9.2. **Closes W1.**
- [x] **S4 [AGENT] — Fixes from S3; prepare W2/W3.** Nothing from S3 needed fixing — all seven steps passed on the first attempt, so this step is procedure prep only. Walkthrough written into S5 below. ✅ 2026-08-13.
- [ ] **S5 [BENCH] — W2 uplink + W3 downlink.**

  **W2 — uplink, one DK button at a time (`remote/BUILD_SPEC.md` §2):**

  1. Button 1 (`ADD_POINT`, P0.11), quick press. *Expect:* `EVT ADD_POINT PRESS RED <seq>` on the wire, score increments on the scoreboard, `ACK <seq>`, and a full-brightness `HAP RED TAP` pulse on LED1.
  2. Button 2 (`TOGGLE_CLOCK`, P0.12), quick press (< 600 ms). *Expect:* `EVT TOGGLE_CLOCK PRESS RED <seq>` — confirms `PRESS` requires release before the threshold. Then hold *past* 600 ms and time it: `EVT … HOLD …` must fire **the instant the threshold crosses, not on release** (`remote/BUILD_SPEC.md` §4 rule 1) — watch the clock action land while the button is still down. Hold the same button 5 s straight: expect **exactly one** `HOLD`, never a repeat — the one button on the DK that proves non-repetition (rule 3).
  3. Button 3 (`FORWARD`, P0.24), hold past 600 ms and keep holding. *Expect:* one `HOLD`, then `HOLD_REP` at a **measured** ~150 ms cadence — time ten of them, don't eyeball it — for as long as it's held, stopping immediately on release.
  4. Button 4 (`F1`, P0.25), quick press, **with a ruleset loaded that leaves F1 inert** (folkstyle NFHS — `PROTOCOL.md` §5.2's own example). *Expect:* `ACK <seq> SILENT` and **nothing else** — no `HAP`, no `STATE` line at all. Confirmed by reading the wire log line by line, not by watching LED1 (A20) — the entire point is that a lamp which was never going to light proves nothing.

  **A4/A5/A6, reconsidered while preparing this:** A4 (duplicate `CTR`) and A5 (gap) are Link-Layer retransmission/loss artifacts, not something a button press on a 3 cm bench link reliably produces on demand — they stay covered by W0's host-suite arithmetic until an actual radio-degradation opportunity exists (W7, range). **A6 (wrap) is the one of the three actually reachable here:** hold `FORWARD` continuously for ~40 s (255 × 150 ms ≈ 38 s) to walk `CTR` through 255→0, and confirm no gap is logged at the wrap boundary.

  **W3 — downlink, via the app's raw command console** (`DetailPanel.jsx`'s System tab — the same one V1–V3 used, since the exclusive COM port rules out a bench terminal running alongside the app):

  5. `STATE RED SOLID 00A0FF OFF 000000` → LED2 on; `STATE RED OFF 000000 OFF 000000` → LED2 off. Confirms assert-whole-state end to end. Colour stays unobservable here by design (§5.6 — mode only on a single-colour LED); `DN_INDICATOR`'s actual encode-idempotence (A11) is already host-suite-covered (`dongle/tests/rframe`), so this step is confirming the state machine around it, not re-proving the encoder.
  6. `HAP RED TAP` → LED1 full-brightness pulse. `HAP RED BEAT` → LED1 pulse **distinctly dimmer**. This is the first time `HAP RED` has ever reached a fitted lamp — the dongle's own RED channel is permanently unfitted (`dongle/BOARD.md` §2) — closing that half of the parked observability row (§3) for good, not just for this session.
  7. `CFG BOTH 20 60`, then repeat `HAP RED TAP` — confirm the pulse is visibly dimmer than at the default scale, closing the other half of the same parked row: `CFG` scaling was equally unobservable on the dongle's own board.
  8. **What none of this proves:** LED brightness is not amplitude on a wrist — R3/R4 stay open until M5's real ERM, and colour/`LED_PWR` stay synthetic/unobservable until M5 too (§5.6). This closes an *observability* gap in the bench rig, not a hardware-fidelity question.

  *Pass criteria: §5.3 rungs W2, W3.*
- [ ] **S6 [AGENT] — W4/W5 instrumentation and procedures.** Latency-distribution capture for the `EVT`→`ACK`→render round trip (the app's ack-latency counters exist — `HISTORY.md` §2.3; add whatever pairing/export the distribution needs). Provocation procedures for A8 (late `ACK` never sent), A9 (second `ACK` replaces), A10 (`BEAT` never truncates a `TAP`). The four-relationship supervision matrix walkthrough for W5. Investigate in passing: is LE Flushable ACL Data usable in v3.4.0? (The deadline rule must hold without it — §4.8.)
- [ ] **S7 [BENCH] — W4 round trip + W5 link state; the M2 demonstration.** Deadline rules provoked and observed (`taps_dropped_late` non-zero when provoked, zero when not); **A15** — app supervision expires, radio stays up, the remote renders link-lost (picks up the parked V5.1 remote half, §3); A16–A18, A7 reboot re-baseline, §9.4 debounce under power-cycling at the range edge (B4). Then the demonstration, run and recorded. *Pass criteria: §5.3 rungs W4, W5. Closes M2.*

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

- [ ] **S8 [AGENT] — PCA10059 second-remote firmware (GREEN).** The minimal peripheral of §4.9: hold a connection, consume the downlink, generate uplink at realistic rates via one button plus a self-stimulus timer (the same trick `TEST 2` plays on the dongle), report telemetry. Its RGB LED renders `DN_INDICATOR` **colour** — the only surface before M5 that can. Board target `nrf52840dongle/nrf52840`, GREEN provisioning from the set.
- [ ] **S9 [BENCH] — W6, two connections.** GREEN's W1 positive case rides along. 7.5 ms clean on the pair — no dropped connection events, no event-length overruns, interval reported in the setup `LOG` line. **B6: taps land on the originating remote only** — press RED and GREEN in quick succession. Beat on the owner only, from real hardware. R5 skew first measurement. B2 with the drop counter watched. *Pass criteria: §5.3 rung W6.*
- [ ] **S10 [AGENT] — Scripted V6.4 + soak tooling.** The 10× reconnect test with programmatic observation (drive the reconnect, read `beatsSent`/wire-log timestamps — its unlock condition, §3, is now met). Verify the V8/W8 soak instrumentation end to end: diagnostics export at start *and* end, counters monotonic.
- [ ] **S11 [BENCH] — W7 range and density; second-dongle rungs.** Swap the spare MDBT50Q-CX-40 in as remote #2 (§4.9 — this is an antenna-and-module measurement). 12 m with body shadowing, dongle in a laptop port below table height, p99 not median. Escalation order if it does not close is fixed: USB extension cable, then a placement constraint in the documentation, then transmit power. Same session, with a second flashed dongle in hand: **V6.5** (mid-match dongle swap — the field-substitution procedure of `SCOPE.md` §8.6) and **V7** (version guard). *Pass criteria: §5.3 rung W7, §5 rungs V6.5/V7.*
- [ ] **S12 [BENCH, overnight] — V8 + W8 soak, and the regression list.** ≥4 h with both remotes connected and pressing; `radio_gap`/`radio_dup` accounted for; no transmit-ring drops beyond `BEAT`; B1 workqueue contention re-measured with the radio live; V4 and V5 re-run underneath it; the full §5.4 list walked. Also the deferred full-ladder QC re-run this hardware finally allows. *Pass criteria: §5 rung V8, §5.3 rung W8.*

**One trap on M4 numbers:** a one-connection latency figure is not a two-connection latency figure. Do not carry a W4 number forward past W6.

### 2.3 Later phases — coarse until M4's measurements exist

Planned deliberately at low resolution; detailing them now would re-create the deferral noise this document was restructured to remove. Each gets its own queue steps when its predecessor closes.

**M5 — the full-feature remote, on the DK.** The DK's GPIO carries what its onboard peripherals could not: seven buttons in the FS §3.1 layout (debounce 15 ms), four RGB indicators rendering `DN_INDICATOR` in colour, an ERM + driver IC with the full waveform table, `DN_CONFIG` scaling that **preserves the `BEAT`:`TAP` ratio** (`RADIO_PROTOCOL.md` §7.3), nPM1300-EK integration (charge, fuel gauge, regulator, USB-C) retiring the synthetic `battery_pct` — the last fake value in the system — and the remote-local behaviours with no wire representation (FS §10.2). **M5 is where R3, R4 and R6 are settled, and settling them is an exit criterion, not a nice-to-have** — all three are inputs to M6, each with a hardware contingency behind it. Measure with the motor on a strap on an actual wrist, wired back to the DK: the motor's mounting is the variable that matters, and it is the one thing that can be made representative early.

**M6 — the custom remote PCB.** No firmware runs. Inputs are the M4 and M5 measurements; output is a board. Nothing about the hardware design is recorded anywhere in this repository yet. Open items at least: module selection (an MDBT50Q variant keeps the RF characterisation), the ERM and driver chosen at M5 or the dual-motor contingency, nPM1300 as the production part, battery chemistry and capacity sized from R6 with headroom (§4.8), the FS §3.1 button mechanics and oversized `TOGGLE_CLOCK` datum, four RGB indicators adjacent to their buttons, USB-C charging, APPROTECT as a manufacturing step, and **a DFU strategy for the remotes, which exists in no document** (§8).

**M7 — port and validate on custom hardware.** The firmware is M5's with the board layer swapped; M7's job is to test that claim rather than assume it: the full radio ladder and §5.4 regression list re-run on production-shaped hardware, then the parts of §7 only real remotes reach — B6 on two wrists, B8's collision, R3/R4 through the real enclosure and strap, R5 during live matches, R6 over a full ten-hour day. First point a complete officiating set exists, so V6.5 and `SCOPE.md` §8.6 become testable end to end.

**M8 — a custom dongle, optional.** Cost-driven, deliberately last. It changes the RF platform underneath a validated system: everything measured at M4 and M7 about range and density is a property of the MDBT50Q module, and a custom dongle re-opens all of it. If BOM cost justifies that, the re-measurement is part of the milestone.

**M9 — deployment validation and MVP hardening.** Not a phase at the end; **runs alongside from M5 onward.** D1–D8 (§6) — D5 is blocked on a real USB VID/PID, a procurement item to start early; D6 requires deciding the production domain before deployment. Plus the §8 gaps M2–M8 do not close: `requestPort()` filters, connection-error language, the Web Worker heartbeat if R1's test demands it, self-hosted fonts, and **ruleset verification against the published rulebooks** — which needs a rules-literate reviewer and is therefore the item most likely to be left until it blocks a real event. Definition of done: §9.

---

## 3. Parked items

Deferred work in one place, each with the condition that re-admits it. **A parked item is not a closed item** — when its unlock condition is met, it enters the queue; nothing here is quietly dropped. (Ladder rungs already scheduled in §2 are not parked — they are queue steps.)

| Item | Why parked | Unlock condition | Re-entry |
|---|---|---|---|
| **V4 row 7** — burst suppression observed on the wire | An operator click dispatches the same `INPUT` a press would but produces no wire `EVT`, so there is nothing to suppress against; mechanism is unit-tested (`DongleService.test.js`) | Dongle-originated `EVT`s under app load — real presses or `TEST` modes | S12 soak, or any W2+ session |
| **V5.1, remote-render half** — remotes render link-lost on app timeout | No remote existed; `radio_null` stub | DK remote connected | **S7** (W5/A15) |
| **V5.3 at 30 s+** — sleep long enough that the OS tears down the USB device | 15 s pass pinned the short-sleep case only | Nothing — cheap bench add-on | Any bench session; fold into S12 |
| **V6.4** — 10× reconnect, no leaked readers/writers | A stopwatch on a `PING` interval can't catch a one-interval leak; needs scripted observation | Scripting, not hardware | **S10** |
| **V6.5** — mid-match dongle swap | Needs a second flashed dongle | Second dongle flashed (M4) | **S11** |
| **V7** — version guard | Cheap but low-yield until a mixed-firmware fleet is possible | Second dongle exists | **S11** |
| **V8 / W8** — soak | Runs overnight once the configuration is stable, rather than blocking progress | S1–S9 stable | **S12** |
| **R1 test** — background-tab throttling vs the heartbeat | Wake lock + banner applied at M1; the test establishing whether that suffices has not run | Nothing — runnable today, needs a 6+ min procedure | M9, or any idle bench slot; the Web Worker is built only if the test fails |
| **Emulator: no `LOG counters` lines** | Surfaced by the wire-log diff; app's counter path never exercised against the emulator | wrsl-app work, any time | With the next emulator change |
| **Emulator: `ECHO` collapses runs of spaces** | `args.join(' ')` vs the firmware's verbatim raw line; firmware is correct | wrsl-app work, any time | With the next emulator change |
| **`CFG` render + `HAP RED` routing** — unobservable on the dongle board, permanently | GPIO not PWM; `RED` drives the unfitted P0.08 ([`dongle/BOARD.md`](dongle/BOARD.md) §2) | DK has PWM and four fitted LEDs | **S5** (W3) |
| **Host suites as a routine build step** | `make check` is not wired into `west build`; no CI. A remembered step | A CI runner, or a build-system hook decision | Open — the standing mitigation is the §1 reminder |
| **Rulesets vs published rulebooks** | Needs a rules-literate reviewer, not an engineer | Reviewer availability | M9 — **before any real match, and again each rules cycle** |
| **Real USB VID/PID + `requestPort()` filters + D5** | Procurement/manufacturing item | VID/PID assigned | M9 — start early, long-lead |
| **Connection-error language** (policy block vs cancelled picker, D4) | App work, low urgency | Nothing | M9 |
| **Self-hosted fonts** | Licensed `.woff2` binaries needed; pre-event load caches meanwhile | License purchase | M9 |
| **Remote DFU strategy** | Absent from every document; consequential (`RADIO_PROTOCOL.md` §10.3) | Decision needed | **Decide at M6, at the latest** |
| **LE Flushable ACL Data** — usable in v3.4.0? | Experimental; the deadline rule must hold without it | Investigation | **S6**, in passing |

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

---

## 5. Validation reference

**There are two ladders.** V0–V8 test the USB link, W0–W8 (§5.3) test the radio. They are separate because they isolate different domains, and the rungs are worked in order — a failure high up is uninterpretable if a lower rung was skipped. **Rung and case names are stable identifiers**: the queue (§2) schedules them, this section defines them, and `HISTORY.md` §9.2 records every execution with date and firmware version.

The interface "works" in the sense that a happy path completed once. That is a much weaker claim than "reliable", and the gap between them is where this class of system fails: at hour three, on a cable pull, on a backgrounded tab, on someone else's laptop.

**Ladder status at a glance:** V0–V6 green (V4 row 7, V6.4, V6.5 parked — §3); V7, V8 never run (queued S11, S12). W0 and W1 green; W2–W8 never run (S5–S12).

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
| **W2** | S5 | **Uplink.** Button → `UP_INPUT` → `EVT` → scoreboard | All three gestures on the four DK buttons (§5.6); 600 ms hold and 150 ms repeat measured, not assumed; A4 duplicate, A5 gap, A6 wrap |
| **W3** | S5 | **Downlink.** `STATE` → `DN_INDICATOR`, `HAP` → waveform, `CFG` → scaling | Indicators assert idempotently (A11); **`ACK … SILENT` puts nothing on the air** (A20) — verify by frame count, not by watching an LED that was never going to light |
| **W4** | S6–S7 | **Round trip and the deadline rule** | `EVT`→`ACK`→render measured as a distribution. A8 a late `ACK` is not sent at all, A9 a second `ACK` replaces rather than queues, A10 a `BEAT` never truncates a `TAP`. `taps_dropped_late` non-zero when provoked and zero when not |
| **W5** | S7 | **Link state, in all four supervision relationships** (`RADIO_PROTOCOL.md` §9.1) | **A15 is the rung** — app supervision expires, radio stays up, both remotes render link-lost. Also A16 boot-is-DOWN, A17 sub-2 s reconnect emits no `DISCONNECTED`, A18 press out of range, A7 reboot re-baselines with no false gap, and §9.4 debounce under repeated power-cycling at the range edge (B4) |
| **W6** | S9 | **Two connections.** The rung M4 exists for | 7.5 ms clean on the pair — no dropped events, no event-length overruns — with the interval reported in the setup `LOG` line so every latency figure is attributable. **B6: taps land on the originating remote only.** R5: cross-connection arrival skew measured. Beat on the owner only, from real hardware. B2 with the transmit-ring drop counter already instrumented |
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

Gaps that are already scheduled point at their queue step or parked row rather than repeating the narrative.

| Item | Impact |
|---|---|
| **Fault indication is invisible on the dongle** | `indicator_error()` drives the unfitted P0.08, so **no `ERR` produces any visible signal** — `APP_TIMEOUT` included. Kept deliberately (`dongle/BOARD.md` §2.2): errors already report on the wire, and moving them to the fitted lamp would destroy the asymmetry that proves routing. **"No blink" never means "no error"** |
| **Every latency figure in `RADIO_PROTOCOL.md` §12 is arithmetic** | Predictions from documentation, never near this hardware. **Load-bearing**: §4.8 chooses rung 3 partly *because* the retransmission rate at 12 m is unmeasured, so measuring it (W7) is what would reopen the decision. R2 |
| **Host suites are a remembered step** | `make check` is not wired into `west build` and there is no CI — nothing fails if it is skipped. §3 |
| **Emulator gaps** — no `LOG counters`, `ECHO` space collapse | §3. The app's counter path is never exercised against the emulator, the one place it is cheap to exercise |
| **No DFU strategy for the remotes** | Absent from every document, and `RADIO_PROTOCOL.md` §10.3 makes it consequential. **Decide at M6, at the latest** — §3 |
| **No hardware design work of any kind** | Module, ERM and driver, PMIC production part, battery sizing, button mechanics, enclosure, APPROTECT. M6, and its inputs are M4 and M5 measurements — §2 |
| **Rulesets not verified against current rulebooks** | The library is implementation-accurate, not authoritative. **Before any real match, and again each rules cycle** — §3 |
| **USB identity is Zephyr's test VID/PID; `requestPort()` has no `filters`** | Blocks the enterprise deployment path (D5); users can select the wrong serial device. §3, long-lead |
| **Connection errors surface raw DOMException text** | A policy block is indistinguishable from a cancelled picker (D4). §3 |
| **Heartbeat survives only in the foreground** | Wake lock and banner applied; the Web Worker that would actually keep it running is not. R1's test decides — §3 |
| **Haptics and indicators unvalidated on the surface that carries them** | No real ERM, RGB or strap until M5 — every R3/R4 requirement is open until then |
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
- [ ] W2–W5 pass on one connection, with A15 and A20 — **S5–S7**
- [ ] **The demonstration:** press on the DK → score on the scoreboard → tap rendered on that DK, and on that DK only — **S7**
- [ ] W6–W7 pass on two connections, with the interval in use recorded against every latency figure — **S9, S11**
- [ ] V8 clean for ≥4 h with zero sequence gaps and zero applied duplicates; W8 clean with both remotes connected, `radio_gap`/`radio_dup` accounted for — **S12**
- [ ] R1–R6 measured, with mitigations applied where they fail — **R3, R4 and R6 before M6 opens** (§2)
- [ ] D1–D8 pass; real VID/PID assigned and `requestPort()` filtered — M9
- [ ] Every ruleset in the library checked against the published rulebook for the current cycle — M9
- [ ] §5.4 re-run in full after the radio lands (S12), and again at M7 on custom hardware
- [ ] `PROTOCOL.md` amended for any further constraint that proves real; `RADIO_PROTOCOL.md` likewise, and its §12 predictions replaced by measurements
- [ ] **The rung 3 decision revisited against a measurement** — either confirmed by a p99 that closes 25 ms at 12 m through a torso, or reopened in favour of SCI (§4.8)
