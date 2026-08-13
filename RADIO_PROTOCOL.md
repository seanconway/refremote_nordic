# RefRemote — Dongle ↔ Remote Radio Protocol

**Version:** 1.0 — amended 2026-08-10 in §12.2 and §12.3, which is deliberately not a version bump (§16.1)
**Link:** Bluetooth LE, nRF52840 dongle (central) ↔ two nRF52840 wrist remotes (peripherals), at a **7.5 ms connection interval, baseline BLE, no SCI** (§12.2)
**Scope:** the radio link between the dongle and the remote pair only. The wired link between the dongle and the scoreboard application is a separate protocol, specified in `PROTOCOL.md`; §13 states only what this link assumes of it.

**Governing documents:** *Project Scope* v1.1 and *Functional Specification* v2.1. Where this document and those disagree, they win. Section references of the form *FS §7.3* point at the functional specification, and *WP §12* at `PROTOCOL.md`.

**This is the counterpart to `PROTOCOL.md`.** WP §12 — *What this link assumes of the radio* — is the acceptance criteria of this document, and every row of it is answered here:

| WP §12 assumption | Answered in |
|---|---|
| ~25 ms one-way, inclusive of retransmission, at 12 m with body shadowing | §12 |
| Exactly-once delivery of button events, or visible loss | §6 |
| Ordered delivery per remote | §6.4 |
| No cross-set association under any circumstance | §10 |
| Debounced link state | §9.4 |
| Degradation visible before it is total | §9.5, §11 |

---

## 1. Design principles

The wire protocol's six principles hold here unchanged, because they are properties of the product rather than of a transport. Two are restated where the radio changes what they cost, and one is new.

**R1 — Do not re-implement the Link Layer.**
Bluetooth LE gives ordering, per-packet acknowledgement, retransmission until acknowledged, AES-CCM encryption, and adaptive frequency hopping. All of that is below the application and none of it is ours to write. What the Link Layer does **not** give is (a) visibility of loss once a connection drops, and (b) any notion that a packet has become worthless because it is late. Those two are the whole of what this protocol adds — see §6 and R6.

**R2 — The air carries buttons and waveforms, not officiating.**
Identical to WP §R2 and for the identical reason: FS §7.4 requires that adding a ruleset be a scoreboard change only, and the way that is guaranteed is by giving no node below the scoreboard a vocabulary that could need to change. The frame types of §5 are a one-to-one binary re-encoding of the USB message set. **The dongle changes the encoding and never the vocabulary.** If a radio frame type ever needs a name a wire message does not have, that is a signal that officiating meaning has leaked downward.

**R3 — State is idempotent; events are not.**
Indicator state (`DN_INDICATOR`) is asserted in full and may be re-sent freely. Presses (`UP_INPUT`) are delivered once and counted. Same discipline, same reasons, one hop further out.

**R4 — Absence is the failure signal.**
No tap means the press did not land. No heartbeat means accrual is not running. No connection means the link is down. Nothing on this link ever transmits a distinct "something went wrong" signal to the wrist, because the referee cannot be asked to tell an error buzz apart from four other buzzes while watching two athletes.

**R5 — Periodic behaviour lives where the state lives.**
The remote runs no match timer and holds no ownership. Every heartbeat is a `DN_HAPTIC BEAT` frame originated by the scoreboard, relayed by the dongle. FS §6.2 rejects the alternative by name; the per-second frame is the price of never diverging, and it doubles as continuous end-to-end liveness proof.

**R6 — Deadline, not delivery. *(new at this layer)***
This is the principle the radio adds, and it is the one most likely to be got wrong by a competent implementer following ordinary practice.

Bluetooth LE retransmits a packet until the peer acknowledges it. That is the correct behaviour for almost every application and the **wrong** behaviour for an acknowledgement tap. WP §11 is explicit: a late tap arriving during the referee's next press is read as acknowledgement *of that press*, so the referee stops pressing while one point is still missing. Silence is recoverable; a misattributed tap is not, and afterwards it looks like referee error.

So every downlink frame carries a lifetime, and a frame whose lifetime has expired is **discarded rather than transmitted**. The Link Layer will happily deliver a 400 ms-old acknowledgement; this protocol must not let it. §8.3 specifies the mechanism.

The rule generalises: on this link, **delivery is not success unless it was timely**. Uplink is symmetric — a press whose acknowledgement can no longer arrive within budget is worth nothing, and the remote does not retry it (§6.3).

**R7 — Binary on the air, ASCII on the wire.**
WP §R6 makes the USB link human-readable, and that property is worth its cost on 3 cm of shielded cable. On the air it is unaffordable, and not because of throughput: the ATT payload budget is what sets the shortest connection interval the controller will grant (§4.3), and the connection interval is half the latency argument. A `UP_INPUT` frame is four bytes.

Readability is not lost, it moves. Every frame the dongle relays becomes an ASCII line on the USB link, where the operator, the app's debug panel and a plain serial terminal can all read it. The radio is observable through the instrument that already exists.

---

## 2. Topology and roles

```
   ┌──────────────┐   BLE      ┌──────────────┐   BLE      ┌──────────────┐
   │ Remote RED   │◄──────────►│    Dongle    │◄──────────►│ Remote GREEN │
   │ peripheral   │  conn 0    │   central    │  conn 1    │  peripheral  │
   │ GATT server  │            │ GATT client  │            │ GATT server  │
   └──────────────┘            └──────┬───────┘            └──────────────┘
                                      │ USB CDC-ACM, PROTOCOL.md v3.0
                                 ┌────▼─────┐
                                 │Scoreboard│
                                 └──────────┘
```

| Node | LE role | GATT role | Rationale |
|---|---|---|---|
| Dongle | Central, initiator | Client | It holds the USB link and must schedule two connections. Central owns the connection parameters, the channel map and the timing of every connection event — the three things the latency and density arguments depend on. |
| Remote | Peripheral, advertiser | Server | The remote owns the characteristics that describe it. It is also the node that must survive being out of range and come back, and peripheral-side reconnection is the cheaper half. |

**The remote is the GATT server even though it is the node that originates presses.** That inversion is deliberate and it is the HID shape: the server is whoever *owns the data*, not whoever speaks first. The remote owns its inputs, its battery and its identity; the dongle reads and subscribes to them, and writes commands into a downlink characteristic. Making the dongle the server would put two service instances on the node with the tightest scheduling budget and would require each remote to discover and address the other's half of it.

**Exactly two connections, both fixed.** The dongle never scans for or connects to anything but the two identity addresses in its provisioning record (§10). There is no discovery, no field pairing, and no state in which a remote is unsure which dongle it belongs to.

---

## 3. The RefRemote Link Service

### 3.1 UUIDs

A vendor 128-bit base, so nothing here collides with an adopted service:

```
Base:            8f2a0000-6b1f-4d5a-9c3e-1d7b4a0e5c21
```

| Attribute | UUID | Properties | Direction |
|---|---|---|---|
| **RefRemote Link Service** | `8f2a0001-…` | — | — |
| `RR_IDENTITY` | `8f2a0002-…` | Read | Remote → dongle, once per connection |
| `RR_UPLINK` | `8f2a0003-…` | Notify | Remote → dongle |
| `RR_DOWNLINK` | `8f2a0004-…` | Write Without Response | Dongle → remote |

Three characteristics. Everything the wire protocol carries in sixteen messages fits into one read, one notification stream and one write stream, because the vocabulary is small and every frame is self-describing (§4.1).

**Every characteristic requires an encrypted link.** All three carry `BT_GATT_PERM_*_ENCRYPT` permissions, so an unencrypted peer that somehow reached the connection cannot read identity, subscribe to presses, or command a haptic. Encryption is established from the provisioned set key before discovery (§10.3); this permission is the belt to that braces.

### 3.2 `RR_IDENTITY`

Read once by the dongle immediately after encryption, before subscribing to `RR_UPLINK`.

| Offset | Size | Field | Notes |
|---|---|---|---|
| 0 | 1 | `radio_proto_major` | `1` in this revision |
| 1 | 1 | `radio_proto_minor` | `0` |
| 2 | 1 | `role` | `0x01` RED, `0x02` GREEN |
| 3 | 1 | `fw_major` | |
| 4 | 1 | `fw_minor` | |
| 5 | 1 | `fw_patch` | |
| 6 | 2 | `caps` | Little-endian bitfield. `0` here. Unknown bits ignored. |
| 8 | 12 | `set_serial` | ASCII, `[A-Z0-9-]`, NUL-padded. Matches `HELLO`'s `<set>` |

