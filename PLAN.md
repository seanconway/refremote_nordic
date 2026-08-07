# Dongle ↔ Scoreboard Interface — Plan and Status

**Status as of 2026-08-07: the USB half is built and working end to end. BLE is
not started.**

This document records where the project stands, the decisions that still bind
future work, and the seams the BLE milestone will attach to. It deliberately
does **not** design the BLE layer.

| For | See |
|---|---|
| Build, flash, manual test | [`dongle/README.md`](dongle/README.md) |
| Test procedures and pass criteria | [`dongle/VALIDATION.md`](dongle/VALIDATION.md) |
| The wire protocol itself | [`PROTOCOL.md`](PROTOCOL.md) |

---

## 1. Where things stand

| Component | State |
|---|---|
| `PROTOCOL.md` v2.0 | Byte-identical in both repos. Amended 2026-08-07 (§10 T9/T9c). |
| Scoreboard app | Complete. 57 tests passing. Debug panel can send raw lines. |
| Dongle USB firmware | Complete: framing, parser, clock, heartbeat, supervision, CONFIRM, TEST modes. 52 KB flash, 19 KB RAM. |
| Dongle BLE | **Not started.** `CONFIG_DONGLE_FAKE_LINK` fabricates link state; LEDs stand in for haptics. |
| Host parser tests | Written, **never executed** — no C compiler on the dev machine. |
| Interface validation | V1–V3 pass. V0, V4–V8 not run. Two risks unmeasured. See VALIDATION.md. |

**Working, in both directions, on real hardware:** the §8 handshake, the 2 s
PING cadence, clock state and the 1 Hz heartbeat, link supervision with
`ERR APP_TIMEOUT`, end-to-end `CONFIRM`, and all four TEST modes driving the
scoreboard.

---

## 2. What was built

```
dongle/
├── prj.conf                    console/shell/logging OFF — see §3.4
├── Kconfig                     CONFIG_DONGLE_FAKE_LINK
├── src/
│   ├── protocol.c/.h           framing + parse + encode. NO Zephyr deps.
│   ├── usb_link.c/.h           only file touching the UART API
│   ├── engine.c/.h             clock, heartbeat, supervision, CONFIRM, TEST
│   ├── indicator.c/.h          LED stand-in for BLE haptics
│   └── main.c
├── tests/protocol/             host tests for PROTOCOL.md §10
└── tools/ncsenv.sh             reconstructs the NCS env for CLI builds
```

App-side additions this milestone: `DongleService.sendRaw()` and a command
input in the debug panel, because the COM port is exclusive — with the app
connected, a serial terminal cannot also be attached, so TEST modes had to be
reachable from the UI.

---

## 3. Decisions that still bind

These were established with evidence and should not be revisited casually.

### 3.1 NCS v3.4.0, pinned

v3.4.0 is the **LTS** release (5 years of security and critical fixes) and the
**last release supporting the nRF52 Series**, which it declares *feature
complete*. Nordic explicitly recommends it for new nRF52840 designs. Older
DevZone advice recommending v3.1.x for this dongle predates that announcement
and is superseded. Do not downgrade, and expect no newer SDK to support this
chip.

### 3.2 Board target `raytac_mdbt50q_cx_40_dongle/nrf52840`

Present upstream in the installed tree. Not interchangeable with
`nrf52840dongle/nrf52840` — LED/button mapping differs, and so does bootloader
entry (**hold the button while plugging in**, rather than pressing RESET).
`FLASH_LOAD_OFFSET=0x1000` is applied automatically for the nRF5 bootloader;
never set it by hand, and do not enable MCUboot/sysbuild without deliberately
switching bootloaders.

### 3.3 The new USB stack (`device_next`)

The board defaults it on. Reports of "old and new stack conflict" come from
fighting that default with legacy-stack samples.

### 3.4 The protocol owns the CDC-ACM port exclusively

The board's common devicetree points `zephyr,console`, `zephyr,shell-uart`,
`zephyr,uart-mcumgr` and both `bt-*-uart` chosen nodes at a single CDC-ACM
instance. Anything left live interleaves bytes into the protocol stream: a log
line over 120 bytes trips the receiver's discard-and-resync path, and a write
landing mid-line corrupts that line. Both failures are silent and intermittent.

So console, shell and logging are off in `prj.conf`, and a `BUILD_ASSERT` in
`usb_link.c` fails the build if a second CDC-ACM instance ever appears.
Diagnostics leave as protocol `LOG`/`ERR` lines.

*This is the trap the upstream `cdc_acm` sample falls into on this board, and it
cost real time during bring-up.*

### 3.5 Never gate transmission on DTR

The app opens the port via Web Serial and never calls `setSignals()`, so DTR
assertion is the browser's default rather than anything the protocol
guarantees. Gating on it yields a dongle that enumerates but never answers
`INFO`. Boot-time `HELLO` is best-effort; the handshake is driven by the app
sending `INFO`.

### 3.6 Everything runs on the system workqueue

All engine timers — heartbeat, supervision, LINK re-emission, TEST drivers —
and the RX drain run on the system workqueue. That gives exactly one producer
feeding the transmit path, so no locking is needed between them.

