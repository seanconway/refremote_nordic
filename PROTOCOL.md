# RefRemote — Dongle ↔ Scoreboard Wire Protocol

**Version:** 4.0
**Link:** USB CDC-ACM (virtual COM port), nRF52840 dongle ↔ browser via Web Serial
**Scope:** the wired link between the dongle and the scoreboard application only. The radio link between dongle and wrist remotes is a separate protocol, specified in `RADIO_PROTOCOL.md`; §12 states only what this link assumes of it.

**Governing documents:** *Project Scope* v1.1 and *Functional Specification* v2.1. Where this document and those disagree, they win. Section references of the form *FS §7.3* point at the functional specification.

**Supersedes v2.0.** v2.0 was written against a seven-action prototype in which the dongle generated the heartbeat locally and the scoreboard confirmed points. Three requirements in the functional specification are incompatible with it, and each of them alone forces a major version:

| Requirement | v2.0 | v3.0 |
|---|---|---|
| **No message carries ruleset meaning** (FS §7.3) | `EVT TIME_UP`, `EVT PERIOD_UP` name officiating operations | `EVT` names a **button and a gesture**. Meaning is assigned by the scoreboard. |
| **Heartbeat commanded per beat by the scoreboard** (FS §6.2) | Dongle ran a local 1 Hz timer off `CLOCK RUN` | Dongle holds no clock state. Each beat is one commanded haptic. |
| **Acknowledgement within ~120 ms** (FS §7.3) | 500 ms confirmation window | 120 ms budget, decomposed and allocated in §11. |

See §14 for the full change list.

**Supersedes v3.0.** `STATE`'s `<f1rgb>`/`<f2rgb>` arguments (§6) carried an arbitrary `RRGGBB` hex triple, chosen by the scoreboard with no knowledge of what the remote's LEDs could actually render true. In practice the app's own brand colours — never calibrated against this hardware — rendered visibly wrong on `LED_F1`/`LED_F2` while the remote's own fixed palette, used by `LED_LINK`/`LED_PWR`, read correctly. v4.0 replaces the hex triple with `<f1colour>`/`<f2colour>`, one of a fixed four-name palette (`RED`/`GREEN`/`BLUE`/`YELLOW`) the remote renders identically across all four indicators (`RADIO_PROTOCOL.md` §5.5, §7). The scoreboard states *which* colour, never *what it looks like* — a changed argument meaning, which forces the major version by this document's own rule (§16).

---

## 1. Design principles

**R1 — Do not re-implement USB.**
CDC-ACM rides on USB bulk endpoints. Error detection and retransmission happen below the application. What USB does *not* give you is message boundaries in a byte stream, so framing is the one transport concern that remains yours.

**R2 — The wire carries buttons, not officiating.**
A press is reported as *which button, which gesture, which wrist*. Nothing on this link knows what a point is, that periods exist, or which ruleset is loaded. This is not tidiness; it is FS §7.4 — adding a ruleset must be a scoreboard change only, and the way you guarantee that is to give the transport no vocabulary to change.

The v2.0 protocol failed this. `EVT PERIOD_UP` is a folkstyle-shaped message: it presumes the green remote's `FORWARD` button advances a period, which is a *default assignment* the scope document has already marked for post-MVP remapping. Under v3.0 remapping a button is a scoreboard configuration edit and the firmware never learns of it.

**R3 — State is idempotent; events are not.**
Indicator state is re-sent freely and always in full (§6). Button events are delivered exactly once (§5.3). The two halves of the link have opposite reliability disciplines and it is worth naming which is which on every message.

**R4 — Absence is the failure signal.**
No acknowledgement tap means the press did not land. No heartbeat means accrual is not running. No line from the app means the end-to-end link is down. Every failure in this system is rendered as the *absence* of something the referee was expecting, never as a distinct "something went wrong" signal — because a buzz that means an error is a buzz that has to be told apart from four other buzzes by a referee who is watching two athletes.

**R5 — Periodic behaviour lives where the state lives.**
v2.0 put the heartbeat timer on the dongle to remove a per-second packet. FS §6.2 rejects that: a remote or dongle beating on locally-held ownership state can report one athlete on the wrist while the scoreboard reports another, silently and with nothing to correct it. The per-second message is the price of never diverging, and it buys continuous end-to-end liveness proof during exactly the periods when the referee depends on the system most.

**R6 — Human-readable on the wire.**
`screen /dev/ttyACM0 115200` shows every event in plain text, and you can type commands by hand to exercise firmware with no browser, no script, no harness.

---

## 2. Framing

Unchanged from v2.0. It was the one part of the v1.0 design that earned its place, and nothing in the functional specification disturbs it.

### 2.1 Line format

```
<KEYWORD> [ARG]... \n
```

| Rule | Value |
|---|---|
| Encoding | 7-bit ASCII |
| Terminator | `\n` (0x0A). Receivers must tolerate and strip a preceding `\r`. Transmitters emit `\n` only. |
| Separator | A single space (0x20) between tokens |
| Keywords | Uppercase `A–Z` and `_` |
| Max line length | 120 bytes including terminator |
| Empty lines | Ignored |

### 2.2 Receiver rules