**Version handling mirrors WP §4.1.** Differing minor → the dongle emits a `LOG` line and continues. Differing major → the dongle **refuses the connection**, disconnects, reports that remote as `DISCONNECTED`, and emits `ERR REMOTE_PROTO_MISMATCH`. A mixed-firmware set is a scoring-integrity hazard of the same family as a mis-paired one, and it must present as a dead remote rather than as a remote that mostly works.

**The `set_serial` check is a second lock on the same door.** Cryptographic binding (§10.3) is what actually prevents cross-set association; a serial mismatch on an otherwise valid connection means a unit was mis-provisioned at manufacture, which is a bench fault. The dongle disconnects and emits `ERR SET_MISMATCH`. Catching it here costs four lines and catches it on the bench rather than in a match.

### 3.3 `RR_UPLINK` — notifications, not indications

Notifications. Not indications, and not read-with-polling.

An indication requires an ATT confirmation before the next indication may be sent, which serialises the uplink and adds a round trip inside the 25 ms allocation. The confirmation would also tell us nothing the Link Layer acknowledgement has not already told us — the LL acknowledges every PDU and retransmits until it is acknowledged, which is the guarantee that matters. Paying an ATT round trip to learn the same fact more slowly is the definition of re-implementing the Link Layer (R1).

The dongle subscribes by writing the CCCD after reading `RR_IDENTITY`. A remote must not queue notifications before it is subscribed; presses made in that window are dropped and counted (§6.5).

### 3.4 `RR_DOWNLINK` — write without response

Write Without Response, for two reasons and the second is the important one.

The first is cost: a write with response is a round trip, and the downlink is the half of the budget carrying the acknowledgement tap.

The second is R6. A write with response is *reliable*, and reliability here means the ATT layer will hold and retry the write until it completes. That is precisely the mechanism that turns a missed acknowledgement into a late one. An unreliable write that the Link Layer retransmits *for as long as the connection event allows* and then abandons is the behaviour this protocol wants — bounded effort, then silence.

---

## 4. Frame format

### 4.1 Common header

Every frame on `RR_UPLINK` and `RR_DOWNLINK`, in both directions:

```
byte 0:  TYPE      frame type (§5)
byte 1:  CTR       counter, 8-bit, wraps
byte 2+: payload   type-specific, 0–18 bytes
```

| Rule | Value |
|---|---|
| Byte order | Little-endian for multi-byte integers |
| Maximum frame | **20 bytes**, including the two header bytes — see §4.3 |
| Unknown `TYPE` | Ignore silently. This is what lets v1.1 add frames without breaking v1.0 peers |
| Wrong length for a known `TYPE` | Ignore, count, and emit one `LOG` line at the dongle |
| Padding | None. A frame is exactly as long as its type requires |

There is no framing problem to solve. ATT delivers whole attribute values, so a frame arrives complete or not at all — the line-assembly hazard that dominates WP §2.2 does not exist here. **That asymmetry is worth naming, because it is the single largest difference between the two protocols and it is easy to carry the wrong instinct across.** On USB the risk is a chunk boundary; here the risk is a deadline.

### 4.2 The counter

`CTR` is a per-direction, per-connection, 8-bit counter, incremented once per frame and wrapping at 256.

On the uplink it does real work and §6 is about it. On the downlink it is diagnostic only: the remote counts gaps and reports the total in `UP_TELEMETRY`, which is how a downlink losing frames becomes visible before it becomes total (FS §7.4).

**`CTR` is not the wire protocol's `seq` and the two must never be conflated.** They have different widths because they have different jobs, and this is the same reasoning WP §5.3 applies to its own width choice:

| | Radio `CTR` | Wire `seq` |
|---|---|---|
| Width | 8-bit | 16-bit |
| Assigned by | The remote, per remote | The dongle, across both remotes |
| Job | Make radio loss visible; reject a replay | Route the tap, dedupe at the app, detect USB loss |
| Wrap horizon | 256 frames ≈ 38 s of continuous hold-repeat | 65 536 frames ≈ 2.7 h |

Eight bits is sufficient here where sixteen was needed there, because the collision the wire protocol guards against — a delayed duplicate meeting a genuine new event on the same number — requires a delay measured in minutes, and nothing on this link survives more than a connection interval. A duplicate that could arrive 38 seconds late would mean a queue we do not have and would not permit (R6).

The dongle translates: it assigns a fresh 16-bit `seq` to every accepted `UP_INPUT` in order of receipt, exactly as `send_evt()` does today. `CTR` never appears on the USB link except inside a `LOG` line.

### 4.3 Why 20 bytes, and why it is a hard ceiling

The default ATT MTU is 23 bytes, so a notification or write carries at most 20 bytes of value. Adding the 3-byte ATT header and the 4-byte L2CAP header gives **27 bytes of Link Layer payload** — which is exactly the default LL data length, and exactly the figure NCS names as a precondition for the shortest connection intervals:

> *"For the Shorter Connection Intervals feature, the minimum connection interval can be achieved using the lowest supported ACL frame space, 2 Mbps PHY, and 27-byte data length."*
> — `nrfxlib/softdevice_controller/README.rst`, NCS v3.4.0

So the payload ceiling and the latency floor are the same constraint seen from two directions.

**Therefore: do not negotiate a larger ATT MTU, and do not enable Data Length Extension.** Both are ordinary, sensible, well-regarded optimisations, and both would silently raise the minimum connection interval the controller will grant — trading the latency budget for throughput this product does not need and cannot spend. Every frame in §5 fits in 20 bytes with room to spare; the largest is 10.

A firmware change that raises the MTU will not fail a test. It will lengthen the acknowledgement tail by milliseconds that nobody attributes to it. This paragraph exists so that the next person to consider it finds the reason first.

---

## 5. Frame types

### 5.1 Uplink — remote → dongle

| `TYPE` | Name | Len | Meaning | § |
|---|---|---|---|---|
| `0x01` | `UP_INPUT` | 4 | A referee press. The only match-affecting path. | 6 |
| `0x02` | `UP_READY` | 4 | This remote holds no indicator state. Drives `JOIN`. | 7.2 |
| `0x03` | `UP_TELEMETRY` | 6 | Battery, charging, downlink loss counters. | 9.3 |
| `0x04` | `UP_DIAG` | ≤20 | Freeform counters for a `LOG` line. Never semantic. | 6.1 |

### 5.2 Downlink — dongle → remote

| `TYPE` | Name | Len | Meaning | § |
|---|---|---|---|---|
| `0x81` | `DN_HAPTIC` | 4 | Render one waveform, now or not at all. | 8 |
| `0x82` | `DN_INDICATOR` | 10 | Complete app-owned indicator state. Idempotent. | 7 |
| `0x83` | `DN_CONFIG` | 4 | Haptic intensity and LED brightness, 0–100. | 7.3 |
| `0x84` | `DN_HOST` | 3 | Whether the scoreboard half of the path is alive. | 9.2 |

Four and four. The mapping to `PROTOCOL.md` is one-to-one in the direction that matters:

| Wire message (WP) | Radio frame | Note |
|---|---|---|
| `EVT <button> <gesture> <src> <seq>` | `UP_INPUT` | Dongle assigns `seq`, fills `src` from the connection |
| `ACK <seq>` | `DN_HAPTIC TAP` | Routed to the remote recorded against that `seq` |
| `ACK <seq> SILENT` | *nothing sent* | Inert must be fully inert (FS §5.6) |
| `STATE <remote> …` | `DN_INDICATOR` | Field-for-field |
| `HAP <target> <waveform>` | `DN_HAPTIC` | One frame per target |
| `CFG <target> <h> <b>` | `DN_CONFIG` | One frame per target |
| `JOIN <remote>` | `UP_READY` | |
| `LINK <remote> <state> <rssi> <batt>` | connection state + `UP_TELEMETRY` | §9 |
| `ERR APP_TIMEOUT` (dongle-originated) | `DN_HOST DOWN` | §9.2 |
| `HELLO`, `PING`/`PONG`, `INFO`, `ECHO`, `TEST`, `LOG` | *never leave the USB link* | Dongle-local |

`ACK … SILENT` sending nothing is worth pausing on. The inert case (FS §5.6) is not "send a silent waveform"; it is **no frame at all**, so no air time, no motor, and nothing for the remote to decide. The distinction FS draws between *inert* and *no-op* survives intact: a no-op press — `REMOVE_POINT` at the score floor — produces an ordinary `ACK` and therefore an ordinary `DN_HAPTIC TAP`, because the referee needs to know the press registered.

