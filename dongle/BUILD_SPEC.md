# Dongle firmware — build specification

**What this document is.** The implementable contract for the dongle firmware: module boundaries, state, algorithms, and acceptance. It is written so that the firmware can be built from it without re-deriving decisions from the protocol documents.

**What it is not.** It does not restate the protocols — `PROTOCOL.md` v3.0 is the wire contract and `RADIO_PROTOCOL.md` v1.0 is the radio contract, and where this document and those disagree, **they win**. It also does not cover build, flash or manual-test procedure; that is [`README.md`](README.md).

| For | See |
|---|---|
| What the system is, how it behaves | `SCOPE.md`, `SYSTEM_FUNC_SPEC.md` — authoritative |
| The wire contract | [`PROTOCOL.md`](../PROTOCOL.md) — cited *WP §n* |
| The radio contract | [`RADIO_PROTOCOL.md`](../RADIO_PROTOCOL.md) — cited *RP §n* |
| The remote's half | [`../remote/BUILD_SPEC.md`](../remote/BUILD_SPEC.md) |
| Status, stages, results | [`PLAN.md`](../PLAN.md) |

---

## 1. Scope

One firmware, bridging two protocols: WP v3.0 over USB CDC-ACM to the scoreboard, and RP v1.0 over Bluetooth LE to two remotes. It holds **no match state** — not the clock, not the score, not secondary-clock ownership. Everything it holds is transport state.

The firmware is currently at WP v2.0. That revision is a breaking change and the work is not incremental: the message set changes, the dongle-side clock and heartbeat are deleted outright, and the radio layer does not exist at all.

### 1.1 The one structural decision behind this document

The radio sits behind **one interface, `radio.h`, with two implementations selected at build time.** `radio_null` is not scaffolding to be deleted — it is a permanent build configuration in which the entire wire layer runs, exercised by the `TEST` modes, with no radio anywhere in the system.

That configuration is what makes a radio regression attributable. `PLAN.md` §5.4 lists eight ways adding a radio degrades a working USB link without touching any USB code, and every one of them is diagnosed by asking *does it still happen with `CONFIG_DONGLE_RADIO=n`?* A baseline you can re-run in thirty seconds answers that; a baseline that was a milestone six weeks ago does not.

---

## 2. Module map

| File | Owns | Zephyr-free |
|---|---|---|
| `src/protocol.c/.h` | WP v3.0 line assembler, parser, encoders | **Yes — enforced** |
| `../common/rframe.c/.h` | RP v1.0 frame codec, both directions, `CTR` arithmetic | **Yes — enforced** |
| `../common/provisioning.c/.h` | Record layout, `provisioning_validate()` | **Yes — enforced** |
| `src/engine.c/.h` | All protocol state; the sole producer on the transmit path |  No |
| `src/radio.h` | The seam. No implementation | — |
| `src/radio_null.c` | The no-radio implementation: LEDs, everything `DISCONNECTED` | No |
| `src/radio_ble.c` | BLE central, GATT client, link state, deadline enforcement | No |
| `src/usb_link.c/.h` | CDC-ACM, ring buffers, **and a transmit drop counter** | No |
| `src/indicator.c/.h` | LED stand-in, used by `radio_null` and by fault indication. **One fitted lamp** — [`BOARD.md`](BOARD.md) §2 | No |

### 2.1 The Zephyr-free rule, and why it is worth the discipline

`protocol.c`, `rframe.c` and `provisioning.c` must include nothing from `zephyr/`, use no `k_*` call, no `CONFIG_*`, no devicetree, no `BUILD_ASSERT`. Permitted: `<stdint.h>`, `<stddef.h>`, `<stdbool.h>`, `<string.h>`, `<stdio.h>`.

`protocol.c` already honours this and it is why a host test suite for the wire parser can exist at all. **`rframe.c` must be written the same way from the first line** — retrofitting it later means unpicking Zephyr types from a codec, which nobody does, so the radio simply never gets a host suite. It is free only if decided now.

`provisioning.c` is included because `provisioning_validate()`'s field checks are exactly the logic that must not be wrong, and they are pure — there is no platform-bound half to isolate any more, since the record is a compile-time constant rather than something read from flash (§9, PLAN.md §4.13).

Both host suites live outside the Zephyr build: `tests/protocol/` and `../tests/rframe/`, plain `gcc` and a makefile.

---

## 3. The radio seam