Accumulate bytes into a line buffer until `\n`. Then strip any trailing `\r`, split on spaces, dispatch on the first token.

- **Unknown keyword → ignore silently.** This is what lets v3.1 add messages without breaking v3.0 peers.
- **Wrong argument count or unparsable argument → ignore silently, log locally.**
- **Buffer exceeds 120 bytes without a terminator → discard the buffer and resynchronise at the next `\n`.**

**Chunk boundaries are the one real hazard on this link.** `reader.read()` returns arbitrary chunks: a line may span several reads, and one read may contain several lines plus a partial. Feed bytes into the line assembler; never assume one read equals one line.

**Resynchronisation happens at the next `\n`, which may be several reads away. A chunk boundary is not a resync point** — the split between reads is an artifact of the transport and carries no information about the stream. It follows that a discarded run and the line sharing its terminator are lost together: after overlong garbage with no intervening `\n`, the next line to arrive is consumed as the tail of that garbage, and the line after it is the first to parse. Losing one line is the correct price of resynchronising on a known-good boundary. The alternative — treating a read boundary as a resync point — lets the tail of a garbage run be emitted as a message, and a tail that happens to begin at a keyword boundary would parse as a genuine event.

---

## 3. Message summary

**Dongle → App**

| Message | Args | Meaning | §|
|---|---|---|---|
| `HELLO` | `<proto> <fw> <set> <caps>` | Identity and bound officiating set. Boot and reply to `INFO`. | 4.1 |
| `EVT` | `<button> <gesture> <src> <seq>` | A referee press. The only match-affecting input path. | 5 |
| `LINK` | `<remote> <state> [rssi] [batt]` | Radio link and battery for one remote. | 7 |
| `JOIN` | `<remote>` | This remote has come onto the link and holds no indicator state. | 6.3 |
| `PONG` | — | Reply to `PING`. | 8 |
| `ECHO` | `<text>` | Reply to `ECHO`, identical text. | 10 |
| `LOG` | `<text>` | Freeform diagnostic. Never semantic. | 10 |
| `ERR` | `<text>` | Dongle-side error, e.g. `ERR RADIO_INIT_FAILED`. | 10 |

**App → Dongle**

| Message | Args | Meaning | §|
|---|---|---|---|
| `ACK` | `<seq> [SILENT]` | This event is applied. Fire the acknowledgement tap on its originating remote. | 5.3 |
| `STATE` | `<remote> <f1> <f1colour> <f2> <f2colour>` | Complete app-owned indicator state for one remote. Idempotent. | 6 |
| `HAP` | `<target> <waveform>` | Render one haptic waveform. | 9 |
| `CFG` | `<target> <haptic> <bright>` | Haptic intensity and LED brightness, 0–100. | 6.4 |
| `SIMSOC` | `<target> <pct>` | Bench-only: simulated `LED_PWR` state of charge. | 6.5 |
| `PING` | — | Liveness. Dongle replies `PONG`. | 8 |
| `INFO` | — | Dongle replies `HELLO`, then `LINK` per remote. | 4.1 |
| `ECHO` | `<text>` | Dongle replies `ECHO <text>`. | 10 |
| `TEST` | `<mode>` | Self-test. | 10.2 |

Eight and nine, `SIMSOC` being the one bench-only exception to the otherwise-symmetric count. Every officiating concept in the functional specification — points, periods, riding time, advantages, cautions, phases, criteria, the pending-choice flag — is expressible over these sixteen messages without any of them naming it.

---

## 4. Identity

### 4.1 `HELLO`

```
HELLO 3.0 0.2.0 RR-0147 0
```

| Arg | Meaning |
|---|---|
| `<proto>` | Protocol version, `major.minor`. |
| `<fw>` | Dongle firmware version. |
| `<set>` | **Officiating set serial**, `[A-Z0-9-]{1,16}`. The set this dongle is firmware-paired to. |
| `<caps>` | Capability bitfield, decimal. `0` in this revision. Unknown bits must be ignored. |

**On `HELLO`,** the app compares the protocol version. Differing minor → warn and continue. Differing major → refuse to operate and tell the operator to update the dongle firmware.

**The set serial is not decoration.** FS §2.3 makes cross-system association a scoring-integrity failure rather than an inconvenience: a remote delivering input to a neighbouring mat corrupts two matches at once and need not be obvious to either referee. The set serial is what the referee checks against the label on the hardware at pre-match confirmation (FS §12.3), and it is the app's only means of noticing that the dongle in the laptop is not the dongle the remotes on the referee's wrists belong to. The app displays it; it does not validate it, because the binding it would be validating against is the one printed on the case.

### 4.2 Handshake

**On app connect** (after Web Serial `open()`):

1. App sends `INFO`.
2. Dongle replies `HELLO`, then one `LINK` line per remote.
3. App sends `CFG` for both remotes.
4. App sends `STATE` for both remotes, asserting full current indicator state (§6.3).
5. App begins the `PING` cadence of §8.

Steps 3 and 4 are what make a mid-match dongle substitution (FS §8.6) a physical swap and nothing more. The app does not clear the remotes and wait for the next state change; it asserts what is true now, so `LED_F1` and `LED_F2` are correct before the clock restarts.