### 5.3 `UP_INPUT`

```
0: 0x01
1: CTR
2: button    0x01 ADD_POINT   0x02 TOGGLE_CLOCK  0x03 REMOVE_POINT
             0x04 FORWARD     0x05 BACKWARD      0x06 F1  0x07 F2
3: gesture   0x01 PRESS       0x02 HOLD          0x03 HOLD_REP
```

Buttons are named by **position**, per WP §5.1 and FS §3.1 — `ADD_POINT` and `REMOVE_POINT` keep their functional names only because FS §5.1 fixes those functions across every ruleset, and they are still transported as buttons. `F1` and `F2` are deliberately unnamed.

**Gesture classification happens in the remote and never crosses this link as a parameter.** Debounce 15 ms, hold threshold 600 ms, hold-repeat 150 ms are firmware constants (FS §4.2), so classification is immediate and identical regardless of link state — including while the link is *down*, which is when a referee is most likely to press repeatedly.

`HOLD` is emitted once, at the moment the threshold is crossed, not on release: the referee's feedback has to arrive while the finger is still down. `HOLD_REP` is emitted only for `FORWARD` and `BACKWARD`.

### 5.4 `DN_HAPTIC`

```
0: 0x81
1: CTR
2: waveform  0x01 TAP  0x02 BEAT  0x03 WARN  0x04 BUZZ
             0x05 LONG 0x06 DOUBLE 0x07 TRIPLE
3: ttl       lifetime in units of 4 ms, 1–255; 0 = no deadline
```

Waveforms name **sensations, not events** (WP §9). The frame says *render the short tap*; it does not say a point was scored. This is R2 applied to the haptic channel and it is what lets the scoreboard add a notification with no firmware change at either end.

`ttl` is the R6 mechanism and §8.3 specifies its handling. Defaults set by the dongle:

| Waveform | `ttl` | Why |
|---|---|---|
| `TAP` | 15 (60 ms) | The remaining share of the 120 ms acknowledgement budget at the moment of transmission |
| `BEAT` | 50 (200 ms) | A beat is a state indicator, not a metronome; a fifth of a second of jitter is imperceptible in a one-second interval, and a beat later than that has been overtaken by the next one |
| `WARN`, `BUZZ`, `LONG`, `DOUBLE`, `TRIPLE` | 0 | Notifications of a real-world event the referee will act on. Late is degraded but still correct — a period-expiry buzz 200 ms late is a period-expiry buzz |

**That last row is the whole of why `ttl` is per-frame rather than a global rule.** "Late is worse than never" is true of the acknowledgement tap, on which repeated-press scoring depends (FS §5.3, FS §11.1). It is *not* true of a period-expiry buzz, and applying the acknowledgement's discipline to expiry would throw away a notification the referee needs, to protect a property that notification does not have.

Amplitude is not a field. `BEAT` is distinctly weaker than `TAP` as a property of the remote's waveform tables (FS §11.1, WP §9.1), and it is **not reachable from `DN_CONFIG`** — see §7.3.

### 5.5 `DN_INDICATOR`

```
0: 0x82
1: CTR
2: f1_mode   0x00 OFF  0x01 SOLID
3: f1_r  4: f1_g  5: f1_b
6: f2_mode
7: f2_r  8: f2_g  9: f2_b
```

One frame asserts the **complete app-owned indicator state of one remote**. There is no partial update and no incremental command, because there is no version of this frame that can leave a remote holding a stale half of its state (WP §6).

`OFF` and `SOLID` only. FS §10.3 makes counter rendering deliberately binary — the exact count is on the scoreboard, and the wrist LEDs answer one question: *does this athlete currently hold this state?* There is exactly one blinking indicator in the system, `LED_PWR` below 10%, and it is remote-local and unreachable from here (§7.4).

### 5.6 `UP_TELEMETRY`

```
0: 0x03
1: CTR
2: battery_pct   0–100
3: flags         bit0 charging, bit1 low-battery latch, bits2-7 reserved
4: dn_lost       downlink CTR gaps observed since connection, saturating at 255
5: reserved      0
```

Emitted on connection, on any change to `battery_pct` of more than one percentage point, on any change to `flags`, and otherwise every 10 s. The 10 s floor exists so that the dongle's `LINK` re-emission cadence (WP §7) always has a fresh value to report rather than a cached one that could be arbitrarily old.

`dn_lost` is the remote's window onto downlink health, and it is the only one. A remote cannot tell a suppressed heartbeat from a lost one — that is by design, since the app suppresses beats during a scoring burst (FS §11.1) — but it can count frames that were sent and never arrived, because `CTR` gaps are unambiguous.

---

## 6. Exactly-once, and what actually guarantees it

FS §7.3 is unambiguous: a silently dropped or doubled scoring input corrupts the match score with no external indication, and repeated-press scoring makes it worse — one dropped press in a sequence of four produces a plausible wrong score rather than an obvious fault.

### 6.1 The mechanism

1. The remote assigns `CTR` in press order and notifies `UP_INPUT`.
2. The Link Layer transmits it and retransmits until the dongle's controller acknowledges. Ordering is guaranteed per connection; duplicates are rejected by the Link Layer's own sequencing.
3. The dongle checks `CTR` against the last accepted value for that connection:
   - **Expected** (`last + 1`): accept.
   - **Duplicate** (`≤ last`, within a window of 8): discard, increment `radio_dup`, emit a `LOG` line.
   - **Gap** (`> last + 1`): accept, and increment `radio_gap` by the size of the gap. **A gap is a real finding**, not a warning to be swallowed — see §6.4.
4. The dongle assigns a 16-bit `seq`, records the originating remote against it in the pending table, and emits `EVT <button> <gesture> <src> <seq>` on the USB link.
5. From there WP §5.3 takes over unchanged.

### 6.2 What each layer actually contributes

Worth stating plainly, because the guarantee is easy to attribute to the wrong place:

| Property | Provided by |
|---|---|
| No reordering | Link Layer, per connection |
| No duplication on an intact connection | Link Layer sequencing (SN/NESN) |
| Retransmission until acknowledged | Link Layer, within the connection event budget |
| **Loss made visible** | **`CTR` gap detection — this protocol** |
| **Replay rejection across a reconnect** | **`CTR` window — this protocol** |

The honest summary is that on an intact connection the Link Layer already delivers exactly-once, and `CTR`'s primary work is the fourth row: **turning a loss that the Link Layer could not prevent into a number somebody can read.** Replay rejection is cheap insurance against a path we do not believe exists — Zephyr discards unsent notifications on disconnect rather than replaying them — and it costs one byte and one comparison.

That is not a reason to omit it. WP §5.3 already treats a `seq` gap on 3 cm of USB cable as a finding worth counting; a gap over twelve metres of contested 2.4 GHz is the thing the whole density and range argument is about, and without `CTR` it would be invisible, because a press that never arrives generates no wire traffic at all.

### 6.3 No application-level retry, on either side

The remote does **not** retry `UP_INPUT`, and the dongle does not ask it to.

Link-layer retransmission inside the connection event is bounded effort, and it is the right amount of effort: at the 7.5 ms baseline interval the 25 ms one-way allocation affords roughly two attempts, and at 2.5 ms roughly eight (§12). An application-level retry on top of that would only ever fire when the link-layer retries had *already* exhausted the budget — that is, at a moment when the press is already worthless.

Note that the argument does not depend on the rung. Fewer affordable retransmissions makes a press **more** likely to be lost outright, which is the visible, recoverable failure this design wants; it does not make a late retry any more useful.

So a press that does not arrive produces no `EVT`, no `ACK`, and no tap. The referee's rule handles it, and the rule is one sentence and absolute: **no tap means the press did not land — press again.** A second press produces a second `UP_INPUT` with a new `CTR`, a genuinely new `seq`, and a correct score.

**There is deliberately no failure haptic** (R4). A "delivery failed" buzz would have to be distinguished from an acknowledgement by a referee looking at the mat, and getting that wrong costs a point.

### 6.4 Order is receipt order, and remotes do not timestamp

The dongle assigns `seq` in the order frames arrive from the radio; the scoreboard attributes in the order it receives them (FS §7.3, WP §5.3).