```c
/* radio.h — the engine's entire view of the radio.
 *
 * The engine never sees a bt_conn, a GATT handle, or a frame. It sees two
 * remotes identified the way the wire protocol identifies them, because the
 * moment the engine knows about connections it acquires a second vocabulary
 * for the same thing and they drift.
 */

struct indicator_state {
    uint8_t f1_mode;              /* 0 OFF, 1 SOLID */
    uint8_t f1_rgb[3];
    uint8_t f2_mode;
    uint8_t f2_rgb[3];
};

struct radio_cb {
    /* A press. The engine assigns seq and emits EVT. */
    void (*on_input)(enum proto_remote src, enum proto_button b, enum proto_gesture g);

    /* This remote holds no indicator state — drives JOIN. From UP_READY. */
    void (*on_ready)(enum proto_remote src);

    /* Already debounced per RP §9.4. The engine emits LINK verbatim. */
    void (*on_link)(enum proto_remote src, enum proto_link_state st, int8_t rssi);

    /* From UP_TELEMETRY. Feeds the batt field of LINK. */
    void (*on_telemetry)(enum proto_remote src, uint8_t batt_pct, uint8_t flags);

    /* Freeform diagnostic destined for a LOG line. Never semantic. */
    void (*on_diag)(enum proto_remote src, const char *text);
};

/* The workqueue is the engine's, and every callback above is invoked on it
 * rather than on the Bluetooth RX thread. Passing it in rather than having the
 * implementation reach for engine_workq() keeps radio_ble.c from depending on
 * engine.h; radio.h forward-declares `struct k_work_q` so it stays free of
 * <zephyr/kernel.h>. */
int  radio_init(const struct radio_cb *cb, struct k_work_q *workq);

/* ttl in 4 ms units, 0 = no deadline. Returns 0 if the frame was handed to
 * the controller, negative if it was not sent at all. A negative return is
 * a normal outcome, not an error: see §6.3. */
int  radio_send_haptic   (enum proto_remote, enum proto_waveform, uint8_t ttl_4ms);
int  radio_send_indicator(enum proto_remote, const struct indicator_state *);
int  radio_send_config   (enum proto_remote, uint8_t haptic, uint8_t bright);
int  radio_send_host     (enum proto_remote, bool up);

/* True once the remote is fully established per RP §9.4 — encrypted,
 * identity validated, CCCD subscribed. Not merely "connected". */
bool radio_is_ready(enum proto_remote);
```

### 3.1 `radio_null`

Selected by `CONFIG_DONGLE_RADIO=n`. Reports both remotes `DISCONNECTED` and never transitions. `radio_is_ready()` is always false. The four send functions render on the board's LED channels via `indicator.c` and return 0 — noting that only the `RED` channel's part is fitted on a stock board, so the render is one lamp for two remotes ([`BOARD.md`](BOARD.md) §2).

**It replaces `CONFIG_DONGLE_FAKE_LINK`, which is deleted rather than defaulted off.** The fake reported synthetic `CONNECTED` with a fixed RSSI and battery so the app's indicators could be exercised. That was reasonable when no radio existed and is now a standing hazard: it fabricates exactly the values every link test is trying to measure, and it does so plausibly. `PLAN.md` §5.2 lists it as a standing trap and RP's ladder notes that it invalidates four rungs while producing entirely believable output. A Kconfig default is not protection against that — deletion is.

The null radio tells the truth instead: nothing is connected, because nothing is. The app renders both remotes disconnected, which is correct and which no longer needs a caveat attached to every result.

### 3.2 The trap this seam sets

`dongleModel.js:245-251` refuses to allocate a `seq` for an event whose source remote is not `CONNECTED`. That is correct and it keeps WP §12's guarantee true — *no `EVT` is ever assigned a `seq`* for a press that was lost, so a gap in `seq` always means USB loss and never radio loss.

Under `radio_null` nothing is ever ready, so applied naively that check **silently disables every `TEST` mode** — the entire stimulus the no-radio configuration exists to provide. The rule is therefore split by origin:

| Origin | Connectivity check |
|---|---|
| `on_input()` from the radio | **Applies.** A press from a remote that is not ready cannot happen, and if it does it is dropped and counted |
| `test_handler()` | **Bypassed.** Test events are synthetic stimulus and are not claims about any remote |

Write this as two call sites into a shared `emit_evt()`, not as a flag threaded through one — a boolean parameter named something like `is_test` is the version of this that gets passed wrong once.

---

## 4. Threading and timing

**One cooperative workqueue owns the engine.** Not the system workqueue, which is what v2.0 used.

```
CDC-ACM ISR ──► rx ring ──┐
                          ├──► engine workqueue (cooperative) ──► tx ring ──► CDC-ACM ISR
BLE host RX thread ───────┘
```

The invariant recorded in `engine.h` — exactly one producer feeding the transmit path, therefore no locking — is what keeps the engine simple, and it survives the radio only if radio callbacks marshal onto the same queue rather than calling into the engine from the BT RX thread.

**Initialisation is split in two, because the dependency is circular.** The transport needs the queue before it can accept a byte; the engine needs the transport before it can say anything. `main()` breaks it by interleaving them — `engine_init()` (queue and state, no I/O) → `usb_link_init(engine_on_line, engine_workq())` → `engine_start()` (boot `HELLO`, timers, `radio_init`). The alternative, a queue created at file scope by a `SYS_INIT`, hides the ordering rather than stating it.

**Cooperative priority, and the reason is the acknowledgement budget.** A preemptible queue can be descheduled between receiving an `ACK` and handing the `TAP` to the controller, and that latency is invisible — it shows up as a missing tap under load and nothing else. `PLAN.md` §4.6 predicted a dedicated queue would be needed at radio bring-up; doing it from the start costs a `K_THREAD_STACK_DEFINE` and removes B1 (workqueue contention) from the list of things a latency measurement might mean.

**`ACK` is turned around in the receive path, never deferred to a timer** (RP §13.1). The `ttl` on a `TAP` is computed from what remains of the 120 ms budget at the moment the `ACK` arrives, so any deferral spends the deadline before transmitting.