**Boot-time `HELLO` is best-effort.** The dongle emits one at boot for the benefit of a human on a terminal, but the handshake is driven by the app sending `INFO`. Never gate transmission on DTR: the app opens the port via Web Serial and never calls `setSignals()`, so DTR assertion is the browser's default rather than anything this protocol guarantees. Gating on it yields a dongle that enumerates and never answers.

---

## 5. `EVT` — referee input

```
EVT ADD_POINT PRESS RED 17
EVT TOGGLE_CLOCK HOLD GREEN 18
EVT F1 PRESS RED 19
EVT BACKWARD HOLD_REP RED 20
```

### 5.1 Arguments

**`<button>`** — the seven physical buttons of FS §3.1, named by position, not function:

| Token | Position |
|---|---|
| `ADD_POINT` | Centre column, top |
| `TOGGLE_CLOCK` | Centre column, middle (the oversized tactile datum) |
| `REMOVE_POINT` | Centre column, bottom |
| `FORWARD` | Upper lateral pair, left |
| `BACKWARD` | Upper lateral pair, right |
| `F1` | Lower lateral pair, inner |
| `F2` | Lower lateral pair, outer |

`ADD_POINT` and `REMOVE_POINT` are named for their universal function because FS §5.1 fixes it across every ruleset; they are still transported as buttons and the scoreboard is still free to rebind them. `F1` and `F2` are deliberately unnamed — their meaning is a riding-time clock, an advantage counter, a caution counter, a negative-point counter or a pending-choice flag depending on a ruleset the dongle has never heard of.

**`<gesture>`** — classified in remote firmware, per FS §4.2:

| Token | Definition | Default threshold |
|---|---|---|
| `PRESS` | Pressed and released before the hold threshold | debounce 15 ms |
| `HOLD` | Continuous depression past the hold threshold | 600 ms |
| `HOLD_REP` | Continued repetition while held | every 150 ms |

**Gesture discrimination never crosses this link.** Thresholds are firmware constants, not app-configurable, so classification is immediate and identical regardless of link state. A `HOLD` is reported once, at the moment the threshold is crossed — not on release — because the referee's feedback has to arrive while the finger is still down.

`HOLD_REP` is emitted only for clock adjustment (`FORWARD` / `BACKWARD`). Any other button emits at most one `HOLD` per depression.

**`<src>`** — `RED` or `GREEN`, the originating remote, which is also the athlete for every per-athlete function.

**`<seq>`** — decimal, `0`–`65535`, incremented per `EVT` and wrapping. See §5.3.

### 5.2 What the scoreboard does with this

Nothing in this section is protocol. It is here so the message set can be read against the behaviour it has to carry, and it is reproduced from FS §5.1 and §5.5.

| Button | Gesture | Red remote | Green remote |
|---|---|---|---|
| `TOGGLE_CLOCK` | `PRESS` | Start / stop the main clock | same clock |
| `TOGGLE_CLOCK` | `HOLD` | Reset the period clock | same |
| `ADD_POINT` | `PRESS` | +1 to red | +1 to green |
| `REMOVE_POINT` | `PRESS` | −1 to red, clamped at the ruleset floor | −1 to green |
| `FORWARD` / `BACKWARD` | `PRESS` `HOLD_REP` | Main clock ±1 s | Next / previous period |
| `F1` / `F2` | `PRESS` `HOLD` | Ruleset-assigned role | mirrored |

Two of these are default assignments the scope document has already flagged for post-MVP remapping. Under §R2 that remapping costs a configuration edit.

**Inert buttons.** Where a ruleset binds nothing to a function button (folkstyle NFHS leaves F1 inert), the app sends `ACK <seq> SILENT`: the event is consumed, no haptic fires, no indicator moves. FS §5.6 requires inert to be *fully* inert — a rejection signal would be more confusing than silence.

### 5.3 Exactly-once delivery

FS §7.3 is unambiguous: a silently dropped or doubled scoring input corrupts the match score with no external indication, and repeated-press scoring makes it worse — one dropped press in a sequence of four produces a plausible wrong score rather than an obvious fault.

**The mechanism:**

1. The dongle assigns `seq` in order of receipt and holds the event in a pending table (8 entries).
2. The app applies the event, **deduplicating on `seq`**, and replies `ACK <seq>`.
3. The dongle routes the acknowledgement tap to the remote recorded against that `seq`, and clears the entry.
4. If no `ACK` arrives within the budget of §11, the dongle drops the entry and **does nothing**.

**There is deliberately no failure haptic** (§R4). The referee's rule is one sentence and it is absolute: *no tap means the press did not land — press again.* A second press produces a second `EVT` with a new `seq`; the app applies it because it is a genuinely new event, and the score is right. The dangerous alternative is a "delivery failed" buzz, which the referee has to distinguish from an acknowledgement while looking at the mat.

**On `seq`:** the field does three jobs. It routes the tap to the originating wrist without the app tracking which remote it is answering. It lets the app dedupe, which is what makes the retry-free design safe. And a gap in it is a real finding about the link — on 3 cm of shielded cable there should never be one, and the app logs and counts every gap it sees.

