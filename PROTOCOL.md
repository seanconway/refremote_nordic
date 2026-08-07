# Grappling Scoreboard — USB Wire Protocol

**Version:** 2.0
**Link:** USB CDC-ACM (virtual COM port), nRF52840 dongle ↔ browser via Web Serial
**Scope:** The wired link between the USB bridge dongle and the scoreboard web app only. The BLE link between dongle and wrist remotes is a separate protocol, out of scope.

**Supersedes v1.0.** v1.0 specified binary framing with CRC8, ACK/retry, and per-frame deduplication. That machinery duplicated guarantees USB bulk transfer already provides — CRC16, hardware retransmission, in-order delivery — and defended against failure modes that do not occur at this data rate over 3 cm of shielded cable. v2.0 removes it. See §9 for what was dropped and why.

---

## 1. Design principles

**R1 — Do not re-implement USB.**
CDC-ACM rides on USB bulk endpoints. Error detection and retransmission happen below the application. What USB does *not* give you is message boundaries in a byte stream, so framing is the one transport concern that remains yours.

**R2 — Periodic behaviour lives on the device that needs it.**
The remotes need to know only whether the clock is running. So the app sends *state changes* (`CLOCK RUN` / `CLOCK STOP`) and the dongle generates the 1 Hz heartbeat locally. There is no per-second packet, so there is no per-second packet to drop. This also immunises the heartbeat against browser timer throttling in backgrounded tabs.

**R3 — Reliability is confirmed to the user, not to the transport.**
A referee 30 ft from the scoreboard cannot see it mid-scramble. The wrist is the display. Score events are therefore confirmed end-to-end at the *product* level: the app confirms, the dongle relays a distinct haptic, and that buzz means "the laptop has your point." This verifies the whole chain — remote, BLE, dongle, USB, app — in a way a transport-layer ACK cannot.

**R4 — Human-readable on the wire.**
`screen /dev/ttyACM0 115200` shows every event in plain text, and you can type commands by hand to exercise firmware with no browser, no script, no harness.

---

## 2. Framing

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

- **Unknown keyword → ignore silently.** This is what lets v2.1 add messages without breaking v2.0 peers.
- **Wrong argument count or unparsable argument → ignore silently, log locally.**
- **Buffer exceeds 120 bytes without a terminator → discard the buffer and resynchronise at the next `\n`.**

**Chunk boundaries are the one real hazard on this link.** `reader.read()` returns arbitrary chunks: a line may span several reads, and one read may contain several lines plus a partial. Feed bytes into the line assembler; never assume one read equals one line. This is the failure that will actually bite you, and it is why framing survived the simplification when everything else did not.

---

## 3. Messages: Dongle → App

| Message | Args | Meaning |
|---|---|---|
| `HELLO` | `<proto> <fw> <caps>` | Identity. Sent on boot and in reply to `INFO`. |
| `EVT` | `<action> <src> <seq>` | Referee button press. See §3.1. |
| `LINK` | `<remote> <state> [rssi] [batt]` | BLE link status for one remote. See §3.2. |
| `PONG` | — | Reply to `PING`. |
| `ECHO` | `<text>` | Reply to `ECHO`, identical text. |
| `LOG` | `<text>` | Freeform diagnostic. App may display or ignore. Never semantic. |
| `ERR` | `<text>` | Dongle-side error, e.g. `ERR BLE_INIT_FAILED`. |

### 3.1 `EVT` — officiating events

```
EVT ADD_POINT RED 17
EVT TOGGLE_TIMER GREEN 18
EVT PERIOD_UP RED 19
```

**`<action>`** — seven values covering the ten original requirements:

| action | Covers |
|---|---|
| `TOGGLE_TIMER` | `red_toggle_timer`, `green_toggle_timer` |
| `ADD_POINT` | `red_add_point`, `green_add_point` |
| `REMOVE_POINT` | `red_remove_point`, `green_remove_point` |
| `TIME_UP` | `increment_time` |
| `TIME_DOWN` | `decrement_time` |
| `PERIOD_UP` | `increment_period` |
| `PERIOD_DOWN` | `decrement_period` |

**`<src>`** — `RED` or `GREEN`, the originating remote.

