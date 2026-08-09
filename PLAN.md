# RefRemote — Plan, Status and Validation

**Status as of 2026-08-09.** The USB link is built and working end to end on real hardware, against protocol **v2.0**. `SCOPE.md` and `SYSTEM_FUNC_SPEC.md` are new and are now the authority on direction; `PROTOCOL.md` **v3.0** is the revision that answers to them, and it is a breaking change. Neither the application nor the firmware has been brought up to it yet. The radio layer is not started and the remotes do not exist.

This document carries where the project stands, the decisions that still bind future work, the milestone sequence, and the full validation ladder. **The validation plan previously lived in `dongle/VALIDATION.md` and has been rolled in here** (§7–§12), because a status document that points at a separate test plan gets read as a status document.

| For | See |
|---|---|
| What the system is and why | [`SCOPE.md`](SCOPE.md) |
| How it behaves | [`SYSTEM_FUNC_SPEC.md`](SYSTEM_FUNC_SPEC.md) |
| The wire protocol | [`PROTOCOL.md`](PROTOCOL.md) |
| Build, flash, manual test | [`dongle/README.md`](dongle/README.md) |

---

## 1. Where things stand

| Component | State |
|---|---|
| `SCOPE.md`, `SYSTEM_FUNC_SPEC.md` | Current. Authoritative. |
| `PROTOCOL.md` v3.0 | Written against the specification. **Not yet implemented on either side.** Byte-identical in both repos. |
| Scoreboard app | Working against v2.0. 57 tests passing. Being rebuilt for v3.0 — milestone M1. |
| Dongle USB firmware | Working against v2.0: framing, parser, clock, heartbeat, supervision, CONFIRM, TEST modes. 52 KB flash, 19 KB RAM. Milestone M2 brings it to v3.0. |
| Dongle radio | **Not started.** `CONFIG_DONGLE_FAKE_LINK` fabricates link state; LEDs stand in for haptics. |
| Remote firmware | **Not started.** Hardware not built. |
| Host parser tests | Written, **never executed** — no C compiler on the dev machine. |
| Validation | V1–V3 passed against v2.0 and are **void** for v3.0. V0 and V4–V8 never run. |

**Working on real hardware, in both directions, at v2.0:** the handshake, the PING cadence, clock state and the 1 Hz heartbeat, link supervision with `ERR APP_TIMEOUT`, end-to-end confirmation, and all TEST modes driving the scoreboard.

That is a real result and it is what made the v3.0 revision cheap: the transport, the framing, the supervision model and the build and flash path are all proven, and the revision touches the message set above them.

---

## 2. What v3.0 changes, and why it had to

`PROTOCOL.md` §15 lists the changes. Three of them are not refinements — the v2.0 design contradicts the specification and could not be extended into compliance.

### 2.1 The wire carried officiating meaning

v2.0's `EVT TIME_UP` / `PERIOD_UP` named operations, not buttons. FS §7.3 requires that no message carry ruleset meaning and that a new ruleset need no communication-layer change; FS §7.4 requires that adding a ruleset be a scoreboard change only.

Worse, those tokens hard-coded a *default* assignment that `SCOPE.md` §9.2 has already marked for post-MVP remapping — under v2.0, letting a referee remap `FORWARD` would have meant a firmware release. v3.0 transports `<button> <gesture> <src>` and assigns meaning in the scoreboard.

This was the single most consequential finding of the specification pass. The prototype protocol looked correct because it worked; it was structurally unable to reach the product.

### 2.2 The heartbeat was generated on the dongle

v2.0 principle R2 put the 1 Hz heartbeat on the dongle, driven by `CLOCK RUN` / `CLOCK STOP`, specifically to remove a per-second packet. FS §6.2 rejects that architecture by name:

> Architectures that hold ownership state on the remote or dongle to improve beat regularity solve a problem the product does not have, while introducing one it cannot tolerate: a remote beating on stale state reports one thing on the referee's wrist while the scoreboard reports another, quietly and with no self-correcting mechanism.