**Ordering is guaranteed per connection but not between them.** Two connections are two independent schedules, and a press on RED and a press on GREEN a millisecond apart may arrive at the dongle in either order depending on where each connection's next event falls. The attribution window is bounded by the connection interval, so it is a few milliseconds wide at 2.5 ms and roughly three times that at the 7.5 ms baseline.

**Choosing rung 3 therefore widens this window**, and it is the one place where the baseline decision of §12.2 costs something real rather than merely deferring a feature. It is still small against a human pressing two buttons on opposite wrists, but it makes the R5 measurement below more pressing rather than less.

This is acceptable, and it is acceptable for a specific reason rather than by assumption: the two remotes are on the two wrists of **one referee**, and a human cannot press two buttons on opposite wrists within a few milliseconds and mean them as an ordered pair. There is no officiating operation whose meaning depends on which of two near-simultaneous cross-wrist presses came first.

It is nonetheless the assumption behind FS §15.1 and PLAN.md R5, and this document does not settle it. **The measurement to make is inter-press interval during live matches against measured cross-connection arrival skew, not median transit time.** If it fails, the remedy is remote-side timestamping, which adds protocol complexity here and reordering logic at the scoreboard — and is a change to this document, not a tuning parameter.

### 6.5 Presses that arrive with nowhere to go

A press can arrive at the dongle while the scoreboard is absent: the app has not connected, the cable is out, the browser crashed, or the watchdog dropped the link (FS §8.3).

**The dongle drops it.** It does not queue it. A queued press applied minutes later is a wrong score with no visible cause, and it would arrive with no acknowledgement tap to warn the referee it had happened.

The drop must be **counted** (`presses_dropped_no_host`) and reported in the first `LOG` line after the app returns. And the referee already knows, before pressing, because `DN_HOST DOWN` has put the remote into the link-lost rendering of FS §10.2 — `LED_LINK` off and the repeating double buzz. §9.2.

---

## 7. Indicator state and configuration

### 7.1 Assertion, never accumulation

`DN_INDICATOR` is idempotent and always complete. The dongle relays every `STATE` line it receives from the app, unconditionally, without comparing against what it last sent. It holds no cache of indicator state to compare against — a cache would be dongle-held state that can diverge from the scoreboard's, which is the failure mode FS §6.2 exists to prevent, arriving through a different door.

Re-sending an unchanged `DN_INDICATOR` costs one 10-byte frame. Holding a cache costs a class of silent divergence. The trade is not close.

### 7.2 `UP_READY` and the reconnection chain

```
0: 0x02
1: CTR
2: ctr_base    the value CTR will take on the next uplink frame
3: reason      0x01 boot  0x02 reconnect
```

A remote emits `UP_READY` as its first uplink frame after subscription, whenever it holds no indicator state: at first connection, after a transient radio interruption, and after a mid-match set substitution (FS §8.6).

The chain it starts is the mechanism that makes a hardware failure mid-match a physical swap and nothing more:

```
remote          UP_READY ──►  dongle  ── JOIN RED ──►  app
                                                        │
remote  ◄── DN_INDICATOR ──   dongle  ◄── STATE RED ────┘
```

**The app must answer with a `STATE` line unconditionally** — not "if something changed", always (WP §6.3). The scoreboard holds the only copy of ownership and flag state; the substituted remote has nothing of its own to be stale; the correct state is one node away.

`ctr_base` exists so that a remote which has rebooted — and therefore restarted its counter — does not present as a 200-frame gap in the dongle's loss statistics. The dongle re-baselines to `ctr_base` and logs the rebaseline rather than the gap. A remote that *reconnected* without rebooting sends its continuing value, and a genuine gap across the disconnection is reported as one, because presses made while out of range genuinely were lost.

### 7.3 `DN_CONFIG`

```
0: 0x83
1: CTR
2: haptic_scale    0–100, master haptic intensity
3: led_brightness  0–100
```

Applied on receipt, held until reboot, re-sent by the dongle whenever the app re-runs its handshake.

Both are **global scale factors**, not per-waveform or per-indicator settings, and they **cannot compress the amplitude separation between `BEAT` and `TAP`** (WP §6.4). `DN_CONFIG` at 20 makes everything quieter; it does not make a heartbeat feel like an acknowledgement. That separation is a property of the waveform tables in remote firmware, it is load-bearing for the scoring interface (FS §11.1), and it is deliberately not exposed to configuration at any layer.

A remote implementation that applies `haptic_scale` by scaling a single amplitude parameter shared by both waveforms would satisfy this frame's contract and violate the requirement behind it. The scale must preserve the *ratio*.

### 7.4 What the downlink cannot touch

`LED_PWR` and `LED_LINK` are remote-local and unreachable from this protocol, exactly as they are unreachable from the wire protocol (WP §6.1).

The remote measures its own battery and detects its own radio link state (FS §2.1). Giving the dongle a way to drive those indicators would create a second, slower, wrong opinion about facts the remote already holds. The one nuance is what "link" means, and §9.2 is about that.

---

## 8. Haptic delivery and the deadline rule

### 8.1 The remote renders; it decides nothing

On `DN_HAPTIC` the remote checks the deadline (§8.3), then drives the waveform. It does not know what the waveform means, does not know whether a match is running, and holds no queue of pending haptics beyond the one being rendered.

If a frame arrives while the motor is running, the arriving frame **wins and restarts the motor**. It does not queue behind the running one. A queued haptic is a late haptic, and R6 disposes of it; more concretely, the collision this rule governs is the one FS §11.1 names — a heartbeat landing inside a scoring burst — and there the correct outcome is unambiguously that the acknowledgement tap is what the referee feels.

### 8.2 Priority

Where the dongle must choose, and where the remote must choose:

| Class | Frames | Discipline |
|---|---|---|
| Highest | `TAP` | Never dropped by the dongle while inside its `ttl`. The scoring interface depends on it |
| High | `WARN`, `BUZZ`, `LONG`, `DOUBLE`, `TRIPLE` | Never dropped. No deadline |
| Normal | `DN_INDICATOR`, `DN_CONFIG`, `DN_HOST` | Never dropped; idempotent, so a retry is free if one is ever needed |
| **Lowest** | `BEAT` | **The only frame on this link that may be dropped.** Dropped first under any pressure, and counted |

`BEAT` at 1 Hz per owning remote is the highest-volume downlink frame and the only best-effort one (FS §7.3, WP §9.3). Never retried, never queued. A sustained absence of beats is meaningful and correct: if the link drops, the beat stops, and that is the intended failure behaviour rather than a degradation of it.

**Burst suppression stays in the app** (FS §11.1, WP §9.3). The window duration is a tuning parameter that interacts with hold-repeat, and the app is the only node that knows a burst is in progress. Nothing in the dongle or the remote suppresses anything.

### 8.3 Enforcing the deadline

Three mechanisms, in the order they should be applied:

**1. Do not enqueue a frame that is already late.** The dongle timestamps each `ACK` on arrival from USB and computes the remaining share of the 120 ms budget. If that share is already spent, the frame is **not sent at all**; `taps_dropped_late` is incremented and a `LOG` line is emitted. This is the cheapest and most reliable of the three, it needs no controller feature, and it catches the case that actually happens — an app or USB stall.

**2. Keep at most one outstanding `TAP` per connection.** If a second `ACK` arrives for the same remote while a `TAP` is still queued in the controller, the queued one is already worthless: it is about to be delivered after the press that superseded it. Replace rather than append.

**3. Mark deadline-bearing frames flushable, where the controller allows.** LE Flushable ACL Data lets the controller discard a queued packet instead of retransmitting it indefinitely, which is exactly R6 expressed at the layer that owns the queue. NCS v3.4.0 lists the feature under Multirole and marks the support **experimental**, so it is specified here as an optimisation and not as the mechanism the guarantee rests on. **Mechanisms 1 and 2 must hold with it disabled.**

The remote applies the same test on receipt: if more than `ttl` has elapsed since the frame was queued — which the remote cannot know directly, and therefore approximates by discarding any `TAP` arriving in a connection event more than `ttl` after the event in which it could first have been sent — it discards rather than renders. Where the remote cannot make that determination confidently, it renders: mechanism 1 at the dongle is the one that carries the guarantee, and a remote guessing at deadlines it cannot measure would drop taps that were fine.

---

## 9. Link state, supervision, and what `LED_LINK` means

### 9.1 Four supervision relationships

The wire protocol names three (WP §8). Adding the radio makes four, and the fourth is the one this section exists for.