For `ADD_POINT` and `REMOVE_POINT`, `src` identifies both the originating remote and the competitor scored — the red remote scores the red competitor. For `TOGGLE_TIMER` and the `TIME_*` / `PERIOD_*` family the action is global; `src` is carried for diagnostics and to route the confirmation haptic back to the correct wrist.

**`<seq>`** — decimal integer, `0`–`999`, incremented per `EVT` sent, wrapping to 0.

The sequence number exists **for loss detection, not retransmission.** The app logs a warning on any gap. In normal operation gaps never occur; if your logs ever show one, that is a real finding about your link, and you would not have seen it without this field. Cost: four bytes and one counter.

### 3.2 `LINK` — remote connectivity

```
LINK RED CONNECTED -52 87
LINK GREEN DISCONNECTED
```

| Arg | Values |
|---|---|
| `<remote>` | `RED` \| `GREEN` |
| `<state>` | `CONNECTED` \| `CONNECTING` \| `DISCONNECTED` |
| `[rssi]` | Signed dBm. Present only when `CONNECTED`. |
| `[batt]` | Battery percent, 0–100. Present only when `CONNECTED` and known. |

Emitted on every state change, and for both remotes in response to `INFO`. While connected, re-emit every 10 s so the scoreboard's signal and battery indicators stay fresh without polling.

---

## 4. Messages: App → Dongle

| Message | Args | Meaning |
|---|---|---|
| `CLOCK` | `RUN` \| `STOP` | Match clock state. Drives the heartbeat. See §5. |
| `EXPIRE` | — | Period time reached zero. Fires the expiration haptic. |
| `CONFIRM` | `<seq>` | The app has applied the `EVT` with this sequence number. See §6. |
| `PING` | — | Liveness. Dongle replies `PONG`. |
| `INFO` | — | Dongle replies `HELLO` plus a `LINK` line per remote. |
| `ECHO` | `<text>` | Dongle replies `ECHO <text>`. |
| `TEST` | `<mode>` | Self-test. See §7.2. |

`EXPIRE` does not itself stop the clock. The app sends `CLOCK STOP` as well, in whichever order suits it. The dongle treats them independently.

---

## 5. Clock state and the heartbeat

The dongle holds one clock state, defaulting to `STOPPED` at boot.

| State | Entered by | Heartbeat |
|---|---|---|
| `STOPPED` | boot, `CLOCK STOP`, or supervision timeout | off |
| `RUNNING` | `CLOCK RUN` | 1 Hz pulse to both connected remotes |

While `RUNNING`, the dongle runs a local 1 Hz timer and sends a short haptic pulse to each connected remote on every tick. The tick is generated on the dongle. The app is not involved after the initial `CLOCK RUN`.

Repeated identical commands are idempotent: `CLOCK RUN` while already running does not restart or double the timer phase.

### 5.1 Link supervision — the fail-safe

Because the heartbeat is now dongle-local, it would otherwise keep running if the app died. So:

- The app sends `PING` every **2 s** whenever it is otherwise idle.
- The dongle tracks the time since **any** line was received from the app.
- If that exceeds **5 s**, the dongle enters `STOPPED`, stops the heartbeat, and emits `ERR APP_TIMEOUT`.
- Recovery is automatic: the next `CLOCK RUN` resumes normal operation.

This single mechanism covers browser crash, tab close, laptop sleep, and cable pull — none of which a transport-level ACK layer would have caught. It is the most important reliability feature in this document.

The app applies the mirror-image rule: if no line arrives from the dongle for **5 s**, mark the link stale and show it in the UI.

### 5.2 Haptic patterns

Three patterns must be distinguishable by feel alone, through a wrist strap, by someone concentrating on a match:

| Event | Suggested pattern |
|---|---|
| Heartbeat (1 Hz) | Single short pulse, ~30 ms |
| Confirmation (§6) | Double pulse, ~40 ms on / 60 ms off / 40 ms |
| Expiration | Long pulse, ~500 ms |

Tune on real hardware with a real strap. These are starting points, not requirements.

---

## 6. End-to-end confirmation

This replaces v1.0's transport ACK, and does a different and more useful job.