The v2.0 heartbeat was also *wrong for the product* in a way that was invisible at the prototype: it beat on both remotes whenever the clock ran. The real heartbeat beats on **the owning athlete's remote only**, while the secondary clock accrues — which is a different signal carrying different information, and the dongle cannot know it because ownership is match state.

Per-beat commanding costs one message per second and buys three things: no stale-state divergence, continuous end-to-end liveness proof during exactly the periods the referee depends on the system most, and a remote that stays fully stateless.

### 2.3 The acknowledgement budget was four times too loose

v2.0 gave confirmation a 500 ms window because confirmation was a convenience. FS §5.3 makes it a **functional requirement of the scoring interface**: multi-point actions are entered as repeated presses, and viability rests on the referee feeling each press land. The budget is ~120 ms inclusive of retries, decomposed in `PROTOCOL.md` §11.

A late tap is worse than no tap, and that asymmetry now drives the design: exceeding the budget must degrade to silence.

---

## 3. Milestones

| # | Milestone | State |
|---|---|---|
| **M0** | USB link at v2.0, working on hardware | ✅ done |
| **M1** | **Scoreboard application to v3.0, the functional specification, and the design system** | ▶ in progress |
| **M2** | Dongle USB firmware to v3.0 | next |
| **M3** | Radio layer and remote firmware | §5 |
| **M4** | Validation ladder and deployment validation | §7–§12 |
| **M5** | MVP hardening — USB identity, instrumentation, ruleset library | §13 |

M1 before M2 deliberately. The application is the node that holds every requirement the specification added — ruleset configuration, secondary clocks, counters, flags, action grouping, persistence, the watchdog, the tiered display — and it can be built and fully tested against `FakeDongleTransport` with no hardware at all. Bringing the firmware up first would mean guessing at the shape of the traffic the application actually produces.

### 3.1 M1 — scoreboard application

| Item | Requirement |
|---|---|
| `protocol.js` to v3.0 | Every message in `PROTOCOL.md` §3, all cases in §14 |
| `DongleService` to v3.0 | Handshake with `CFG` + `STATE`, 1 s `PING`, 2.5 s supervision, `ACK` with dedupe, `JOIN` → `STATE`, `HAP`, latency and gap instrumentation |
| Ruleset configuration | The `FS §12.2` schema as data, with the library: NFHS, NCAA, UWW freestyle/Greco, IBJJF, ADCC |
| Match model | Score with floor, periods and phases, counters with ladder position, tri-state flag, secondary clock both polarities, action grouping, action log |
| Clock of record | Monotonic, wall-clock-immune, discontinuity detection with referee confirmation |
| Persistence | Survives reload and browser restart; restore prompt on load |
| Watchdog | Drops the serial link on a match-state stall (FS §8.3) |
| Display | Tiered per FS §8.4, built on the design system, `.rr-mat` surface |
| Pre-match confirmation | Ruleset, period structure, F1/F2 legend, colour assignment, set serial, link and battery (FS §12.3) |
| Tests | Protocol suite and service suite, both hardware-free |

### 3.2 M2 — dongle USB firmware

Straightforward against a finished application: the message set changes, the clock and heartbeat timer are deleted, `ACK` routing replaces `CONFIRM` routing at a 120 ms window, `STATE` and `CFG` are relayed to the radio seam, `JOIN` is emitted from it, and `TEST 4` is added. The framing, transport, supervision skeleton and build path are unchanged.

The pending table shrinks in lifetime and grows in importance. The transmit ring needs a drop counter before M3, not after.

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

**Practical consequence for M3:** the console is disabled and a second CDC instance is forbidden, so how diagnostics get out during radio bring-up is a decision to make **before** starting, not during. RTT is the obvious answer and needs the debugger partition table (`fstab-debugger.dtsi`), which means giving up the stock bootloader on the bring-up unit.