| Who watches whom | Mechanism | Timeout | On expiry |
|---|---|---|---|
| Dongle watches app | Any received USB line | 2500 ms | `ERR APP_TIMEOUT`; **`DN_HOST DOWN` to both remotes** |
| App watches dongle | Any received USB line | 2500 ms | Link stale, surfaced in the primary tier |
| Dongle watches remote | LE supervision timeout | 1000 ms | Connection dropped; `LINK <remote> DISCONNECTED` after debounce (§9.4); reconnect |
| **Remote watches dongle** | LE supervision timeout | 1000 ms | `LED_LINK` off, repeating double buzz (FS §10.2) |

**Peripheral latency is zero on both connections and must stay zero.** Peripheral latency is the standard power lever for a BLE peripheral, and it is precisely the one this product cannot use: it permits the peripheral to skip up to *N* connection events, which delays every downlink frame — including the acknowledgement tap — by up to *N* intervals. R6 forbids it. Connection subrating has the same problem for the same reason and is not used in this revision. Power must be found elsewhere (§12.4).

### 9.2 `DN_HOST` — why the remote cannot detect its own link state alone

```
0: 0x84
1: CTR
2: state    0x00 DOWN  0x01 UP
```

FS §2.1 lists link status among the three things a remote holds locally. FS §10.2 defines the indication as **"connected end to end."** Those are consistent only if the remote is told about the half of the path it cannot see.

The remote can observe its radio connection to the dongle. It cannot observe the USB cable, the browser tab, the laptop's sleep state, or an application watchdog deliberately dropping the serial link (FS §8.3) — and every one of those leaves the radio connection perfectly healthy while the scoreboard is gone. A remote rendering `LED_LINK` from radio state alone would show solid blue, with no buzz, to a referee whose presses were going nowhere. **That is the exact failure the indicator exists to prevent.**

So:

```
LED_LINK solid  ⟺  radio connection up  AND  last DN_HOST said UP
```

The remote computes the conjunction. The radio half it detects; the host half it is told. It still holds no match state, and it still holds its own link state — the state simply has two inputs.

The dongle sends `DN_HOST DOWN` on app supervision expiry and `DN_HOST UP` on the next line received from the app, and re-sends the current value to any remote that connects. WP §8 already requires this behaviour of the dongle in prose — *"instruct both remotes to render link-lost"* — and `DN_HOST` is the frame that discharges it.

**On boot the remote assumes `DOWN`** until told otherwise. A remote powered on next to a dongle with no laptop attached must show link-lost, because that is what is true.

### 9.3 RSSI and battery

`LINK <remote> CONNECTED <rssi> <batt>` requires an RSSI value whenever connected (WP §7) — it is mandatory rather than optional because the app's signal indicator has no other source, and an implementation that omitted it would present as an indicator that silently never updates.

| Value | Source | Cadence |
|---|---|---|
| `rssi` | Dongle-side, HCI Read RSSI on the connection, **averaged over the last 8 connection events** | Sampled on the `LINK` re-emission tick |
| `batt` | `UP_TELEMETRY.battery_pct`, measured by the remote | Pushed on change or every 10 s |

RSSI is averaged rather than instantaneous because a single reading is a sample of one hop of a frequency-hopping link, and the channel-to-channel spread in a hall with Wi-Fi present can exceed the trend the referee's operator is trying to read. An unaveraged figure would make the indicator jitter across its whole range while the link was perfectly stable, which trains the operator to ignore it — and the indicator's entire job is FS §7.4's *degradation must be visible before it is total*.

### 9.4 Debouncing link state

WP §7 requires that what reaches the USB link is already settled: the app renders link loss as a primary-tier alarm (FS §8.4), so an undebounced link produces an indicator that strobes at a referee who is trying to officiate.

| Transition | Rule |
|---|---|
| → `CONNECTED` | Reported only after the connection is encrypted, `RR_IDENTITY` has been read and validated, and the CCCD subscribed. A half-established connection is not a connected remote |
| → `DISCONNECTED` | Reported after **2 s** with no connection re-established |
| → `CONNECTING` | Reported immediately on disconnection, so the app has something true to show during the debounce window |

The asymmetry is deliberate. Connection is reported late because a connection that is not yet usable is not a connection. Disconnection is reported through an intermediate state rather than late, because the referee must not be told everything is fine while it is not — `CONNECTING` is honest, and a remote that reconnects inside 2 s never produces a `DISCONNECTED` line at all.

**The remote's own indication is not debounced.** It renders link-lost the moment its supervision timer expires. The debounce exists to keep an alarm off the scoreboard, not to keep the referee uninformed; those are different audiences with different costs of a false alarm.

### 9.5 Reconnection

| Parameter | Value | Note |
|---|---|---|
| Advertising after disconnection | Directed, 20 ms interval, for 2 s | Fast path back for a transient fade |
| Then | Undirected connectable, 100 ms interval, indefinitely | With the dongle's address in the accept-list |
| Dongle initiation | Continuous, filtered to the two provisioned addresses | The dongle never stops trying |
| Advertising interval jitter | ±10 ms, randomised per attempt | So thirty sets recovering from the same interference event do not synchronise |

No state is resumed on reconnect. The remote sends `UP_READY`, the chain of §7.2 runs, and indicator state is asserted afresh from the only node that holds it.

**"Never stops trying" is per remote, not literally two searches at once.** The Bluetooth host on the dongle allows exactly one outstanding connection-creation attempt system-wide, so when both remotes want one simultaneously — at boot, or a simultaneous dual-loss — the dongle time-shares the single attempt between them in bounded turns rather than letting either search run unbounded and starve the other. Once only one remote is trying (the normal mid-match case: the other is already connected), that one holds the attempt uncontested and it runs unbounded exactly as the table above describes — the sharing only ever activates under actual contention, and never affects a remote that is already connected. `dongle/src/radio_ble.c`'s header comment has the mechanism.

---

## 10. Set binding and security

FS §2.3 states the requirement and the reason: **cross-system association would be a scoring-integrity failure, not an inconvenience.** A remote accepting commands from, or delivering input to, a neighbouring mat corrupts two matches at once and would not necessarily be obvious to either referee. Up to 30 sets — 90 devices — operate in one hall. Pairing must be cryptographically enforced rather than proximity-based.

### 10.1 The provisioning record

Written once at manufacture into a dedicated flash partition on all three units of a set, and never written again in the field:

| Field | Size | Notes |
|---|---|---|
| `magic`, `version` | 4 | Record format version |
| `set_serial` | 12 | ASCII `[A-Z0-9-]`, NUL-padded. The serial printed on all three cases |
| `role` | 1 | `DONGLE`, `RED`, `GREEN` |
| `own_addr` | 6 | LE static random identity address |
| `peer_addr[2]` | 12 | The other two units of the set. The dongle carries both remotes; a remote carries the dongle and a zero slot |
| `set_key` | 16 | The shared set key. Never transmitted |
| `crc32` | 4 | Over the record |

A unit whose record fails its CRC, or is absent, **does not advertise and does not initiate**. It renders an unmistakable fault indication and, if it is the dongle, emits `ERR NO_PROVISIONING` on the USB link. An unprovisioned unit that behaved like a normal one — advertising, connectable — is a unit that can be associated with something, and there is no safe default for "which set am I in."

### 10.2 Addressing

Both connections use **LE static random identity addresses** fixed at provisioning. No resolvable private addresses and no address rotation.

Privacy is the usual reason to rotate, and it does not apply: these devices are worn openly by an official, in a hall, for a scheduled event, and the tracking risk is not the threat model. What rotation *would* cost is directness — the dongle could no longer filter its initiator on a literal address, and a resolution step would sit in the reconnection path that FS §8.6's field substitution wants to be as short as possible.

The dongle initiates only to `peer_addr[0..1]`. Each remote advertises with the dongle's address in its accept-list. A neighbouring set's dongle is not an address either remote will answer.

### 10.3 Encryption without a pairing procedure

**No pairing procedure is ever performed, at manufacture or in the field.**

On each connection, both ends install the provisioned `set_key` as the Long Term Key via `bt_nrf_conn_set_ltk()` — an NCS host extension available since v3.2.0 and present in the pinned v3.4.0 (`nrf/include/bluetooth/nrf/host_extensions.h`) for exactly this case, two devices with a shared proprietary method of obtaining an LTK. The dongle then raises security, and **no GATT operation is permitted before encryption completes** (§3.1). A connection that fails to encrypt is disconnected and counted.