### 4.1 Timing constants

Every one of these is a named constant in one place. None is a magic number at a call site.

| Constant | Value | Source |
|---|---|---|
| `SUPERVISION_MS` | 2500 | WP §8 — was 5000 at v2.0 |
| `ACK_WINDOW_MS` | 120 | WP §11 — was 500 at v2.0 |
| `LINK_REEMIT_MS` | 10000 | WP §7 |
| `PENDING_SLOTS` | 8 | WP §5.3 |
| `CONN_INTERVAL_UNITS` | 6 (= 7.5 ms) | RP §12.2 rung 3 |
| `LE_SUPERVISION_MS` | 1000 | RP §9.1 |
| `LINK_DEBOUNCE_MS` | 2000 | RP §9.4 |
| `TEST1_INTERVAL_MS` / `TEST2` / `TEST4` | 500 / 200 / 250 | WP §10.2 |

`CONN_INTERVAL_UNITS` is a Kconfig integer rather than a `#define`, because RP §12.2 makes it the one tuning value with a hardware consequence behind it, and it should be visible in a build configuration rather than buried in a header.

---

## 5. Wire layer — v2.0 to v3.0

### 5.1 The change table

| # | Change | Note |
|---|---|---|
| 1 | `PROTO_VERSION` → `"3.0"` | All-or-nothing. Until this lands the app refuses the link at its major-version guard, which is correct behaviour and means there is no partial-migration state to support |
| 2 | `HELLO 3.0 <fw> <set> <caps>` | Four fields. `<set>` comes from the provisioning record; before Stage 2 it is a Kconfig placeholder |
| 3 | `enum proto_action` becomes `enum proto_button`; new `enum proto_gesture` | Buttons are named **by position**, not by function. `TIME_UP`, `TIME_DOWN`, `PERIOD_UP`, `PERIOD_DOWN` are deleted — they encoded officiating meaning on the wire, which FS §7.3 forbids |
| 4 | `EVT <button> <gesture> <src> <seq>` | Exactly four fields. **The three-field v2.0 form must parse as `INVALID`** — see §5.2 |
| 5 | **Delete the clock and the heartbeat** | `clock_running`, `clock_set()`, `heartbeat_work`, `PROTO_CLOCK`, `PROTO_EXPIRE`. A deletion, not a port |
| 6 | `ACK <seq> [SILENT]` replaces `CONFIRM <seq>` | Window 500 → 120 ms. **Every** `EVT` now enters the pending table, not just `ADD_POINT`/`REMOVE_POINT` |
| 7 | `seq` 0–999 → 0–65535 | See §5.3 |
| 8 | `SUPERVISION_MS` 5000 → 2500 | And the handler is re-derived, not edited — see §5.4 |
| 9 | New inbound: `STATE`, `CFG`, `HAP` | §5.5, §5.6 |
| 10 | New outbound: `JOIN <remote>` | From `on_ready()`. Does not exist before the radio; under `radio_null` it is reachable only from a test mode |
| 11 | `TEST 4` | Currently falls into `send_log("unsupported TEST mode")` |
| 12 | Transmit drop counter | §8 |

Framing is **unchanged and must not be touched**: the assembler, the tokenizer, the 120/128-byte limits, the discard-and-resync rule. WP §16 lists it as retained, it is proven on hardware from M0, and its host tests already pass by inspection of the two ends agreeing.

### 5.2 Fail closed on the v2.0 shape

`EVT ADD_POINT RED 17` — three arguments where v3.0 needs four — **must be rejected**, not read as a gestureless press.

This is WP §14 T7 and it is the single most important parser case in the rewrite. A partially-updated dongle, or a v2.0 unit in a mixed fleet, presents exactly this line. Read as a press it produces a score with no gesture and no way to tell it apart from a real one; rejected it produces nothing, and the app's major-version guard has already refused the link anyway.

The parser must therefore check the token count **exactly** (`!= 4` → `INVALID`), never `>= 4`. The existing tokenizer returns the true count even when it exceeds the array, which is what makes an exact check safe against a long line.

### 5.3 `seq`

16-bit, 0–65535, wrapping, post-increment: emit the current value then advance. Not reset on app reconnect — the app handles the apparent discontinuity with a wrap heuristic.

Widened from 0–999 because hold-repeat at 150 ms wraps a 1000-entry space in about 2.5 minutes, which is close enough to a match that a delayed duplicate and a genuine new event could collide on the same number. 65536 wraps in roughly 2.7 hours of *continuous* hold-repeat.

**One subtlety in the range check.** With `seq` as a `uint16_t` the old `seq >= PROTO_SEQ_MODULO` test becomes vacuous, but T11 still requires the literal `65536` on an inbound `ACK` to be **rejected**. Parse into a `uint32_t`, range-check against 65535, then narrow.

### 5.4 Supervision

The v2.0 handler stopped the clock and emitted `ERR APP_TIMEOUT` only `if (clock_running)`, so an idle bench did not spam timeouts. At v3.0 there is no clock, so that gate has nothing to test and the handler must be re-derived rather than edited:

On expiry of `SUPERVISION_MS` since **any** received line:

1. Emit `ERR APP_TIMEOUT`.
2. `radio_send_host(RED, false)` and `radio_send_host(GREEN, false)`.
3. Do not re-arm. The next received line re-arms it and sends `DN_HOST UP`.

Step 2 is the obligation WP §8 states in prose — *instruct both remotes to render link-lost* — and it is the one supervision requirement with no executable reference anywhere, because the emulator models no radio. It is RP §14 A15, and RP §9.2 calls it the case most likely to be missed, because every part of the radio looks healthy while it happens.

The timer is re-armed **before parsing**, on any framed line including malformed ones. WP §8's "any received line" means any line the assembler completed, not any line that parsed — a peer sending garbage is a peer that is alive.

Idle bench work now has only `TEST 3` to keep quiet, which raises the stakes on that standing trap rather than lowering them.

### 5.5 `STATE` — relay, never cache

```
STATE <remote> <f1> <f1rgb> <f2> <f2rgb>
```

Parsed, converted to `struct indicator_state`, passed to `radio_send_indicator()`. **Unconditionally, without comparing against anything previously sent, and the dongle holds no indicator cache** (RP §7.1).

The temptation is obvious — a cache would suppress redundant frames, and the app already suppresses unchanged `STATE` lines at its end. Refuse it. A cache is dongle-held state that can diverge from the scoreboard's, which is the FS §6.2 failure mode by name, and it diverges silently. Re-sending an unchanged 10-byte frame costs one frame; holding a cache costs a class of bug.

`<f1rgb>` requires a **strict six-character hex** validator, case-insensitive on receive. Five characters must invalidate the whole line (T13). `protocol.c` has `parse_uint` and `parse_int` and no hex parser; write one that checks length first.

### 5.6 `CFG` and `HAP` — the `BOTH` target

Both carry `<target>` ∈ {`RED`, `GREEN`, `BOTH`}, which the existing remote-name lookup cannot express — it maps only the two remotes. Add a separate target enum and lookup shared by the two messages, and expand `BOTH` into two frames at the radio, one per remote. Do not invent a broadcast frame; RP §5 has no such thing, and the per-remote `CTR` would have nowhere to live.

`CFG` is applied on receipt and **persisted until reboot**, and re-sent to any remote that connects (RP §7.3). That is transport state, not match state, so holding it does not violate §5.5 — the distinction is that `CFG` has no scoreboard-side counterpart that could diverge, whereas indicator state does.

`HAP <target> <waveform>` maps to `DN_HAPTIC`. An unknown waveform invalidates the line (T15).

### 5.7 `ECHO`, and a known divergence from the emulator

`ECHO <text>` must reply with the text **verbatim**, including runs of spaces.

The emulator reconstructs it as `args.join(' ')`, which collapses runs of spaces and contradicts WP §3's "identical text". The firmware has the raw line and should simply return the remainder after the keyword.

**This will appear as a difference in the wire-log diff against the emulator, and it is not a firmware defect.** Recorded here because that diff is the primary Stage 1 acceptance instrument and its value depends entirely on differences being trustworthy. One known-benign difference that nobody wrote down is how a diff stops being read.

### 5.8 `TEST` modes

| Mode | Behaviour | Reply |
|---|---|---|
| `0` | Stop any running mode; clear `supervision_suspended`; re-arm supervision | `LOG TEST 0: stopped, supervision active` |
| `1` | One `EVT` per button, `PRESS`, alternating RED/GREEN, 500 ms apart — 7 events | none |
| `2` | Random button and gesture, random src, ~5 Hz until stopped. `HOLD_REP` only for `FORWARD`/`BACKWARD` | none |
| `3` | Suspend supervision until `TEST 0` or reboot | `LOG TEST 3: suspended until TEST 0` |
| `4` | Gesture sweep: **16 events per remote, 32 total**, 250 ms apart | `LOG TEST 4: complete, 32 events` |

Starting any mode implicitly stops the previous one. An unsupported mode gets a `LOG` line — there is no other local channel, since the console is disabled and a second CDC-ACM instance is forbidden (§8), and `LOG` is never semantic (WP §3), so noting it there is not the same as answering on the wire.

**`TEST 4`'s count is pinned by a `BUILD_ASSERT` on the sweep table, not by a test.** The table lives in `engine.c`, which is Zephyr-bound and therefore outside the host suite, so the assertion is the only thing standing between the corrected 16 and the 21 that the prose used to say. It fails the build rather than a test run, which is a weaker guarantee than the rest of §14 gets and is recorded as such.

**`TEST 4` is 16 per remote, not 21.** Seven buttons take `PRESS`, seven take `HOLD`, and only `FORWARD` and `BACKWARD` take `HOLD_REP`: 7 + 7 + 2. WP §10.2 said 21 and has been corrected. A 21-event sweep would emit `HOLD_REP` on five buttons that can never repeat in the field, exercising an app path against traffic no remote will ever send.

All test events go through the normal emission path: they consume real `seq` values, populate the pending table, and expire at 120 ms with no `ACK` if the app is absent.

---

## 6. Acknowledgement routing and the deadline rule

This is the mechanism the whole scoring interface rests on, and every part of it fails silently.

### 6.1 The pending table

Eight entries. On emitting an `EVT`:

```c
struct pending {
    bool     used;
    uint16_t seq;
    enum proto_remote src;   /* the wrist the tap must land on */
    int64_t  born;           /* k_uptime_get() at emission */
};
```

On overflow, **evict the oldest by `born`** — no expiry event, no tap. Steady-state capacity is 8.

Entries expire at `born + ACK_WINDOW_MS`. v2.0 expired them lazily, purging as `pending_add` and `pending_take` walked the table, on the argument that nothing observable happens on expiry so a sweep buys nothing. **That argument does not survive the window shrinking to 120 ms.** Lazily, an entry only expires when a later `EVT` or `ACK` touches the table, so on an idle link a stale entry can still match an `ACK` that arrives long after its deadline — and firing a tap the referee cannot account for is the single worst outcome WP §11 identifies. Add the sweep.

### 6.2 On `ACK <seq>`

1. Look up `seq`. **Unknown, duplicate or already-expired → do nothing.** No tap, no `ERR`, no `LOG` at error level. Acting on it would fire a tap the referee cannot attribute.
2. `SILENT` → clear the entry, record the latency, and **send nothing at all** (§6.4).
3. Otherwise compute the remaining budget and send a `TAP` to `entry.src` (§6.3).

**Routed by `src`, never broadcast.** A broadcast tap is indistinguishable from a correctly routed one whenever only one remote is being watched, which is every bench test until a second remote exists. It is wrong in every real match. This is `HISTORY.md` §2.5 item 4 and rung B6.

Expect the app to `ACK` a duplicate `EVT` it dropped — it does so deliberately, because withholding the tap would make the referee press a third time. The dongle already retired that entry on the first `ACK`, so it lands on the do-nothing path and exactly one tap reaches the wrist.

### 6.3 Deadline enforcement — both mandatory mechanisms

**Mechanism 1 — do not enqueue a frame that is already late.**

```
elapsed   = now - entry.born
remaining = ACK_WINDOW_MS - elapsed - MOTOR_SPINUP_MS   /* 20 ms, WP §11 */
if (remaining <= 0) { taps_dropped_late++; LOG; return; }   /* send nothing */
ttl_4ms = clamp(remaining / 4, 1, 255)
```

This needs no controller feature and it catches the case that actually happens — an app stall, a USB stall, a browser that lost the foreground.

**Mechanism 2 — at most one outstanding `TAP` per connection.** If a second `ACK` arrives for the same remote while a `TAP` is still queued in the controller, the queued one is already worthless. **Replace it; do not append.**

Mechanism 3 (LE Flushable ACL Data) is experimental in NCS v3.4.0 and is not implemented. The guarantee must hold without it.

**Exceeding the budget degrades to silence, never to a late tap.** No tap invokes a rule the referee already has — *press again* — and a second press produces a new `seq` and a correct score. A late tap arriving during the next press is read as acknowledgement of *that* press, so the referee stops pressing while a point is still missing. Silence is recoverable; a misattributed tap is not. There is deliberately no failure haptic.

### 6.4 Inert and no-op are different, and the dongle must not flatten them

| | App sends | Dongle does | Wrist feels |
|---|---|---|---|
| **Inert** — ruleset leaves the button unassigned | `ACK <seq> SILENT` | **Nothing on the air at all** | Nothing |
| **No-op** — legitimate press that changes nothing, e.g. `REMOVE_POINT` at the floor | `ACK <seq>` | Ordinary `DN_HAPTIC TAP` | Full tap |

From the dongle's side the difference is exactly *tap or no tap*, which makes it look trivial and makes it easy to collapse into one path. It is not trivial: a rejection signal on an inert button is more confusing than silence (FS §5.6), and a referee who feels nothing after a legitimate press will press again and double the score.

`ACK … SILENT` sending **no frame** is RP §14 A20, and it must be verified by counting transmitted frames, not by watching an LED that was never going to light.

---

## 7. Radio layer

### 7.1 Connection lifecycle

Per remote, in this order. The order is the contract, not a suggestion:

```
initiate to peer_addr[i] only, filtered on the literal address
  → install set_key as LTK via bt_nrf_conn_set_ltk()
  → raise security; no GATT operation before encryption completes
  → read RR_IDENTITY
  → validate radio_proto_major == 1 and set_serial matches ours
  → write the RR_UPLINK CCCD
  → only now report LINK … CONNECTED
  → send the current DN_HOST value
  → expect UP_READY  →  emit JOIN <remote>
```

**No pairing procedure is ever performed**, at manufacture or in the field. Pairing requests are rejected in both directions and the device is not bondable, so there is no bond store to erase, migrate, corrupt, or clear with a firmware update.

Negative outcomes, all of which must be exercised and none of which may be a silent retry loop:

| Case | Result |
|---|---|
| A12 — valid address, no set key | Encryption fails; disconnect; count. No GATT access at any point |
| A13 — `set_serial` mismatch | Disconnect; `ERR SET_MISMATCH` |
| A14 — `radio_proto_major` ≠ 1 | Disconnect; `ERR REMOTE_PROTO_MISMATCH`; report that remote `DISCONNECTED` |
| A19 — unprovisioned dongle | Do not initiate at all; `ERR NO_PROVISIONING` |