```
Remote → Dongle:  (BLE button event)
Dongle → App:     EVT ADD_POINT RED 17
App:              applies the point, updates the display
App → Dongle:     CONFIRM 17
Dongle → Remote:  (BLE confirmation haptic, RED only)
```

The confirmation haptic fires on the **originating remote only**, routed by the `src` recorded against that sequence number.

The dongle keeps a small pending table (8 entries is ample) mapping recent `seq` values to their originating remote. On `CONFIRM`, look up and fire; on an unknown or already-confirmed `seq`, ignore silently.

**If no `CONFIRM` arrives within 500 ms**, the dongle drops the entry and does nothing. There is deliberately no failure haptic: a buzz that means "something went wrong" is worse than silence, because the referee's rule is simple and absolute — *no buzz means the point did not land, press again.* Absence of confirmation is itself the signal.

Only `ADD_POINT` and `REMOVE_POINT` need confirmation. Confirming `TOGGLE_TIMER` is redundant — the heartbeat starting or stopping is already the feedback, and it is unambiguous. The `TIME_*` and `PERIOD_*` corrections are rare, deliberate, and made while looking at the scoreboard; confirm them if bench testing shows referees want it, but do not assume.

---

## 7. Debug and test

### 7.1 Manual operation

Every command is typeable. With a terminal open on the port:

```
INFO
CLOCK RUN
CLOCK STOP
EXPIRE
ECHO hello
```

This exercises the entire firmware with no browser, no scoreboard, and no test harness — and is the fastest way to isolate whether a bug is in firmware or in the web app.

**Caveat:** the supervision timeout in §5.1 will fire after 5 s of manual idleness and stop the heartbeat. That is correct behaviour. Type `PING` to hold it open, or use `TEST 3` to suspend supervision for bench work.

### 7.2 `TEST <mode>`

| mode | Behaviour |
|---|---|
| `0` | Stop any running test mode |
| `1` | Emit each of the seven `EVT` actions once, alternating `RED`/`GREEN`, 500 ms apart |
| `2` | Emit random `EVT` lines continuously at ~5 Hz until stopped |
| `3` | Suspend link supervision until reboot or `TEST 0` (bench use only) |

Mode `1` gives the scoreboard a deterministic, repeatable stimulus with no remotes, no BLE, and no button presses. Mode `2` is your soak-test driver — leave it running for hours and check for gaps in `seq` and for memory or timer drift.

### 7.3 Logging

Log every line, both directions, with a direction tag and timestamp:

```
12:04:31.882  TX  EVT ADD_POINT RED 17
12:04:31.907  RX  CONFIRM 17
```

Keep a raw-line panel in the scoreboard UI behind a debug toggle. It survives past bring-up and earns its keep at the first pilot.

---

## 8. Connection lifecycle

**On app connect** (after Web Serial `open()`):

1. App sends `INFO`.
2. Dongle replies `HELLO 2.0 0.1.0 0`, then a `LINK` line per remote.
3. App sends `CLOCK STOP` to force a known state regardless of what the dongle was doing.
4. App begins the 2 s `PING` cadence.

**On `HELLO`,** the app compares the protocol version. Differing minor version → warn and continue. Differing major version → refuse to operate and tell the user to update the dongle firmware.

**On disconnect** (port closed, device unplugged, `readable` stream ends):

1. App shows a disconnected state and stops the `PING` cadence.
2. Dongle's supervision timeout fires within 5 s, stopping the heartbeat.
3. App offers reconnect. `navigator.serial.getPorts()` returns previously authorised ports, so reconnection to a known dongle needs no user gesture; only a first-time grant does.

**Do not attempt to resume state across a reconnect.** Re-run the sequence above from scratch. The match clock lives in the app; the dongle holds nothing worth preserving.

### 8.1 Transport settings

| Setting | Value |
|---|---|
| Baud | 115200 (nominal; ignored by CDC-ACM, but set explicitly on both sides) |
| Data / parity / stop | 8 / none / 1 |
| Flow control | None |

---

## 9. What was removed from v1.0, and why