The range is `0`–`65535` rather than v2.0's `0`–`999`. A held clock adjustment repeats at 150 ms; at that rate a 1000-entry space wraps in two and a half minutes, which is close enough to a match that a delayed duplicate and a genuine new event could collide on the same number. 65536 entries wrap in under three hours of *continuous* hold-repeat and never in practice.

**Order is receipt order.** The dongle assigns `seq` in the order events arrive from the radio; the scoreboard attributes them in the order it receives them. Remotes do not timestamp (FS §7.3). This assumes referee input intervals comfortably exceed transit variance — an assumption held open as a validation item, not a proven fact (see `RADIO_PROTOCOL.md` §6.4 and PLAN.md).

---

## 6. `STATE` — indicator assertion

```
STATE RED SOLID BLUE OFF RED
STATE GREEN OFF RED SOLID GREEN
```

One line asserts **the complete app-owned indicator state of one remote**. There is no partial update and no incremental command, because there is no version of this message that can leave a remote holding a stale half of its state.

| Arg | Values |
|---|---|
| `<remote>` | `RED` \| `GREEN` |
| `<f1>` | `OFF` \| `SOLID` — mode of `LED_F1` |
| `<f1colour>` | `RED` \| `GREEN` \| `BLUE` \| `YELLOW`. Ignored when mode is `OFF`; send `RED` by convention, the same role `000000` played under v3.0 — a syntactically valid placeholder for a state where the value doesn't matter. |
| `<f2>` `<f2colour>` | As above, for `LED_F2` |

### 6.1 What the app owns and what it does not

`LED_F1` and `LED_F2` are the app's. `LED_PWR` and `LED_LINK` are **remote-local and unreachable from this protocol** — the remote measures its own battery and detects its own link state (FS §2.1), and giving the app a way to drive them would create a second, slower, wrong opinion about facts the remote already holds.

### 6.2 Rendering rules

Modes are `OFF` and `SOLID` only. FS §10.3 makes counter rendering deliberately binary — off at zero, solid when non-zero — because the exact count is on the scoreboard and a referee mid-match is looking at the mat. The wrist LEDs answer one question: *does this athlete currently hold this state?*

Colour is per-role and comes from ruleset configuration (FS §12.2 `led_colour`), so a referee can tell a clock indicator from a counter or a flag without recalling which ruleset is loaded. The protocol carries *which* colour and has no opinion about what it means — but unlike v3.0, it is no longer a channel for an arbitrary value: `led_colour` and this argument are both drawn from the same fixed four-name palette the remote actually renders, not a hex value the app picked without knowing what the hardware could show true.

Blink modes are absent deliberately. There is exactly one blinking indicator in the system — `LED_PWR` below 10% — and it is remote-local. Every app-owned indicator answers a binary question, and adding a blink to this message would be adding a vocabulary for a distinction nothing needs to make.

### 6.3 `JOIN` — the reconnection case

```
JOIN RED
```

The dongle emits `JOIN <remote>` when a remote comes onto the radio link holding no indicator state: at first connection, after a transient radio interruption, and after a mid-match set substitution (FS §8.6).

**The app must answer with a `STATE` line for that remote, unconditionally.** Not "if something changed" — always. This is FS §7.3's *full indicator state must be assertable on demand*, and it is the mechanism that makes a hardware failure mid-match a physical swap: the scoreboard holds the only copy of ownership and flag state, the substituted remote has nothing of its own to be stale, and the correct state is one node away.

`JOIN` is advisory. A dongle that has just been plugged into a laptop mid-match has no way to know the app is even present, so a remote may `JOIN` before the app connects. Step 4 of the handshake (§4.2) covers that case by asserting state unprompted; `JOIN` covers the case where the app was already connected. Both paths end at the same `STATE` line, which is safe because `STATE` is idempotent.

### 6.4 `CFG`

```
CFG BOTH 80 60
```

| Arg | Values |
|---|---|
| `<target>` | `RED` \| `GREEN` \| `BOTH` |
| `<haptic>` | Master haptic intensity, 0–100 |
| `<bright>` | LED brightness, 0–100 |

Both are global scale factors, not per-waveform or per-indicator settings. **They cannot compress the amplitude separation of §9.1**, which is a firmware property of the waveforms themselves. `CFG BOTH 20 …` makes everything quieter; it does not make a heartbeat feel like an acknowledgement. That distinction is load-bearing for the scoring interface (FS §11.1) and is not exposed to configuration.

Applied on receipt, persisted by the dongle until reboot, re-sent by the app on every handshake.

### 6.5 `SIMSOC` — simulated state of charge

```
SIMSOC RED 42
```

| Arg | Values |
|---|---|
| `<target>` | `RED` \| `GREEN` \| `BOTH` |
| `<pct>` | 0–100 |

Bench-only. No fuel gauge exists before the nPM1300 stage of the remote build-out, so `LED_PWR` (FS §10.1) has nothing real to render from. `SIMSOC` sets a value on the targeted remote(s) purely for exercising the `LED_PWR` colour ladder end to end over the real wire path, rather than testing the rendering logic against a hardcoded constant.