### 4.5 Never gate transmission on DTR

The app opens the port via Web Serial and never calls `setSignals()`, so DTR assertion is the browser's default rather than anything the protocol guarantees. Gating on it yields a dongle that enumerates but never answers `INFO`. Boot-time `HELLO` is best-effort; the handshake is driven by the app sending `INFO`.

### 4.6 Everything runs on the system workqueue

All engine timers and the RX drain run on the system workqueue, giving exactly one producer feeding the transmit path, so no locking is needed between them.

**This is the constraint most likely to matter at M3.** Radio work on the same queue can delay the acknowledgement turnaround, and the budget that used to have 500 ms of slack now has 120 ms across six hops. Expect to need a dedicated cooperative workqueue for the engine, and measure before assuming otherwise.

### 4.7 `PROTOCOL.md` is duplicated, not linked

The two copies were previously kept in step by a filesystem hard link. That does not survive an editor writing a new file rather than modifying in place — it silently broke during this revision, leaving the two repos on different versions with no indication. Copy explicitly and **verify the hashes match** after any change.

---

## 5. M3 — the radio layer

Not designed here. What follows is the starting position, the evidence behind it, and the questions that have to be answered first.

### 5.1 What the radio has to deliver

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

### 5.2 Bluetooth LE against Enhanced ShockBurst

Both ends are Nordic silicon, so a proprietary link is genuinely available and worth taking seriously rather than dismissing.

**Enhanced ShockBurst** is attractive on three of the requirements. It is a star topology with one Primary Receiver and up to eight Primary Transmitters, which is our shape exactly. It does packet acknowledgement and automatic retransmission in hardware, with a configurable retransmit count and delay. And critically, **the PRX discards repeated packets**, so a retransmitted press is not delivered twice — link-layer exactly-once, which is the guarantee `PROTOCOL.md` §5.3 is built on. Latency is excellent: there is no connection interval to wait for, so a press goes out when it happens.

It fails on three others, and the third is decisive.

- **No channel hopping.** ESB does not do adaptive frequency hopping. A fixed channel in a hall with venue Wi-Fi, hundreds of spectator phones and 29 other systems is exactly the environment `SCOPE.md` §4 says the product must survive. Frequency agility would have to be built — which is what Gazell is, and adopting Gazell brings its own constraints.
- **No security of any kind.** ESB traffic is plaintext with no authentication. FS §2.3 requires pairing to be cryptographically enforced rather than proximity-based, because cross-system association is a scoring-integrity failure. That layer would be ours to write and get right, over a link where getting it wrong corrupts two matches at once and need not be obvious to either referee. The nRF52840 has CryptoCell-310 and hardware AES-CCM, so it is feasible — but it is our code holding scoring integrity.
- **The downlink is backwards for this product.** In ESB, PRX→PTX data rides only as a payload attached to an acknowledgement, and acknowledgement payloads must be **preloaded** — a transmitter cannot send a command and get a direct response to it. Our downlink is not a response channel: it is a 1 Hz heartbeat, plus per-press acknowledgement taps, plus expiry buzzes and indicator assertions, all originated by the scoreboard asynchronously. Delivering that over ESB means the remotes poll continuously, which spends the battery budget on the uplink to service a downlink, and adds a polling interval to every acknowledgement.

**Bluetooth LE** answers all three. Connection events are bidirectional by construction, so the downlink costs nothing extra. Adaptive frequency hopping across 37 data channels is the mechanism the density requirement needs, and Wi-Fi-overlapping channels can be marked bad in the channel map. LE Secure Connections with bonding gives cryptographically enforced pairing directly, with no protocol of our own between us and scoring integrity.

The question BLE has to answer is latency. Standard minimum connection interval is 7.5 ms, and Nordic's own multi-link HID guidance uses **10 ms rather than 7.5 ms whenever more than one connection is active**, because 7.5 ms with multiple links produces link-layer scheduling conflicts that show up as dropped report rates and disconnections. Two remotes means two connections, so 10 ms is the naive figure — and a 10 ms interval with retransmission slots is uncomfortably close to the 25 ms allocation before body shadowing is considered.