### 7.2 Connection parameters

7.5 ms interval both connections, **identical on both** — central scheduling needs a common factor between intervals or events collide. LE 2M PHY. LE supervision timeout 1000 ms.

**Pinned off, every one a standard optimisation that would fail silently:**

| Feature | Why not |
|---|---|
| Peripheral latency | Skips connection events, delaying the acknowledgement tap by up to *N* intervals |
| Connection subrating | Same mechanism, same objection. Not needed at all now that SCI is deferred |
| Data Length Extension | Raises the minimum interval the controller will grant; buys throughput nothing here needs |
| ATT MTU above the default 23 | Same. The largest frame in RP §5 is 10 bytes |
| Indications on the uplink | An ATT confirmation per notification serialises the uplink and adds a round trip inside the 25 ms allocation |
| Write-with-response on the downlink | *Reliable* delivery is precisely what turns a missed tap into a late one |
| SCI / LLPM | RP §12.2 — deferred contingency, not implemented |

Each of these is what a competent implementer following ordinary practice would reach for, and not one would fail a test.

### 7.3 `CTR` accounting

Per connection, 8-bit, modulo arithmetic — never comparison, so 255 → 0 is consecutive (A6).

| Case | Action |
|---|---|
| `last + 1` | Accept |
| `≤ last`, within a window of 8 | Duplicate. Discard, `radio_dup++`, `LOG`. **No `EVT`** (A4) |
| `> last + 1` | **Accept**, and `radio_gap += gap`. `LOG` (A5) |

A gap **accepts the event**. A real press must not be discarded because an earlier one was lost — the gap is a finding about the link, not a reason to drop the press in hand.

On `UP_READY`, re-baseline `last` to the frame's `ctr_base` and log the rebaseline **instead of** a gap (A7). A remote that rebooted restarted its counter; reporting that as a 200-frame gap would bury every real gap in noise. A remote that reconnected *without* rebooting sends its continuing value, and a genuine gap across the disconnection is reported as one — presses made out of range genuinely were lost (A18).

### 7.4 Link state and `DN_HOST`

`on_link()` delivers **already-debounced** state (RP §9.4):

- → `CONNECTED` only after encryption, identity validation and CCCD subscription. A half-established connection is not a connected remote.
- → `CONNECTING` immediately on disconnection, so the app has something true to show meanwhile.
- → `DISCONNECTED` only after 2 s with no reconnection. A remote back inside 2 s produces no `DISCONNECTED` line at all (A17).

`LINK <remote> CONNECTED` **must always carry an RSSI value** — the app's signal indicator has no other source, and a `CONNECTED` line without one is dropped by the app's parser, so the indicator silently never updates. RSSI is averaged over the last 8 connection events, sampled on the 10 s re-emission tick; a single reading is a sample of one hop of a frequency-hopping link and would jitter across its whole range while the link was stable.

`DN_HOST` carries the half of the path the remote cannot see — the USB cable, the browser tab, the laptop's sleep state, the app watchdog. Sent `DOWN` on supervision expiry, `UP` on the next line from the app, and **re-sent to any remote that connects**. The remote renders `LED_LINK` as the conjunction; the dongle's job is only to keep it honest.

### 7.5 Presses with nowhere to go

A press arriving while the app is absent is **dropped, not queued.** Counted as `presses_dropped_no_host` and reported in the first `LOG` line after the app returns.

Never queued: a press applied minutes later is a wrong score with no visible cause, and it arrives with no acknowledgement tap to warn anyone it happened. The referee already knows before pressing, because `DN_HOST DOWN` has put the remote into link-lost rendering.

---

## 8. Counters and diagnostics

The console is disabled and a second CDC-ACM instance is forbidden (§10), so `LOG` and `ERR` lines are the dongle's only diagnostic channel. That is sufficient because the app already logs both directions with timestamps.

| Counter | Meaning |
|---|---|
| `radio_gap` | Uplink frames lost, by `CTR` gap |
| `radio_dup` | Uplink duplicates rejected |
| `presses_dropped_no_host` | Presses arriving with no app connected |
| `taps_dropped_late` | `ACK`s whose budget was already spent (mechanism 1) |
| `tx_drops` | Whole lines dropped from the USB transmit ring |
| `beat_drops` | `BEAT` frames dropped under downlink pressure |
| `evt_emitted` | Denominator for all of the above |

Emitted as a `LOG` line on handshake, together with the connection interval in use so that any later latency figure is attributable to a known rung.

**The transmit drop counter is new and it is not optional.** `usb_link_send()` has two silent drop paths today — an over-length line and a full ring — and returns `void`, so a dropped line is indistinguishable from a line that was never sent. `PLAN.md` §5.4 B2 makes ring saturation a two-remote condition, which means it will first appear exactly when it is hardest to diagnose. Instrument the drop path before the traffic that saturates it exists.

`BEAT` is the only frame on either link that may be dropped, and it is dropped first under pressure. Its drops are counted separately because a `BEAT` drop is expected under load and a `TAP` drop is a defect, and one counter cannot say both.

---

## 9. Provisioning