**`SIMSOC` never feeds `UP_TELEMETRY.battery_pct`.** It is one-directional, app → remote, and affects only `LED_PWR`'s local rendering. A remote that echoed an operator-injected test value back as if it were a real telemetry reading would be exactly the trap `CLAUDE.md` already retired once (`CONFIG_DONGLE_FAKE_LINK`) — a signal that looks real and is not. Not persisted across a remote reboot; defaults to a mid-green value until set.

---

## 7. `LINK` — radio state and battery

```
LINK RED CONNECTED -52 87
LINK GREEN DISCONNECTED
```

| Arg | Values |
|---|---|
| `<remote>` | `RED` \| `GREEN` |
| `<state>` | `CONNECTED` \| `CONNECTING` \| `DISCONNECTED` |
| `[rssi]` | Signed dBm. **Required when `CONNECTED`**, absent otherwise. |
| `[batt]` | Battery percent, 0–100. Optional, only when `CONNECTED`. |

RSSI is mandatory rather than optional when connected. The app's signal indicator has no other source, and an implementation that omitted it would present as an indicator that silently never updates — the worst class of bug in a system whose whole safety argument is that failures are visible.

Emitted on every state change, for both remotes in reply to `INFO`, and re-emitted every 10 s while connected so the scoreboard's signal and battery indicators stay fresh without polling.

**Transitions must be debounced in the dongle.** A remote at the edge of range flaps, and each flap is a line; the app renders link loss as a primary-tier alarm (FS §8.4), so an undebounced link produces an indicator that strobes at a referee who is trying to officiate. Debounce policy belongs to the radio layer; this link only requires that what arrives here is already settled.

---

## 8. Liveness and supervision

Three independent supervision relationships exist, and conflating them is how this class of system fails.

| Who watches whom | Timeout | On expiry |
|---|---|---|
| Dongle watches app | 2500 ms since any received line | Emit `ERR APP_TIMEOUT`; instruct both remotes to render link-lost |
| App watches dongle | 2500 ms since any received line | Mark the link stale, surface it in the primary tier |
| Remote watches dongle | Radio-layer supervision | `LED_LINK` off, repeating double buzz (FS §10.2) |

**The app sends `PING` every 1000 ms whenever it is otherwise idle.** Any line resets the peer's timer, so during accrual the heartbeat carries liveness for free and the `PING` cadence costs nothing.

v2.0 used 2 s / 5 s. The tightening is affordable now and it was not before: v2.0's dongle-local heartbeat meant a short timeout risked cutting off a heartbeat that was still correct, so the timeout was set generously. In v3.0 the dongle generates nothing, so the only thing a supervision timeout controls is **how long a dead link can go on looking alive on the referee's wrist**, and 2.5 s of that is already more than one would choose.

**This mechanism covers browser crash, tab close, laptop sleep, cable pull, and application stall** — none of which a transport-level acknowledgement layer would catch, and all of which present identically to the referee: `LED_LINK` off and the link-lost buzz.

**Application stall is the case worth stating explicitly.** FS §8.3 identifies the dangerous failure as an app running but wedged, with the serial connection nominally alive, so the remotes see only an ambiguous silence. The app runs an internal watchdog and, on detecting that its own match-state processing has stalled, **deliberately drops the serial connection** — converting that ambiguity into the same unmistakable link-loss signal a radio fault produces. From this protocol's point of view an application fault and a pulled cable are indistinguishable, which is exactly the intent.

**Recovery is automatic.** The next line from the app restores the link. No state is resumed across a reconnect on the dongle side; the app re-runs §4.2 from scratch, and because step 4 asserts full indicator state, "from scratch" on the wire is not "from scratch" for the match.

---

## 9. `HAP` — haptic commands

```
HAP RED BEAT
HAP BOTH LONG
```

| Arg | Values |
|---|---|
| `<target>` | `RED` \| `GREEN` \| `BOTH` |
| `<waveform>` | `TAP` \| `BEAT` \| `WARN` \| `BUZZ` \| `LONG` \| `DOUBLE` \| `TRIPLE` |

**Waveforms name sensations, not events.** `HAP BOTH LONG` says *render the long buzz*; it does not say a period ended. This is §R2 applied to the haptic channel, and it is what lets the scoreboard add a notification — a new phase boundary, a new ruleset's warning — with no firmware change.

| Waveform | Character | Amplitude |
|---|---|---|
| `TAP` | Single short tap | Full |
| `BEAT` | Single short tap | **Distinctly reduced** — see §9.1 |
| `WARN` | Single distinct warning | Full |
| `BUZZ` | Short buzz | Full |
| `LONG` | Long buzz | Full |
| `DOUBLE` | Two pulses | Full |
| `TRIPLE` | Three pulses | Full |

### 9.1 The one amplitude that is a requirement

`BEAT` must be **unmistakably weaker than `TAP` on the wrist**, through a strap, in motion, to a referee not attending to it. Not different in principle — different in sensation.

Repeated-press scoring depends on the referee counting acknowledgement taps by feel (FS §5.3). During folkstyle riding time the same motor delivers a heartbeat every second, and a four-press near-fall entry takes roughly a second, so a heartbeat will frequently fall inside a scoring burst. A miscounted near fall is precisely the failure the acknowledgement design exists to prevent.

The separation is a firmware property of the waveform tables and is not reachable from `CFG` (§6.4). Whether one ERM plus driver IC can deliver it at all is an open validation item, not an established fact.