The Security Manager is compiled in as plumbing, but:

- Pairing requests are **rejected**, in both directions.
- The device is not bondable.
- There is no bond store to erase, migrate, corrupt, or diverge, and no DFU that can silently unpair a set.

**Why not bond at manufacture.** Standard LE Secure Connections pairing performed once on a provisioning bench would satisfy FS §2.3 and uses only stock APIs. It was rejected because it stores the resulting binding in a settings partition that a firmware update, a settings-schema change, or a mis-scripted flash can clear — and a set that has silently forgotten its binding presents in the field as two remotes that will not connect, at an event, with no diagnostic. The provisioning record is read-only data in a partition of its own, verified by CRC at boot, and a corrupted one refuses to operate rather than degrading.

**Why not first-boot auto-bonding.** It requires the least tooling and it opens a window during which pairing is possible. A set powered up on a bench beside another set is the failure FS §2.3 names, and it would happen once, at manufacture, to a set that then works perfectly on every bench test and is wrong in a hall.

### 10.4 What this does and does not defend against

Stated plainly, because a security section that only lists mechanisms invites the reader to assume more than was built.

**Defended:** accidental cross-set association at density, which is the requirement. A remote cannot associate with a neighbouring dongle, cannot be commanded by one, and cannot deliver a press to one. Passive eavesdropping on match traffic. Casual injection by anything that does not hold the set key.

**Not defended:** a determined attacker with physical access to a unit and the means to read its flash. `set_key` is stored in plain flash; APPROTECT should be enabled on production units, and that is a manufacturing step, not a protocol one. Also undefended: jamming, which no protocol at this layer can defend against and which FS §7.4 answers by requiring that degradation be *visible* rather than prevented.

The threat model is a crowded hall, not an adversary. It is worth being explicit that this was the model chosen, so that a future requirement to widen it is recognised as a change rather than assumed to be already met.

---

## 11. Density and coexistence

`SCOPE.md` §4: up to 30 independent systems in parallel, in venue Wi-Fi, among hundreds of spectator phones, with no degradation of the latency budget attributable to neighbours.

| Mechanism | Setting | Why |
|---|---|---|
| PHY | LE 2M, both connections | Halves air time per packet, which halves the collision cross-section. Also a precondition for the SCI intervals of §12.2, should they ever be adopted — but it earns its place on the density argument alone |
| Frequency hopping | Adaptive, standard | The mechanism the density requirement actually rests on. 37 data channels, and the central owns the map |
| Channel map | Adapted from QoS Conn Event Reports (`CONFIG_BT_CTLR_SDC_QOS_CONN_EVENT_REPORT`) | Persistently failing channels are removed. In a hall this will mostly mean the channels under Wi-Fi 1, 6 and 11 |
| Map update cadence | No more than once per 30 s | A map that chases short-term interference chases noise, and each update is a link-layer procedure |
| Minimum map size | 8 channels | Below that, hopping stops being the defence it is here. Falling to the floor is a reportable condition, not a silent adaptation |
| Connection intervals | **Identical on both connections** | Central scheduling requires a common factor between connection intervals or events collide and packets are dropped. Equal intervals satisfy it trivially |
| Event length | Sized for one packet pair | One 27-byte pair per event is all this protocol needs (§12) |
| TX power | **+4 dBm default**, +8 dBm as a documented escalation | See below |

**On transmit power.** The nRF52840 will do +8 dBm and the temptation is to start there. Thirty systems all transmitting at +8 dBm raise each other's noise floor, so the setting that improves one set's link budget degrades the aggregate — and the aggregate is the requirement. The escalation order for a link budget that does not close is the one `PLAN.md` §5.3 already gives: **a USB extension cable to raise and separate the dongle, then a dongle placement constraint in the deployment documentation, then transmit power.** Raising power first will appear to work on a bench with one set and is the hardest of the three to walk back once shipped.

**The dongle's location is part of the link budget.** It sits in a USB port on a laptop at the scoreboard table — close to the host's own 2.4 GHz radios, often below table height, frequently with bodies between it and the mat. Every measurement in §12 must be taken at the dongle **as deployed**, not on a bench with line of sight.

---

## 12. The latency budget

WP §11 allocates 120 ms across six hops and gives this link **25 ms in each direction, inclusive of retransmission**, at 12 m with body shadowing.

### 12.1 Where the 25 ms goes

For a press, at connection interval *I*:

| Component | Cost | Note |
|---|---|---|
| Wait for the next connection event | 0 to *I*, worst case *I* | The dominant term, and the only one the interval controls |
| Air time, one 27-byte pair on 2M PHY | ~0.7 ms | Two packets plus two inter-frame spaces |
| Each retransmission | *I* | A failed attempt costs a whole interval |
| Dongle receive path to USB transmit | ~1 ms | Parse, translate, `seq` assignment, ring |

So one-way ≈ *I* + 1.7 ms with no retransmission, and each retry adds *I*:

| Interval *I* | One-way, no retry | Retries affordable inside 25 ms |
|---|---|---|
| 1.25 ms | ~3.0 ms | ~17 |
| **2.5 ms** | **~4.2 ms** | **~8** |
| 5 ms | ~6.7 ms | ~3 |
| 7.5 ms | ~9.2 ms | ~2 |
| 10 ms | ~11.7 ms | ~1 |

**This table is the entire argument for SCI, and it is an argument about retransmission headroom rather than about latency.** At every rung the no-retry figure is comfortably inside 25 ms; what changes is how many consecutive failed connection events the budget can absorb. At 10 ms — the interval Nordic's own multi-connection guidance lands on — a single retransmission consumes the budget and a second breaks it, so one bad connection event during a body-shadowed moment costs the referee a tap. At 2.5 ms there is room for a fade to persist across eight consecutive events before the press is lost.

The headroom that matters is therefore **the retransmission rate at 12 m through a torso**, which is the one quantity in this section that cannot be derived and has never been measured. §12.2 resolves the ladder against that gap.

### 12.2 The connection-interval ladder

The target interval is a **firmware constant**, negotiated once during connection setup and never changed while a match is running. A link that renegotiates mid-match changes the latency the referee is relying on, without telling them.

| Rung | Interval | Mechanism | Status |
|---|---|---|---|
| 0 | 1.25 ms | SCI, RCV minimum | Stretch. Two connections at 1.25 ms is likely not schedulable — see below |
| 1 | 2.5 ms | SCI | Deferred contingency |
| 2 | 5 ms | SCI | Deferred contingency |
| **3** | **7.5 ms** | **Baseline BLE, no SCI** | **Baseline. What this revision specifies and what the firmware implements** |
| 4 | 10 ms | Baseline BLE | Nordic's multi-connection figure. The floor |

**Rung 3 is the baseline, and SCI is deferred.** An earlier revision named rung 1 the target and treated the rest of the ladder as fallback. That inverted the burden of proof: SCI is a controller feature with a four-call enable sequence, a subrating prerequisite this design otherwise refuses, and a failure mode that is a rejected HCI command at runtime rather than a build error — paid for against a retransmission rate nobody has measured. Rung 3 needs none of it, clears the 25 ms allocation with roughly two retransmissions of headroom, and is plain Bluetooth LE that any tooling can observe.

Nothing in `SCOPE.md` or `SYSTEM_FUNC_SPEC.md` specifies a connection interval. What they specify is the ~120 ms acknowledgement of FS §5.3, and rung 3 meets it.

**SCI is revisited only if body shadowing proves a real problem on shipping hardware**, measured at the dongle as deployed, with p99 rather than median, on the module and enclosure the product actually carries. Adopting it then is a deliberate change with a number behind it; adopting it now would be a prediction with complexity attached. Rungs 1 and 2 remain specified so that change has somewhere to land, and §12.3 is kept accurate against it.

Descend to rung 4 only on a **measured** scheduling failure — dropped connection events, disconnections, or event-length overruns — never on suspicion. The interval in use is reported in a `LOG` line at connection setup so that a latency measurement can always be attributed to a known interval.

**Why rung 0 is a stretch.** A connection event carrying one 27-byte packet pair on 2M PHY occupies roughly 0.65 ms once ramp-up and two inter-frame spaces are counted. Central scheduling requires that one timing-event of *every* active link fits inside the common interval, so two connections need roughly 1.3 ms — which does not fit inside 1.25 ms. It fits comfortably inside 2.5 ms, and trivially inside 7.5 ms.