**Redesigned 2026-08-12 — PLAN.md §4.13 reverses §4.10's original "bench tool writes a flash partition" design.** Identity is baked into the firmware image at build time instead: `dongle/tools/provision.py` generates a small C header, `provisioning_data.h`, containing a `static const struct provisioning_record PROV_RECORD` literal, and `west build` refuses to configure without one.

**Why the reversal.** The original design wrote a 55-byte record to `storage_partition` (16 KB at `0xf0000`) as a separate step, over Serial DFU, after the application was already flashed. Attempting this for real against W1 surfaced that this board's bootloader (`BOARD.md` §1: Nordic's factory-programmed nRF5 "Open bootloader") activates *whatever it receives* into the fixed application slot, regardless of the address the source hex file's own Extended Linear Address record claims — confirmed against Nordic's own DFU documentation and this board's dual-slot flash map (`BOARD.md` §4: `slot0_partition`/`slot1_partition`, `storage_partition` outside both). Every provisioning write was landing on the application, not `storage_partition` — which is why the app stopped enumerating right after a write (a bare 55-byte record isn't a valid image) and why the reported serial never actually changed. The dongle's sealed enclosure exposes only USB-C, with no SWD probe on the bench to reach `storage_partition` directly, so the DFU limitation isn't a workaround-able inconvenience on this hardware — it rules the original design out. PLAN.md §4.13 has the full reasoning, including why the original argument against baking identity into the build doesn't hold for this project's actual manufacturing model (one person, no post-sale firmware updates, no adversarial threat model).

**The record itself** (`common/provisioning.h`): `set_serial[12]`, `role`, `own_addr[6]`, `peer_addr[2][6]`, `set_key[16]`. No `magic`/`version`/`crc32` — that machinery protected against corruption introduced by a separate, fallible write step, and that failure mode doesn't exist once the record is compiled into the same image, written by the same operation, as the application code around it.

**`../tools/provision.py`** generates a `provisioning_data.h` per role — dongle, RED, GREEN — from a serial, in its own output directory, plus a human-readable manifest. Prints the exact `west build` command for each role, including `-DCONFIG_PROVISIONING_HEADER_DIR=<dir>`.

**`CONFIG_PROVISIONING_HEADER_DIR` is a Kconfig string, not a plain CMake `-D` variable**, because `west build` runs under sysbuild, which only forwards `CONFIG_`-prefixed values into a specific image's cache automatically from a bare `-D` — the same reason `CONFIG_DONGLE_RADIO` already works with a bare `-D` and an arbitrary variable would not. Its value must be quoted on the command line (`-DCONFIG_PROVISIONING_HEADER_DIR="C:/path/to/dir"`) so the generated Kconfig fragment is a well-formed string literal.

**On any failure, do not initiate, do not advertise, render the fault, and emit `ERR NO_PROVISIONING`** (A19) — kept, at two points instead of one:

- **Build-time, the primary guard:** `CMakeLists.txt` `FATAL_ERROR`s at configure time if `CONFIG_PROVISIONING_HEADER_DIR` is unset or the file isn't there. There is no default to silently fall back to — omitting it is a build error, not a boot-time state.
- **Boot-time, belt and braces:** `engine_start()` calls `provisioning_validate(&PROV_RECORD)` before anything else touches the wire — structural sanity (serial charset, role range, an all-zero `set_key` sentinel), not a parser. On failure it logs the specific reason and `HELLO`'s `<set>` field falls back to the fixed, deliberately-implausible `RR-0000`, same as before.

**Layout, split like `protocol.c`.** `common/provisioning.h`/`.c` is the Zephyr-free struct definition and `provisioning_validate()` — no board, no SDK, host-tested at `dongle/tests/provisioning` (`make check`, 9 checks). `dongle/src/provisioning_flash.c` is deleted: there is nothing left to read from flash, and nothing else on this board reads a flash partition (`CONFIG_FLASH`/`CONFIG_FLASH_MAP` dropped from `prj.conf`).

**The role enum's numeric values** (`DONGLE=0`, `RED=1`, `GREEN=2`) are still pinned in `common/provisioning.h`, for the same drift reason `CLAUDE.md` §2 names for `PROTOCOL.md`: `provision.py`'s generator and the firmware source must agree on them, or a header that looks fine associates the wrong remote.

**Verified:** the host suite (9 checks: valid record for all three roles, serial charset including the full-12-bytes-no-NUL edge case, bad role, all-zero-key sentinel rejected, a single nonzero key byte accepted); both firmwares rebuild clean and smaller against a generated set (radio=y 181,668 B, was 185,212 B; no-radio 54,936 B, was 56,200 B); and the configure-time refusal was confirmed to actually fire with no header directory supplied. **Not yet possible:** A19 on real hardware under this mechanism — the original "confirmed on hardware" result was for the now-abandoned flash-partition design and is in doubt for the reason above (the negative/unprovisioned case, needing only erased flash, is unaffected).

**A device that briefly stops enumerating right after a DFU write is not necessarily corrupted** — the bootloader can sit in DFU mode after a write, pending a manual power cycle, rather than jumping back to the application on its own. Unplug and replug (without holding the button) before assuming anything is wrong. This still applies to ordinary application updates, which is the only kind of DFU write this design performs now.

---

## 10. Configuration