### 9.2 Scoreboard mapping — policy, not protocol

Reproduced from FS §11 so the waveform set can be read against what it has to carry. Changing this table is a scoreboard change.

| Event | Message |
|---|---|
| Input registered | `ACK <seq>` → `TAP` on the originating remote |
| Secondary-clock beat | `HAP <owner> BEAT`, once per second |
| Activity clock expiry | `HAP BOTH BUZZ` |
| Main clock warning (configurable, default 5 s) | `HAP BOTH WARN` |
| Period or match expiry | `HAP BOTH LONG` |
| Phase boundary (ADCC) | `HAP BOTH DOUBLE` |
| Link lost | *Remote-local.* Not on this link. |
| Low battery threshold | *Remote-local.* Not on this link. |

The last two rows matter. They are generated by the remote from state only the remote holds, and they are the two notifications that must still fire when this link is down.

### 9.3 The heartbeat is best-effort

`HAP <owner> BEAT` at 1 Hz is the highest-volume message on this link and the only one that may be dropped.

- **Never retried, never queued.** FS §7.3: an occasional dropped beat is tolerable and must not be retried at the expense of latency for anything else. A beat that arrives late is worse than a beat that never arrives.
- **Lowest transmit priority.** If the transmit ring is under pressure, `BEAT` is what gets dropped, and the drop is counted.
- **Sustained absence is meaningful and correct.** If the link drops, the beat stops. That is the intended failure behaviour, not a degradation of it.

**Burst suppression lives in the app.** FS §11.1 suppresses the heartbeat for a short window after any button press so a scoring burst delivers only acknowledgement taps. The app simply does not send `BEAT` during that window. It belongs there because the window duration is a tuning parameter (FS §15.6) that interacts with hold-repeat, and because the app is the only node that knows a burst is in progress. Suppression is bounded: a held clock adjustment repeating at 150 ms must not be able to silence the heartbeat indefinitely, which would falsely report that accrual had stopped.

---

## 10. Debug and test

### 10.1 Manual operation

Every command is typeable. With a terminal on the port:

```
INFO
STATE RED SOLID BLUE OFF RED
HAP BOTH LONG
CFG BOTH 100 100
ECHO hello
```

This exercises the entire firmware with no browser, no scoreboard and no harness, and is the fastest way to isolate whether a bug is in firmware or in the app.

**Caveat:** the supervision timeout of §8 fires after 2.5 s of manual idleness. That is correct behaviour. Type `PING` to hold it open, or use `TEST 3` to suspend supervision for bench work.

### 10.2 `TEST <mode>`

| Mode | Behaviour |
|---|---|
| `0` | Stop any running test mode; re-enable supervision |
| `1` | Emit one `EVT` per button, `PRESS`, alternating `RED`/`GREEN`, 500 ms apart — seven events |
| `2` | Emit random `EVT` lines continuously at ~5 Hz until stopped |
| `3` | Suspend link supervision until reboot or `TEST 0` (bench use only) |
| `4` | Sweep every gesture on every button: **16 events per remote, 32 total**, 250 ms apart |

Mode 1 gives the scoreboard a deterministic, repeatable stimulus with no remotes and no radio. Mode 4 is new in v3.0 and exists because the gesture axis is new: it is the only cheap way to confirm the app's `HOLD` and `HOLD_REP` handling before any remote hardware exists. Mode 2 is the soak driver.

**The sweep is 16 per remote, not 21, and the arithmetic is worth showing** because the obvious reading of "every gesture on every button" gives the wrong number. Seven buttons take `PRESS` and seven take `HOLD`, but §5.1 emits `HOLD_REP` **only for `FORWARD` and `BACKWARD`** — so 7 + 7 + 2 = 16. A sweep that produced 21 would be emitting `HOLD_REP` on five buttons that can never repeat in the field, which is a stimulus the product does not have and a `HOLD_REP` handler the app would be exercising against traffic no remote will ever send.

**`TEST 3` is a standing trap.** Left on, every supervision test passes for the wrong reason. Send `TEST 0` first and confirm the reply.

### 10.3 Logging

Log every line, both directions, with a direction tag and timestamp:

```
12:04:31.882  RX  EVT ADD_POINT PRESS RED 17
12:04:31.907  TX  ACK 17
```

Keep a raw-line panel in the app behind a debug toggle. The app must also maintain **running counters** — sequence gaps, dropped beats, acknowledgement latency distribution, connection uptime — because a one-off warning in a ring buffer has rolled over long before anyone reads it, which makes a soak test unfalsifiable.

---

## 11. The latency budget

FS §7.3: the confirmation haptic must fire within approximately **120 ms of the press, inclusive of any retries**. Where interference prevents delivery within budget, the failure must be surfaced rather than absorbed by extended retry.

That budget covers six hops, only two of which belong to this document.

| Hop | Allocation | Notes |
|---|---|---|
| Button edge → gesture classified | 15 ms | Debounce (FS §4.2). Fixed. |
| Remote → dongle over radio | 25 ms | Includes retransmission. See §12. |
| Dongle → app over USB | 5 ms | CDC-ACM bulk, 3 cm of cable. |
| App: parse, apply, render, reply | 25 ms | One reducer pass plus a paint. |
| App → dongle over USB | 5 ms | |
| Dongle → remote over radio | 25 ms | Includes retransmission. |
| Motor spin-up to perceptible | 20 ms | ERM. The hard floor. |
| **Total** | **120 ms** | |