**These are estimates from documentation and arithmetic, and they have never been measured on this hardware.** They are here so that the measurement has a prediction to falsify, which is the only way a measurement of this kind means anything. That they are unmeasured is precisely why the baseline is the rung that needs no special controller feature to reach.

**One consequence to hold, because it runs backwards into hardware.** The interval is an input to the radio power budget and through it to battery sizing (`PLAN.md` §4.8, R6). A board sized against rung 3 and later moved to rung 1 sees roughly three times the connection events per second, so a battery sized exactly to rung 3 would need a respin. Size with headroom, and keep the interval a single named constant in one place, so the option survives without any SCI code existing.

### 12.3 SCI configuration — for the deferred contingency only

**Not implemented in this revision.** Rung 3 requires none of what follows. This section is retained, and corrected, so that a future adoption of SCI starts from something true rather than from a sequence that fails at runtime.

The features must be enabled in the right order, and the dependency is easy to miss because the failure is a rejected HCI command rather than a build error. Verified against `nrfxlib/softdevice_controller/include/sdc.h` in the pinned NCS v3.4.0:

```c
sdc_support_extended_feature_set_central();      /* prerequisite of frame-space update and of SCI */
sdc_support_connection_subrating_central();      /* prerequisite of SCI, not a feature we use */
sdc_support_frame_space_update_central();        /* prerequisite of lowest_frame_space */
sdc_support_lowest_frame_space();                /* required for the shortest intervals */
sdc_support_shorter_connection_intervals_central();
```

All of these must be called **before** `sdc_cfg_set()` and `sdc_enable()`; `sdc_support_helper()` exists to get that ordering right.

**Two corrections against the previous revision of this section**, both of which would have presented as a rejected HCI command with no build-time signal:

1. `sdc_support_extended_feature_set()` is **deprecated** in v3.4.0 in favour of the role-specific `sdc_support_extended_feature_set_central()` / `_peripheral()`.
2. `sdc_support_lowest_frame_space()` requires `sdc_support_frame_space_update_central()` or `_peripheral()` to have been called, which the previous sequence **omitted entirely**. Frame Space Update in turn requires Extended Feature Set.

Note also that `sdc_support_lowest_frame_space()` forces ACL connections to use the same TX and RX PHY. This protocol uses LE 2M symmetrically (§11), so the constraint costs nothing here — but it is a constraint, not a hint.

**Each end calls only its own role's variants.** The dongle is central-only and the remote peripheral-only, so the dongle calls the `_central` forms and the remote the `_peripheral` forms. The header's requirement to call both is conditional on one device supporting both roles, which neither does.

Connection subrating is a **prerequisite of SCI that this protocol does not otherwise use** — subrating skips connection events, and §9.1 has already ruled that out for the same reason peripheral latency is ruled out. It would be enabled because SCI mandates it, with the subrate factor held at 1.

The minimum connection interval a peer will accept is read with `bt_conn_le_read_min_conn_interval()` rather than assumed. NCS v3.4.0 documents 750 µs as the nRF52 Series floor, which is below every rung on the ladder; the read exists so that a firmware mismatch between dongle and remote produces a clean fallback rather than a rejected parameter update.

### 12.4 Power, and what may not be traded for it

FS §11.2 targets a ten-hour tournament day. The two significant consumers are the haptic motor and the radio, and the motor is expected to dominate.

The radio's cost here is set by the connection interval, and the standard levers for reducing it — peripheral latency and connection subrating — are both **unavailable** (§9.1), because both work by skipping connection events and every skipped event is a delayed acknowledgement tap.

Heartbeat density is not available either, and for a different reason: FS §11.2 is explicit that the beat carries the running/paused distinction, so any reduction in beat density must be accompanied by the LED taking that distinction over — which is the basis of the planned power-saving mode, not a tuning knob.

**What that leaves, in order:** the amplitude separation already required by FS §11.1 (a reduced-amplitude `BEAT` draws meaningfully less than a full `TAP`, and the beat is by far the most repeated haptic event); LED duty cycle and brightness; and the rung of the §12.2 ladder, which trades latency headroom for radio duty cycle directly and visibly.

If the ten-hour target cannot be met with those, the answer is `SCOPE.md` §9.2's planned power-saving mode or a swappable battery — **not** a quiet increase in the connection interval, which would spend the acknowledgement budget to buy battery life without anybody deciding to.

---

## 13. What this link assumes of its peers

Out of scope to specify, in scope to constrain — the mirror of WP §12.

### 13.1 Of the USB link and the scoreboard

| Assumption | Why this link depends on it |
|---|---|
| `ACK` is turned around in the receive path, not deferred to a timer | The `ttl` on a `TAP` is computed from what is left of 120 ms when the `ACK` arrives. A dongle that batched would spend the deadline before transmitting |
| `STATE` is answered unconditionally to every `JOIN` | §7.2 is the only path by which a reconnected or substituted remote gets correct indicators |
| Heartbeat burst suppression stays in the app | §8.2. Nothing below the app knows a scoring burst is in progress |
| The app tolerates a `LINK … CONNECTING` state | §9.4 emits it during the disconnection debounce |
| Presses arriving with no app are dropped, not queued | §6.5. The app must count what it is told was dropped |

### 13.2 Of the remote hardware

| Assumption | Source |
|---|---|
| Gesture classification in firmware at 15 / 600 / 150 ms, independent of link state | FS §4.2 |
| `BEAT` unmistakably weaker than `TAP` on the wrist, through a strap, in motion | FS §11.1. **An open validation item, not an established fact** |
| Motor spin-up to perceptible within 20 ms | WP §11. The hard floor of the acknowledgement budget |
| Four RGB indicators, two of them driven only from `DN_INDICATOR` | FS §3.2 |
| Battery measurement good to ±1 percentage point over the discharge curve | §5.6 drives FS §10.1's four-band indication |
| Flash partition for the provisioning record, and APPROTECT on production units | §10 |

---

## 14. Conformance cases

Both ends must handle these. Each has a failure mode that is silent, which is why each is listed.

| # | Case | Expected |
|---|---|---|
| A1 | `UP_INPUT` with an unknown `button` or `gesture` value | Ignored, counted, one `LOG` line. **No `EVT` emitted** |
| A2 | Frame with an unknown `TYPE` | Ignored silently. Forward compatibility depends on it |
| A3 | Known `TYPE`, wrong length | Ignored, counted, logged |
| A4 | `UP_INPUT` with `CTR` equal to the last accepted | Discarded as a duplicate; counted; **no `EVT`** |
| A5 | `UP_INPUT` with `CTR` skipping three values | Accepted; `radio_gap` += 3; logged. **The event is not withheld** — a real press must not be discarded because an earlier one was lost |
| A6 | `CTR` wrapping 255 → 0 | Accepted as consecutive. Modulo arithmetic, not comparison |
| A7 | Remote reboots mid-match; `UP_READY` carries a reset `ctr_base` | Dongle re-baselines. **No gap logged.** `JOIN` emitted; `STATE` answered; indicators correct before the clock restarts |
| A8 | `ACK` arriving after its 120 ms budget is spent | **No `DN_HAPTIC` sent at all.** `taps_dropped_late` incremented |
| A9 | Two `ACK`s for the same remote inside one connection interval | The first is replaced, not queued behind (§8.3) |
| A10 | `DN_HAPTIC BEAT` arriving while a `TAP` is rendering | The `TAP` is not interrupted or truncated; the `BEAT` is dropped |
| A11 | `DN_INDICATOR` identical to the last one received | Applied; nothing re-triggers; no visible change. Idempotence |
| A12 | Connection from a device with a valid address but no set key | Encryption fails; disconnected; counted. **No GATT access at any point** |
| A13 | `RR_IDENTITY` reporting `set_serial` other than the dongle's | Disconnected; `ERR SET_MISMATCH` |
| A14 | `RR_IDENTITY` reporting `radio_proto_major` ≠ 1 | Disconnected; `ERR REMOTE_PROTO_MISMATCH`; remote reported `DISCONNECTED` |
| A15 | App supervision expires while both remotes are connected | Both remotes receive `DN_HOST DOWN`; both render `LED_LINK` off and the repeating double buzz, **while the radio connections stay up** |
| A16 | Remote powered on with no dongle present | Renders link-lost from boot. Does not render "connected" pending contact |
| A17 | Remote leaves and re-enters range within 2 s | App sees `CONNECTING` then `CONNECTED`. **No `DISCONNECTED` line** (§9.4) |
| A18 | Press made while out of range | No `EVT`, no tap. The gap appears in `radio_gap` on reconnection |
| A19 | Unprovisioned unit powered on | Does not advertise, does not initiate; fault indication; `ERR NO_PROVISIONING` if a dongle |
| A20 | Inert button pressed (`ACK … SILENT`) | **No downlink frame at all.** No motor, no LED, no air time |