| Removed | Reason |
|---|---|
| CRC8 | USB bulk transfer already does CRC16 with hardware retransmission. |
| ACK / retry | Defends against packet loss that does not occur at ~10 events/sec over USB. The genuine loss case — disconnection — is not recoverable by retransmission and is handled by §5.1 and §8 instead. |
| Deduplication | Only existed because retry could produce duplicates. |
| Byte stuffing / escaping | Unnecessary once the format is newline-delimited text. |
| Binary `TIMER_STATE` at 1 Hz | Replaced by dongle-local heartbeat (§5). Removes the packet entirely rather than making it reliable. |
| Golden test vectors | Text lines are self-evidently correct by inspection. Test the parsers against the negative cases in §10 instead. |

Retained: framing (chunk boundaries are real), sequence numbers (repurposed for loss detection), link supervision (strengthened), version handshake.

**When to reconsider.** If you later put the match clock on the remote displays, add telemetry at real data rates, or move to a transport without built-in error detection — a raw UART, RS-485, or a direct radio link — the v1.0 design becomes appropriate again. The `HELLO` version field is what lets you swap protocols without breaking deployed hardware.

---

## 10. Parser test cases

Both parsers must handle these without crashing, and must correctly parse the next valid line immediately after:

| # | Input | Expected |
|---|---|---|
| T1 | `EVT ADD_POINT RED 17\n` | Parsed |
| T2 | `EVT ADD_POINT RED 17\r\n` | Parsed, `\r` stripped |
| T3 | T1 delivered one byte per `read()` | Parsed identically |
| T4 | Three lines in a single `read()` | All three parsed, in order |
| T5 | `EVT ADD_POI` then `NT RED 17\n` in two reads | Parsed |
| T6 | `BOGUS FOO BAR\n` | Ignored, no crash |
| T7 | `EVT ADD_POINT\n` (missing args) | Ignored, logged |
| T8 | `EVT ADD_POINT RED xyz\n` (bad seq) | Ignored, logged |
| T9 | 200 bytes with no `\n`, then `\n`, then a valid line | Overlong buffer discarded; the line after the terminator parses |
| T9c | An overlong run split across several reads, then `\n`, then a valid line | Identical to T9 — the discard state survives the chunk boundary |
| T10 | `\n\n\nEVT ADD_POINT RED 17\n` | Empty lines ignored, event parsed |

T3, T4, and T5 are the ones that matter. Write them first.

**Resynchronisation happens at the next `\n`, which may be several reads away.
A chunk boundary is not a resync point** — the split between reads is an
artifact of the transport and carries no information about the stream. It
follows that a discarded run and a line that shares its terminator are lost
together: after overlong garbage with no intervening `\n`, the next line to
arrive is consumed as the tail of that garbage, and the line after it is the
first to parse. Losing one line is the correct price of resynchronising on a
known-good boundary. The alternative — treating a read boundary as a resync
point — lets the tail of a garbage run be emitted as a message, and a tail
that happens to begin at a keyword boundary would parse as a genuine event.

---

## 11. Implementation notes

**Keep parsers dependency-free.** `protocol.c` with no Zephyr dependencies — no `k_*`, no `LOG_*`, no devicetree — so the same file compiles under host `gcc` for unit tests. `protocol.js` with no DOM and no Web Serial imports, so it tests under Node with no browser and no hardware. Both are small enough that this costs nothing and makes §10 trivial to run.

**Firmware line assembly:** a fixed 128-byte buffer and an index. No dynamic allocation, no `printf`-style parsing. `strtok_r` or manual space-splitting is fine.

**Reader loop structure (JS):** acquire and release the reader lock per read loop, not for the lifetime of the app, or you cannot cleanly close and reopen the port on reconnect.

**Clock accuracy:** derive the match clock from `performance.now()` deltas rather than counting `setInterval` fires. Chrome throttles timers in backgrounded tabs, and delta-based timing turns silent drift into a visible, correctable jump.

---

## 12. Version history

| Version | Change |
|---|---|
| 1.0 | Binary framing, CRC8, ACK/retry, dedup, 1 Hz `TIMER_STATE`. Superseded. |
| 2.0 | Newline-delimited ASCII. Dongle-local heartbeat driven by clock state. Transport ACK replaced by end-to-end `CONFIRM`. Link supervision as primary fail-safe. |

Additive changes (new keywords, new optional arguments) bump the minor version; unknown-keyword tolerance in §2.2 makes them non-breaking. Changing the meaning or argument order of an existing message bumps the major version.