**The two USB hops are not the risk.** They are 10 ms of a 120 ms budget on a link with hardware CRC and retransmission below the application. The risk is concentrated in the two radio hops and in the app's 25 ms, and both of those numbers are estimates that have never been measured on this hardware.

**Consequences for this protocol:**

- **The dongle must not batch.** An `EVT` goes out on the transmit ring immediately, and an `ACK` is turned around in the receive path rather than deferred to a timer.
- **The pending-entry lifetime is 120 ms, not 500 ms.** v2.0's window was set when the acknowledgement was a convenience; FS §5.3 makes acknowledgement fidelity a functional requirement of the scoring interface. An entry outliving the budget produces a tap the referee has already stopped waiting for and may attribute to their *next* press.
- **The app measures and reports.** `EVT`→`ACK` round-trip is instrumented, and the distribution is what matters — p99, not median. Any sample approaching 120 ms is a finding.
- **Exceeding the budget must degrade to no tap, never to a late tap.**

**Why a late tap is worse than no tap.** No tap invokes a rule the referee already has: press again. A late tap arriving during the next press is read as acknowledgement *of that press*, so the referee stops pressing while one point is still missing. Silence is recoverable; a misattributed tap is not, and it looks like referee error afterwards.

---

## 12. What this link assumes of the radio

Out of scope to specify, in scope to constrain. These are the assumptions the USB half is built on; the radio layer either meets them or this protocol's guarantees do not hold.

| Assumption | Why this link depends on it |
|---|---|
| **~25 ms one-way, inclusive of retransmission, at 12 m with body shadowing** | Half the acknowledgement budget of §11. Body attenuation at 2.4 GHz is the governing case, not free-space distance: the remotes are on the wrists of a referee circling the mat, so for much of a match one remote has the referee's torso between it and the dongle. |
| **Exactly-once delivery of button events, or visible loss** | §5.3 dedupes on `seq` and never retries. If the radio silently duplicates, the app sees a genuine duplicate `seq` and drops it. If the radio silently drops, no `EVT` is ever assigned a `seq`, no tap fires, and the referee presses again. Both are safe **only** if the radio never delivers a press as a *different* press. |
| **Ordered delivery per remote** | §5.3 attributes in receipt order. |
| **No cross-set association under any circumstance** | FS §2.3. Pairing is cryptographic and fixed in firmware, not proximity-based. Up to 30 sets — 90 devices — operate in one venue. A remote that associated with a neighbouring dongle would corrupt two matches at once. |
| **Debounced link state** | §7. |
| **Degradation visible before it is total** | FS §7.4. A link deteriorating but not yet failed is the most dangerous state, because inputs may be delayed or lost while the referee still believes the system works. RSSI on every `LINK` line is the app's only window onto this. |

**Two facts about the dongle hardware bear on the above.** The board is `raytac_mdbt50q_cx_40_dongle/nrf52840` — a Raytac MDBT50Q-P1M module, PCB trace antenna, nRF52840 at up to +8 dBm. It sits in a USB port on a laptop at the scoreboard table, which is a poor RF location: close to the host's own 2.4 GHz radios and often below table height with bodies between it and the mat. The link budget must be taken at the dongle as deployed, not on a bench.

The radio-layer choice — Bluetooth LE against Enhanced ShockBurst, and the connection-interval and retransmission parameters under it — is specified in `RADIO_PROTOCOL.md`, against these numbers. Status and measurement are tracked in PLAN.md.

---

## 13. Transport settings

| Setting | Value |
|---|---|
| Baud | 115200 (nominal; ignored by CDC-ACM, but set explicitly on both sides) |
| Data / parity / stop | 8 / none / 1 |
| Flow control | None |

**The protocol owns the CDC-ACM port exclusively.** Console, shell and logging must be disabled in the firmware, and a build-time assertion must fail the build if a second CDC-ACM instance appears. Anything else writing to the port interleaves bytes into the protocol stream: a log line over 120 bytes trips the §2.2 discard-and-resync path, and a write landing mid-line corrupts that line. Both failures are silent and intermittent. Diagnostics leave the dongle as `LOG` / `ERR` lines.

**USB identity.** The dongle must enumerate with a real VID/PID and product string, and `requestPort()` must carry a matching `filters:` array. Shipping Zephyr's test identity blocks the enterprise deployment path: a site policy allowlisting `0x2fe3` grants the origin access to *any* Zephyr device the user plugs in, which no IT department will approve.

---

## 14. Parser test cases

Both parsers must handle these without crashing, and must correctly parse the next valid line immediately after.