| Symbol | Default | Meaning |
|---|---|---|
| `CONFIG_DONGLE_RADIO` | **`n` until stage 3**, then `y` | Selects `radio_ble.c`; `n` selects `radio_null.c`. It defaults to `n` while `radio_ble.c` does not exist, and `CMakeLists.txt` refuses `y` with a sentence naming stage 3 — a missing-file link error is not a useful way to learn that a feature has not been written |
| `CONFIG_DONGLE_CONN_INTERVAL_UNITS` | `6` | Connection interval in 1.25 ms units. RP §12.2 |
| `CONFIG_DONGLE_SET_SERIAL_FALLBACK` | `"RR-0000"` | Used only before Stage 2 exists. Removed once the record is read |

**`CONFIG_DONGLE_FAKE_LINK` is deleted**, not defaulted off — §3.1.

Retained from v2.0 and load-bearing: console, shell and logging off; the new `device_next` USB stack; `CDC_ACM_SERIAL_INITIALIZE_AT_BOOT`; and the `BUILD_ASSERT` in `usb_link.c` that fails the build if a second CDC-ACM instance appears. That assert is the guard against the trap the upstream `cdc_acm` sample falls into on this board, where log output interleaves into the protocol stream and corrupts lines intermittently and silently.

BLE additions bring `CONFIG_BT`, central role, the SoftDevice Controller, and `CONFIG_BT_SMP` compiled in as plumbing only — bondable off, pairing rejected.

---

## 11. Acceptance

Rung names are `PLAN.md`'s, kept so the results log stays continuous.

| Stage | Delivers | Green when |
|---|---|---|
| **0** | Host suites rewritten to v3.0 before the parser is touched | ◐ 2026-08-11 — toolchain in, protocol suite green at 131 checks. The `rframe` suite moves to stage 3, still written before its codec |
| **1** | Wire v3.0, the seam, `radio_null` | ◐ 2026-08-11 — code-complete, **V0** green (T1–T16, both fail-closed cases), both build configurations clean. Outstanding: wire-log diff against the emulator (modulo §5.7), and **V1–V6** on hardware with `TEST 0` sent first |
| **2** | Provisioning record, reader, refusal path, bench tool | **A19** on both boards. One set provisioned |
| **3** | `rframe` codec, then BLE at 7.5 ms | ✅ **W0 and W1 both closed on hardware, 2026-08-12 and 2026-08-13.** `common/rframe.c/.h` and `src/radio_ble.c` written and building clean under `-DCONFIG_DONGLE_RADIO=y`; `dongle/tests/rframe` green, 0 failures. A DK (RED) connects, encrypts, discovers, subscribes and reports `CONNECTED` with real RSSI, and all four negative cases (A12 wrong key, A13 set mismatch, A14 proto major, A19 unprovisioned remote) refuse exactly as `RADIO_PROTOCOL.md` §14 specifies, each confirmed on hardware. Getting there required rewriting the boot-time connection scheduler — Zephyr allows only one outstanding `bt_conn_le_create()` system-wide, and the original "fire both remotes' create calls at boot" logic could let one remote's unbounded search starve the other permanently (`HISTORY.md` §9.2/§9.3) |
| **4** | End to end | ◐ **W2 and W3 closed on hardware 2026-08-13** — the whole DK remote firmware (`../remote/src/`) confirmed against a live scoreboard: all three gestures on all four DK buttons, `HAP RED` and `CFG` observed for the first time on any hardware, `ACK … SILENT` producing zero wire traffic (`A20`). **W4, W5 and the full demonstration remain — `PLAN.md` queue steps S6–S7** |
| **5** | Measurement | **W6–W8**, **V8**, **R2**. Latency at range for the record, not as a gate |

**Deferred with reasons recorded:** V7 (version guard — needs a deliberately-wrong rebuild, and the app-side guard is unit-tested at M1) and V8 as a gate rather than as an overnight run once Stage 4 is stable.

---

## 12. Traps

Each of these produces no useful error message.

| Trap | Symptom | Guard |
|---|---|---|
| **`TEST 3` left on** | Supervision suspended until `TEST 0` or reboot. Every supervision test passes for the wrong reason | Send `TEST 0` first and confirm the reply |
| **A second CDC-ACM instance** | Log output interleaves into the protocol stream; lines corrupt intermittently | The `BUILD_ASSERT` in `usb_link.c`. Do not weaken it |
| **Gating transmission on DTR** | Dongle enumerates but never answers `INFO` | Never gate. The app never calls `setSignals()` |
| **A dongle-side indicator cache** | Indicators diverge from the scoreboard, silently | §5.5 — relay unconditionally |
| **Broadcast acknowledgement** | Correct on a one-remote bench, wrong in every match | §6.2 — route by `src` |
| **Raising the ATT MTU** | Nothing fails. The acknowledgement tail lengthens by milliseconds nobody attributes to it | §7.2 |
| **Lazy pending expiry at 120 ms** | A late `ACK` fires a tap the referee cannot account for | §6.1 — sweep, do not purge lazily |
| **`radio_null` suppressing `TEST`** | Every test mode silently emits nothing | §3.2 — bypass by origin |
| **Stale `§` references in comments** | v2.0 section numbers survive the rewrite and misdirect the next reader | Re-check every `§` citation touched |