A4, A5 and A7 are the ones that pay for `CTR`. A8, A9 and A10 are the ones that pay for R6. A15 is the one that pays for `DN_HOST`, and it is the case most likely to be missed, because everything about the radio looks healthy while it happens.

---

## 15. Alternatives considered and rejected

Recorded so that settled questions are not reopened without cause, and so that the reasons travel with the decision.

### 15.1 Enhanced ShockBurst / Gazell

Both ends are Nordic silicon, so a proprietary link is genuinely available and was taken seriously rather than dismissed.

**What ESB gets right**, and it is not a small list: a star topology with one Primary Receiver and up to eight Primary Transmitters, which is our shape exactly; packet acknowledgement and automatic retransmission in hardware with configurable count and delay; and — critically — the PRX discards repeated packets, which is link-layer exactly-once, the guarantee §6 is built on. Latency is excellent, because there is no connection interval to wait for: a press goes out when it happens.

**Why it was rejected**, on three counts, and the third is decisive:

- **No channel hopping.** A fixed channel in a hall with venue Wi-Fi, hundreds of spectator phones and 29 other systems is exactly the environment `SCOPE.md` §4 requires the product to survive. Frequency agility would have to be built — which is what Gazell is, and adopting Gazell brings its own constraints and its own scheduling model.
- **No security of any kind.** ESB traffic is plaintext and unauthenticated. FS §2.3 requires cryptographic enforcement, and that layer would be ours to write, over a link where getting it wrong corrupts two matches at once and need not be obvious to either referee. The nRF52840 has CryptoCell-310 and hardware AES-CCM, so it is feasible — but it would be our code holding scoring integrity, and §10 currently achieves the same property with a provisioned key and a stock host extension.
- **The downlink is backwards for this product.** In ESB, PRX→PTX data rides only as a payload attached to an acknowledgement, and acknowledgement payloads must be **preloaded** — a transmitter cannot send a command and get a direct response. Our downlink is not a response channel: it is a 1 Hz heartbeat, plus per-press acknowledgement taps, plus expiry buzzes and indicator assertions, all originated asynchronously by the scoreboard. Delivering that over ESB means the remotes poll continuously — spending the battery budget on the uplink in order to service a downlink, and adding a polling interval to every acknowledgement tap.

**ESB remains the documented fallback** if measured latency at density fails against the §12.2 ladder in full. If it is adopted, the channel-hopping and security layers are scoped as first-class work, not as details.

### 15.2 LLPM instead of SCI

Nordic's proprietary Low Latency Packet Mode gives a 1 ms connection interval on 2M PHY and is proven in nRF Desktop. It was rejected as the *primary* mechanism, though it remains available:

- One packet pair per connection event, 2M only, no standards path, and no interoperability story if any part of this system ever has to talk to something that is not ours.
- Nordic's own multi-connection guidance moves an LLPM central with more than one connection to a 10 ms interval to avoid Link Layer scheduling conflicts that show up as dropped report rates and disconnections — and two remotes is that case.
- SCI is a Core 6.2 feature, present in the pinned SDK for this silicon, with a documented 750 µs floor on nRF52 and a defined negotiation the peer can decline gracefully. The ladder of §12.2 degrades; LLPM either works or does not.

**Both are now deferred**, since §12.2 makes rung 3 the baseline and neither mechanism is needed to reach it. The comparison stands for the day a measurement reopens the question: if sub-millisecond intervals are ever wanted, SCI is still the one to try first, and for the reasons above rather than because it was chosen once.

### 15.3 A larger MTU, or Data Length Extension

Rejected, and §4.3 gives the reasoning in full: the 27-byte Link Layer payload is a precondition for the shortest connection intervals, so raising the MTU spends the latency budget to buy throughput this protocol does not need. The largest frame here is 10 bytes.

At the rung 3 baseline that precondition is not binding, so the rejection rests on the simpler half of the argument: **there is nothing to carry.** A larger MTU would lengthen air time per packet — which §11 counts against the density requirement — in exchange for capacity no frame in §5 uses. It would also quietly foreclose the §12.2 contingency.

### 15.4 Application-level retry of presses

Rejected. §6.3: link-layer retransmission inside the connection event is the right amount of effort, and any retry above it would fire only when the press was already worthless. "No tap means press again" is a rule the referee can hold; a retry that succeeds at 300 ms produces a tap the referee has stopped expecting and will attribute to their next press.

### 15.5 Holding secondary-clock ownership on the remote or the dongle

Rejected by FS §6.2 by name, and restated here because this is the layer where it would be tempting: holding ownership on the remote would let the beat run without a per-second downlink frame, saving air time and battery on the highest-volume message on the link.

It would also let a remote beat on stale state — reporting one athlete on the referee's wrist while the scoreboard reports another, quietly, with no self-correcting mechanism. The per-second frame is the price of never diverging, and it buys continuous end-to-end liveness proof during exactly the periods when the referee depends on the system most.

### 15.6 HID over GATT, or the Nordic UART Service

**HID over GATT** would give a well-trodden low-latency profile and Nordic's own multi-link tuning for free. It was rejected because the downlink does not fit: HID output reports are a poor carrier for asynchronous per-beat haptic commands and full indicator assertions, and the profile's vocabulary would have to be bent into shapes it does not have. There is also no host to be compatible with — the dongle is ours, so the interoperability that justifies HID buys nothing.

**NUS** would let the ASCII wire protocol run verbatim over the air, making the dongle a near-transparent relay and giving one protocol instead of two. It was rejected on §4.3: an `EVT ADD_POINT PRESS RED 17` line is 28 bytes where `UP_INPUT` is 4, which exceeds the 20-byte value budget that the shortest connection interval depends on. It would also erase the two things this layer must do that the wire layer does not — deadline enforcement (R6) and gap-visible loss detection (§6) — by giving them nowhere to live.

---

## 16. Versioning

`radio_proto_major.minor` is carried in `RR_IDENTITY` (§3.2) and is **independent of the wire protocol's version.** The two links are versioned separately because they change for different reasons: the wire protocol changes when the scoreboard's needs change, and this one changes when the radio's behaviour does. A dongle mediates between them and is the only node that must know both.

| Change | Bump |
|---|---|
| New frame `TYPE`; new optional trailing field within the 20-byte budget | Minor. Unknown-type tolerance (§4.1) makes it non-breaking |
| Changed meaning, length or field order of an existing frame; changed enum value | Major |
| Changed connection-interval rung, channel-map policy, debounce timing | Neither. These are tuning constants, reported in `LOG`, and must not silently redefine a version |

A major mismatch is a **refusal to operate**, not a degraded mode (§3.2). A set with one remote on old firmware is a set that will behave correctly on six of seven buttons and wrongly on the seventh, which is worse than a set with a visibly dead remote.

### 16.1 Version history

| Version | Change |
|---|---|
| 1.0 | Initial. Bluetooth LE, dongle central with two peripheral remotes, SCI with a fallback ladder, provisioned set key with no pairing procedure, device-local counter for gap visibility, per-frame deadlines on the downlink |

**Amended 2026-08-10, and deliberately *not* a version bump.** §12.2 now makes rung 3 (7.5 ms, baseline BLE) the specified interval and defers SCI to a contingency; §12.3 is corrected against the v3.4.0 headers. By this section's own rule a changed connection-interval rung is "neither" a major nor a minor change, and the rule is right: nothing on the air moves. The frame set, the counters, the deadline mechanisms and `RR_IDENTITY` are byte-for-byte what v1.0 specified, so a v1.0 peer and an amended peer interoperate exactly as before. **A version number describes the contract, not the tuning**, and bumping it here would train the mismatch check in §3.2 to fire on changes that cannot break anything.

---

*Governing documents are `SCOPE.md` v1.1 and `SYSTEM_FUNC_SPEC.md` v2.1. The wire protocol counterpart is `PROTOCOL.md` v3.0. Status, milestones, the validation ladder and the results log are in `PLAN.md`; every number in §12 is a prediction awaiting measurement, and `PLAN.md` §7 is where the measurements land.*