| # | Input | Expected |
|---|---|---|
| T1 | `EVT ADD_POINT PRESS RED 17\n` | Parsed |
| T2 | `EVT ADD_POINT PRESS RED 17\r\n` | Parsed, `\r` stripped |
| T3 | T1 delivered one byte per `read()` | Parsed identically |
| T4 | Three lines in a single `read()` | All three parsed, in order |
| T5 | `EVT ADD_PO` then `INT PRESS RED 17\n` in two reads | Parsed |
| T6 | `BOGUS FOO BAR\n` | Ignored, no crash |
| T7 | `EVT ADD_POINT RED 17\n` (v2.0 shape, missing gesture) | Ignored, logged |
| T8 | `EVT ADD_POINT PRESS RED xyz\n` (bad seq) | Ignored, logged |
| T9 | 200 bytes with no `\n`, then `\n`, then a valid line | Overlong buffer discarded; the line after the terminator parses |
| T9c | An overlong run split across several reads, then `\n`, then a valid line | Identical to T9 — the discard state survives the chunk boundary |
| T10 | `\n\n\nEVT ADD_POINT PRESS RED 17\n` | Empty lines ignored, event parsed |
| T11 | `EVT ADD_POINT PRESS RED 65536\n` | Ignored — seq out of range |
| T12 | `STATE RED SOLID BLUE OFF RED\n` | Parsed |
| T13 | `STATE RED SOLID ORANGE OFF RED\n` (not in the four-colour palette) | Ignored, logged |
| T14 | `LINK RED CONNECTED\n` (no rssi) | Ignored, logged — §7 makes rssi mandatory |
| T15 | `HAP BOTH SPIN\n` (unknown waveform) | Ignored, logged |
| T16 | `EVT` with the same `seq` twice | Applied once; the duplicate is dropped and counted |
| T17 | `SIMSOC RED 150\n` (out-of-range pct) | Ignored, logged |

T3, T4 and T5 are the ones that matter for framing. T7 is the v2.0-compatibility case and must fail closed: a v2.0 dongle talking to a v3.0 app is refused at the `HELLO` version check long before this, but a partially-updated firmware would present exactly this line and must never be interpreted as a gestureless press.

---

## 15. What changed from v2.0

| Change | Reason |
|---|---|
| `EVT` gains a gesture; actions become buttons | FS §7.3 ruleset-agnosticism, FS §4.1 gesture model. `TIME_UP` / `TIME_DOWN` / `PERIOD_UP` / `PERIOD_DOWN` named officiating operations and are gone. |
| `seq` widened to 0–65535 | Hold-repeat at 150 ms wraps a 1000-entry space inside a match. |
| `CONFIRM` → `ACK`, window 500 ms → 120 ms | FS §7.3 acknowledgement budget. Acknowledgement is a functional requirement, not a convenience. |
| `ACK … SILENT` added | FS §5.6 inert buttons must be fully inert. |
| `CLOCK RUN` / `CLOCK STOP` removed | The dongle holds no clock state. FS §6.2 forbids dongle-local heartbeat generation. |
| `EXPIRE` removed | Subsumed by `HAP BOTH LONG`. |
| `HAP` added | FS §11. Waveforms rather than events, per §R2. |
| `STATE` added | FS §7.3 full indicator state assertable on demand; FS §8.6 set substitution. |
| `JOIN` added | The reconnection trigger for `STATE`. |
| `CFG` added | FS §7.2 haptic intensity and LED brightness. |
| `HELLO` gains the set serial | FS §2.3, §12.3 pre-match confirmation of the bound officiating set. |
| Supervision 2 s/5 s → 1 s/2.5 s | Affordable once the dongle generates nothing; the timeout now bounds only how long a dead link looks alive. |
| `TEST 4` added | The gesture axis needs a stimulus that does not exist in hardware yet. |
| RSSI made explicitly mandatory when `CONNECTED` | It always was, in the app's parser. The spec now describes what shipped. |

**Retained unchanged:** framing and the §2.2 receiver rules; newline-delimited ASCII; the `HELLO` version handshake; sequence numbers as a loss-detection mechanism; link supervision as the primary fail-safe; exclusive ownership of the CDC-ACM port.

**When to reconsider the framing.** If the match clock ever moves onto the remotes, or telemetry arrives at real data rates, or the transport changes to one without built-in error detection, the binary framing of v1.0 becomes appropriate again. The `HELLO` version field is what lets that happen without breaking deployed hardware.

---

## 16. Version history

| Version | Change |
|---|---|
| 1.0 | Binary framing, CRC8, ACK/retry, dedup, 1 Hz `TIMER_STATE`. Superseded. |
| 2.0 | Newline-delimited ASCII. Dongle-local heartbeat driven by clock state. Transport ACK replaced by end-to-end `CONFIRM`. Link supervision as primary fail-safe. |
| 3.0 | Buttons and gestures rather than officiating actions. Scoreboard-commanded heartbeat and haptics. Full indicator state assertion. 120 ms acknowledgement budget. Officiating set identity. |
| 4.0 | `STATE`'s `<f1rgb>`/`<f2rgb>` hex arguments replaced by `<f1colour>`/`<f2colour>`, one of a fixed `RED`/`GREEN`/`BLUE`/`YELLOW` palette. Changed argument meaning — major by this section's own rule. |

Additive changes — new keywords, new optional trailing arguments — bump the minor version; unknown-keyword tolerance in §2.2 makes them non-breaking. Changing the meaning, argument count or argument order of an existing message bumps the major version.