Two mechanisms close that gap, both available on nRF52 in the pinned SDK:

- **Shorter Connection Intervals (SCI)**, from Bluetooth 6.2 and present in NCS since v3.2.0, extends the interval range down to 1.25 ms in the mandatory range and 375 µs in the extended range, and mandates connection subrating. It is supported across the Nordic portfolio including the nRF52 series. Because both ends of this link are ours, the usual objection — that consumer hosts will not support it for years — does not apply.
- **Low Latency Packet Mode (LLPM)**, Nordic proprietary, gives a 1 ms interval on LE 2M PHY. Note that Nordic's own desktop application drops to 10 ms when LLPM is combined with more than one connection, for the same scheduling reason.

**Starting position:** Bluetooth LE, dongle as central holding two peripheral connections, LE 2M PHY, SCI negotiated to the shortest interval both ends support with 10 ms as the fallback, LE Secure Connections with bonding for the fixed set pairing. ESB is retained as the documented fallback if measured latency at density fails — and if it is adopted, the channel-hopping and security layers are scoped as first-class work, not as details.

**Do not treat this as settled.** It is a reasoned starting point from documentation, and the numbers that matter — latency at 12 m through a torso, with 29 other systems in the hall — cannot be obtained from documentation.

### 5.3 Where the dongle sits is part of the link budget

The Raytac MDBT50Q-CX-40 carries an MDBT50Q-P1M module with a PCB trace antenna, and the nRF52840 will do up to +8 dBm. The dongle then sits in a USB port on a laptop at the scoreboard table: close to the host's own 2.4 GHz radios, often below table height, frequently with bodies between it and the mat.

The link budget must be taken **at the dongle as deployed**, not on a bench with clear line of sight. If it does not close, the available remedies are a USB extension cable to raise and separate the dongle, higher transmit power, or a dongle placement constraint in the deployment documentation — in that order of preference.

### 5.4 Seams that exist in the code today

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

### 5.5 Questions M3 has to answer

- Does radio work share the system workqueue (§4.6), or does the engine need its own? What jitter does the 1 Hz heartbeat tolerate, and what does the 120 ms acknowledgement budget tolerate?
- How is a physical remote bound to the `RED` / `GREEN` identity, and how is the officiating-set serial provisioned so `HELLO` can report it?
- What is the measured one-way latency at 12 m through body shadowing, at density — and what is its p99, not its median?
- How is `LINK` state derived and debounced so a remote at the edge of range does not flood the USB link with transitions?
- Where do presses go that arrive while the app is disconnected — dropped, or queued? *(Dropped. A queued press applied minutes later is a wrong score with no visible cause. But it must be counted and surfaced.)*
- What is the power cost of a 1 Hz downlink beat to one remote plus the connection cadence the acknowledgement budget requires, against the ten-hour target?

---

## 6. How the radio can regress the USB link

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

---

## 7. Validation — how to use the ladder

**Work the rungs in order.** Each isolates one failure domain, and a failure high up is uninterpretable if a lower rung was skipped. Record the date and firmware version against each result in §12.

The interface "works" in the sense that a happy path completed once. That is a much weaker claim than "reliable", and the gap between them is where this class of system fails: at hour three, on a cable pull, on a backgrounded tab, on someone else's laptop.

**V1–V3 passed against protocol v2.0 and are void.** The message set they exercised no longer exists. They are cheap to re-run and must be, after M2.

### 7.1 Test rig

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

**Two standing traps:**

- **`TEST 3` suspends link supervision until `TEST 0` or reboot.** Left on, every supervision test in V5 passes for the wrong reason. Send `TEST 0` first and confirm the reply.
- **`CONFIG_DONGLE_FAKE_LINK=y` fabricates `LINK … CONNECTED`** with synthetic RSSI and battery. Any test that appears to validate link reporting is validating a constant until this is `n`.