It is recorded here because it is the constraint most likely to matter next:
see §5.

---

## 4. Validation status

Moved to [`dongle/VALIDATION.md`](dongle/VALIDATION.md), which carries the full
ladder, procedures, pass criteria and a results log.

Summary: **V1–V3 pass. V0 and V4–V8 have never been run.** The highest-value
outstanding item is V0 — the host parser tests exist and have never executed,
so the firmware parser is currently trusted on inspection alone. It needs only
a machine with a C compiler.

Two risks remain unmeasured and are written up in VALIDATION.md §7: background
tab throttling versus the 2 s PING, and the 500 ms `CONFIRM` latency budget.

---

## 5. Starting point for the BLE milestone

**The wire protocol does not change.** That was the point of building this half
first: `PROTOCOL.md` §3 and §4 already describe every message the BLE layer
needs to produce or consume, and the app is already complete against them.

### 5.1 Seams that exist in the code today

Each is currently satisfied by a stand-in:

| Seam | Location | Currently |
|---|---|---|
| Link state source | `engine.c` — `links[]`, populated in `engine_init()` under `#ifdef CONFIG_DONGLE_FAKE_LINK` | Synthetic `CONNECTED` with fixed RSSI/battery |
| Event origination | `engine.c` — `send_evt(action, src)` | Called only from `test_handler()` |
| Heartbeat delivery | `engine.c` — `heartbeat_handler()`, marked `TODO(BLE)` | Pulses an LED |
| Confirmation delivery | `engine.c` — `PROTO_CONFIRM` case, marked `TODO(BLE)` | Pulses an LED, not routed by `src` |
| Expiration delivery | `engine.c` — `PROTO_EXPIRE` case | Pulses an LED |
| Haptic patterns | `indicator.c/.h` | The whole file is the stand-in (§5.2 timings) |

`send_evt()` already allocates and wraps the sequence number and registers
confirmable actions in the pending table, so an event arriving from a remote
needs to reach that function rather than reimplement around it.

### 5.2 Constraints this half imposes

Facts the BLE work inherits, not suggestions:

- **`LINK … CONNECTED` must always carry RSSI.** The app rejects the short form
  outright, silently. Battery is optional.
- **Only `ADD_POINT` and `REMOVE_POINT` are confirmed**, routed to the
  originating remote by the `src` recorded against that `seq`. 8-entry table,
  500 ms window, no failure haptic by design.
- **`seq` is per-`EVT`, 0–999, wrapping**, and exists for loss detection only.
- **`LINK` re-emits every 10 s while connected**, so the app's indicators stay
  fresh without polling.
- **Nothing may write to the CDC-ACM port except the protocol** (§3.4). This has
  a practical consequence for BLE bring-up: the console is disabled, and the
  `BUILD_ASSERT` forbids a second CDC instance in the default build. How to get
  diagnostics out during BLE development is a decision to make early.
- **The TX ring is 1024 bytes and drops whole lines when full.** Bench traffic
  never approached that; real remotes plus re-emission will raise it.
- **`CONFIG_DONGLE_FAKE_LINK` must go to `n`.** Left on, the dongle reports
  remotes as connected that are not.

### 5.3 Questions the BLE design will have to answer

Listed as open questions, deliberately unanswered here:

- Does BLE work share the system workqueue (§3.6), or does the engine need its
  own? What jitter does the 1 Hz heartbeat tolerate?
- How is a physical remote bound to the `RED` / `GREEN` identity — pairing,
  bonding, fixed addresses, or configuration?
- How are heartbeat, confirmation and expiration commands carried to a remote,
  and what latency does that add to the 500 ms confirmation budget?
- How is `LINK` state derived and debounced, so a remote at the edge of range
  does not flood the USB link with transitions?
- Where do events go that arrive while the app is disconnected — dropped, or
  queued?
- What is the power and latency budget for a 1 Hz pulse to two remotes?

### 5.4 What must be re-validated afterwards

[`VALIDATION.md`](dongle/VALIDATION.md) §6 lists seven specific ways the BLE
layer can regress this link **without touching any USB code** — workqueue
contention, TX ring saturation, LINK churn, out-of-range RSSI/battery,
confirmation routing, and the latency budget. Read that section before writing
BLE code, not after.

---

## 6. Known gaps and debt

| Item | Impact |
|---|---|
| Host parser tests never executed | Firmware parser trusted on inspection alone |
| V4–V8 not run | Supervision, reconnect and soak behaviour unverified |
| Soak instrumentation missing | No running `seq`-gap counter, latency measurement, or log export — V8 is unfalsifiable without them |
| USB identity is Zephyr's test VID/PID (`0x2fe3`/`0x0004`, `"CDC ACM serial backend"`) | Blocks the enterprise deployment path; a policy allowlisting a shared VID grants the site access to any Zephyr device |
| `requestPort()` has no `filters` | Users can select the wrong serial device |
| Connection errors surface raw DOMException text | A policy block is indistinguishable from a cancelled picker |
| `README.md` still contains pasted prompt text | Cosmetic; misleading as a repo entry point |