---

## 8. The ladder

### V0 — Parser unit tests (host, no hardware) — **NEVER RUN**

`protocol.c` has no Zephyr dependencies precisely so this can run anywhere. It has never executed: the development machine has no host C compiler.

```bash
cd dongle/tests/protocol && make check
```

**Pass:** all cases green, exit 0. Covers `PROTOCOL.md` §14 T1–T16, encoder round-trips, and the `LINK` RSSI constraint of §7.

**Priority: highest.** The firmware parser is currently trusted on inspection alone. Every bug caught here is a bug not chased over USB. Run it on any Linux box, WSL, macOS, or MinGW/MSYS2 install. The v3.0 cases T11–T16 are new and include the two that fail closed: T7, the v2.0-shaped gestureless `EVT`, and T16, the duplicate `seq`.

The app's counterpart suite (`npm test` in `wrsl-app`) does run and passes.

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

**Instrumentation gap:** the app logs gaps as one-off warnings into a ring buffer that will have rolled over long before anyone reads it. Running counters — gaps, duplicates, dropped beats, latency distribution, connection uptime — and a log export must exist before V8 is attempted, or the result is unfalsifiable. This is M1 work.

---

## 9. Deployment validation — **NOT RUN**

The product ships from a website onto organisation-managed computers. That introduces failure modes no bench test reveals.

| # | Test | Pass criteria |
|---|---|---|
| D1 | Serve over real HTTPS (not localhost) | Works. Plain `http://` on a LAN IP will **not** |
| D2 | Chromium-based browsers | Connects. Non-Chromium degrades with a clear message rather than a broken page |
| D3 | Linux client | Port opens. Requires `dialout` membership or a udev rule — without it Chrome lists the port and fails to open it, opaquely |
| D4 | Machine with `DefaultSerialGuardSetting=2` | App detects the block and says so in plain language, not as a raw DOMException |
| D5 | Machine with `SerialAllowUsbDevicesForUrls` allowlisting the origin + VID/PID | Connects with **no picker and no prompt** |
| D6 | Change the site origin, then reconnect | Confirms the expected loss of all grants |
| D7 | Full offline run — load once, disconnect the network, run a complete match | Works end to end. This is `SCOPE.md` §7.3 and it is the one users will actually exercise |
| D8 | Ten-hour session on one host without restart | No memory growth, no clock drift, no degradation of the debug ring |

**Blocker for D5:** the dongle enumerates with Zephyr's test identity (`VID 0x2fe3`, `PID 0x0004`, `"CDC ACM serial backend"`). A policy allowlisting `0x2fe3` would grant the site access to any Zephyr device the user plugs in, and IT will reject it. A real VID/PID and product string are prerequisites, and `requestPort()` must carry a matching `filters:` array.

**D6 is a decision, not just a test.** Serial grants are per-origin. Decide the production domain before deployment, not after.

---

## 10. Interop constraints stricter than the spec reads

Real, discovered during implementation, each of which would present as a silent mystery rather than an error. All four are now written into `PROTOCOL.md` — the spec should describe what shipped.

1. **`LINK <remote> CONNECTED` without an RSSI value is rejected.** Firmware must always emit it when connected. Symptom if violated: the signal indicator silently never updates. Now mandatory in `PROTOCOL.md` §7.
2. **Resynchronisation happens at the next `\n`, never at a chunk boundary.** A read boundary carries no information about the stream. The app had a bug here (fixed 2026-08-07); `PROTOCOL.md` §2.2 carries the clarifying paragraph and T9c pins it.
3. **Nothing but the protocol may write to the CDC-ACM port** (§4.4). If either guard is relaxed, log output interleaves with protocol traffic, corrupting lines intermittently and silently.
4. **The acknowledgement must fire on the originating remote only**, routed by the `src` recorded against that `seq`. A broadcast tap is indistinguishable from a correct one in single-remote bench testing and wrong in every real match.

---

## 11. Unmeasured risks

### R1 — Background-tab throttling versus the heartbeat — **materially worse at v3.0**

Chrome throttles timers in hidden tabs; after several minutes hidden a tab can drop to roughly one callback per minute.

At v2.0 this threatened only the liveness proof, because the heartbeat was generated on the dongle. **At v3.0 the heartbeat is generated by the app**, so throttling stops the referee's heartbeat directly, and the `PING` cadence with it. Alt-tabbing away from the scoreboard would stop the beat on the referee's wrist while riding time continued to accrue on a display nobody is looking at.

`SCOPE.md` §5 makes a foregrounded scoreboard a stated operating assumption, and it is an easy one to hold in practice — the scoreboard is the mat's public state indicator. But an assumption is not a mechanism.

**Test:** connect, start a secondary clock, confirm the beat. Fully hide the window for 6+ minutes. Return and read the counters.

**Fail:** any beat interval materially over one second, or `ERR APP_TIMEOUT` present.

**Mitigations, in order of preference:** a screen wake lock, which is appropriate anyway for an application driving a public display; moving the beat and `PING` into a Web Worker, which is not throttled the same way; and surfacing loss of foreground in the primary tier so the operator sees the state the assumption was violated in. Widening the supervision timeout is not a mitigation — it weakens the fail-safe the whole design rests on.

### R2 — The 120 ms acknowledgement budget has never been measured

`PROTOCOL.md` §11 allocates 120 ms across six hops. The v2.0 round trip through React render and the browser event loop was never measured even against 500 ms, and that path is now allocated 25 ms with two radio hops added around it.

**Test:** with `TEST 2` as background load, pair each `RX EVT … <seq>` with its `TX ACK <seq>` and take the difference. **Measure the distribution, not the median** — p99 is what matters.

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

## 12. Definition of done, and the results log

- [ ] V0 green on a machine with a C compiler
- [ ] V1–V8 pass at v3.0, recorded below with dates and firmware version
- [ ] R1–R6 measured, with mitigations applied where they fail
- [ ] D1–D8 pass; real VID/PID assigned and `requestPort()` filtered
- [ ] V8 clean for ≥4 h with zero sequence gaps and zero applied duplicates, using real instrumentation
- [ ] §6 re-run in full after the radio lands
- [ ] `PROTOCOL.md` amended for any further constraint that proves real

| Date | FW | Proto | Rung | Result | Notes |
|---|---|---|---|---|---|
| 2026-08-07 | 0.1.0 | 2.0 | V1 | pass | *void at v3.0* — `ERR APP_TIMEOUT` observed and correct |
| 2026-08-07 | 0.1.0 | 2.0 | V2 | pass | *void at v3.0* — handshake and PING cadence confirmed |
| 2026-08-07 | 0.1.0 | 2.0 | V3 | pass | *void at v3.0* — `TEST 1`, seven events, confirmation round trip |
| | | | | | |

---

## 13. Known gaps and debt

| Item | Impact |
|---|---|
| Host parser tests never executed | The firmware parser is trusted on inspection alone. Highest-value outstanding item; needs only a machine with a C compiler |
| V4–V8 never run | Supervision, reconnect and soak behaviour unverified |
| Soak instrumentation missing | No running counters for gaps, duplicates, dropped beats or latency, and no log export. V8 is unfalsifiable without them. M1 work |
| USB identity is Zephyr's test VID/PID | Blocks the enterprise deployment path (D5) |
| `requestPort()` has no `filters` | Users can select the wrong serial device |
| Connection errors surface raw DOMException text | A policy block is indistinguishable from a cancelled picker |
| No remote hardware | Every haptic and indicator requirement is unvalidated on the surface that carries it |
| Ruleset library incomplete | The configuration schema exists; the preconfigured rulesets are M1/M5 work and must be verified against the current rulebooks at each rules cycle |
