# Remote firmware — build specification (nRF52840 DK)

**What this document is.** The implementable contract for the remote firmware in its first form: an nRF52840 DK standing in for a wrist remote that does not exist yet.

**What it is not.** It does not restate `RADIO_PROTOCOL.md` v1.0 — that is the contract, cited *RP §n*, and where this document and it disagree, **it wins**. `SCOPE.md` and `SYSTEM_FUNC_SPEC.md` (cited *FS §n*) win over both.

| For | See |
|---|---|
| The radio contract | [`../RADIO_PROTOCOL.md`](../RADIO_PROTOCOL.md) |
| The dongle's half | [`../dongle/BUILD_SPEC.md`](../dongle/BUILD_SPEC.md) |
| Status, stages, results | [`../PLAN.md`](../PLAN.md) |

- **Board:** `nrf52840dk/nrf52840`
- **SDK:** nRF Connect SDK **v3.4.0**, pinned
- **Role:** BLE peripheral, GATT **server**

---

## 1. Scope, and what this firmware cannot claim

The product remote has **seven buttons, four RGB indicators, an ERM with a driver IC, and a PMIC** (FS §3.1–§3.4). The DK has four buttons, four single-colour LEDs, one of which is on PWM, and no motor at all. The gap is not something to work around quietly — it determines what this stage can and cannot demonstrate.

**What it does reach, and reaches fully:** all three gestures and their timings, the inert/no-op distinction, the complete GATT service, association and encryption from a provisioned key, `CTR` accounting, the deadline rule, indicator *mode*, `LED_LINK` as a conjunction, and the whole end-to-end loop from a physical press to a scoreboard to a rendered acknowledgement.

**What it does not reach, and must never be read as reaching:**

| Not reached | Why | Settled by |
|---|---|---|
| Indicator **colour** | `DN_INDICATOR` carries RGB per indicator; the DK's LEDs are single-colour and show mode only. Verify the colour fields by reading them out over RTT and treat the rendering as unvalidated | Real RGB hardware |
| **Anything haptic** | An LED is not a motor. Brightness is not amplitude | R3, R4, with a real ERM on a real strap |
| `LED_PWR` and state of charge | The DK is bus-powered. `UP_TELEMETRY.battery_pct` is **synthetic** — see §8.3 | Real battery and PMIC |
| Tactile discrimination between buttons | Four identical DK buttons; the product's are shaped and sized differently, and `TOGGLE_CLOCK` is an oversized tactile datum | Enclosure design |

**The three missing buttons are deliberately not simulated.** `REMOVE_POINT`, `BACKWARD` and `F2` wait for real GPIO. A shift or bank modifier to reach seven from four is **rejected**: it is a second input path, which is the same objection §2.3 of `PLAN.md` makes to an operator shortcut in the scoreboard, one layer down. It would be firmware the product does not have, exercising timing the product does not have, and it is the only thing under test that would ship nowhere.

---

## 2. Hardware map

The four buttons are mapped for **gesture and semantic coverage, not button coverage.** Each of the seven product buttons is the same code path; what can actually be wrong is the three gestures, the `HOLD_REP` restriction, and the inert/no-op distinction.

| DK button | GPIO | Mapped to | Covers |
|---|---|---|---|
| Button 1 | P0.11 | `ADD_POINT` | `PRESS`. The scoring path, and the one repeated-press scoring rests on |
| Button 2 | P0.12 | `TOGGLE_CLOCK` | `PRESS` **and** `HOLD` at the 600 ms threshold |
| Button 3 | P0.24 | `FORWARD` | `HOLD_REP` at 150 ms — the only button class that repeats |
| Button 4 | P0.25 | `F1` | **Inert vs no-op.** `ACK … SILENT` must put nothing on the air (A20) |

All four are `GPIO_PULL_UP | GPIO_ACTIVE_LOW` in the stock devicetree.

| DK LED | GPIO | Renders | Fidelity |
|---|---|---|---|
| LED 1 | P0.13 (**PWM**) | Haptic activity — waveform shape in time, amplitude as brightness | Proxy. §7.3 |
| LED 2 | P0.14 | `LED_F1` mode from `DN_INDICATOR` | Mode only — `OFF`/`SOLID`. Colour not rendered |
| LED 3 | P0.15 | `LED_F2` mode | Mode only |
| LED 4 | P0.16 | `LED_LINK` — radio-up **AND** `DN_HOST` UP | **Full behaviour.** This is A15 and it is fully testable here |

All four LEDs are active-low.

**This differs from `PLAN.md` §3.4, which put the haptic proxy on LED 4, and the reason is worth recording.** The stock DK devicetree puts only `led0` (P0.13) on a PWM channel — `pwm0_default` assigns `PWM_OUT0` to P0.13 and nothing else. Driving any other LED with PWM means extending the `pwm0` pinctrl in a board overlay. Since physical LED position carries no meaning on a development kit, the mapping is arbitrary and the one that needs no overlay is better: **one fewer thing that can be wrong, and one fewer file that diverges from upstream.** `PLAN.md` §3.4's table is corrected to match.

Consequence: `led0`'s `gpio-leds` node is **not used**. The haptic proxy drives `pwm_led0`, and driving the same pin from both the GPIO and PWM drivers is a conflict that does not announce itself.

---

## 3. Module map

| File | Owns | Zephyr-free |
|---|---|---|
| `../common/rframe.c/.h` | RP v1.0 frame codec, both directions, `CTR` arithmetic | **Yes — enforced** |
| `../common/provisioning.c/.h` | Record layout, `provisioning_validate()` | **Yes — enforced** |
| `src/link.c` | BLE peripheral, advertising, the RefRemote Link Service, uplink `CTR` | No |
| `src/buttons.c` | Debounce and gesture classification | No |
| `src/haptic.c` | Waveform table, `ttl` check, PWM rendering | No |
| `src/indicators.c` | `LED_F1`, `LED_F2`, `LED_LINK` | No |
| `src/main.c` | Boot order, and the refusal path when unprovisioned | No |

`common/` is shared with the dongle by CMake source reference, not by copying. **One codec, compiled into both firmwares and into the host suite** — that is the entire point of it being Zephyr-free, and a second copy would be a second thing to keep in step, which is the trap `PROTOCOL.md` already set once with a filesystem hard link.

---

## 4. Gesture classification

Classification happens **entirely in the remote**, and the gesture crosses the radio as a value rather than being inferred anywhere else (RP §5.3). It is **immediate and identical regardless of link state — including while the link is down.** A referee pressing a button on a dead link gets the same local behaviour; only the delivery fails.

Constants, fixed in firmware and not configurable from anywhere (FS §4.2):

| Constant | Value |
|---|---|
| `DEBOUNCE_MS` | 15 |
| `HOLD_THRESHOLD_MS` | 600 |
| `HOLD_REPEAT_MS` | 150 |

Per-button state machine:

```
IDLE ──edge──► DEBOUNCING ──15 ms, still down──► DOWN
                     │                            │
                 bounced                    released < 600 ms ──► emit PRESS ──► IDLE
                     └──► IDLE                     │
                                            600 ms reached ──► emit HOLD (once)
                                                     │
                                          ┌──────────┴───────────┐
                              repeating button          non-repeating button
                                     │                           │
                        every 150 ms: emit HOLD_REP       nothing more until release
                                     │                           │
                                 released ──► IDLE ◄─────────────┘
```

Three details that are easy to get subtly wrong, each of which produces plausible-looking traffic:

1. **`HOLD` fires once, at the moment the threshold is crossed — not on release.** A referee holding `TOGGLE_CLOCK` expects the clock to reset when the threshold passes, not when they let go.
2. **A press that becomes a `HOLD` never also emits a `PRESS`.** `PRESS` is defined as pressed *and released* before the threshold. Emitting both would double every held button.
3. **`HOLD_REP` is emitted only for `FORWARD` and `BACKWARD`.** Every other button emits at most one `HOLD` per depression. On the DK only `FORWARD` is mapped, so this restriction is testable in exactly one direction — that a non-repeating button (`TOGGLE_CLOCK`, Button 2) held for five seconds produces exactly one frame.

---

## 5. The RefRemote Link Service

GATT server. Base UUID `8f2a0000-6b1f-4d5a-9c3e-1d7b4a0e5c21`.

| Attribute | UUID | Properties |
|---|---|---|
| Service | `8f2a0001-…` | — |
| `RR_IDENTITY` | `8f2a0002-…` | Read |
| `RR_UPLINK` | `8f2a0003-…` | Notify |
| `RR_DOWNLINK` | `8f2a0004-…` | Write Without Response |

**Every characteristic carries `BT_GATT_PERM_*_ENCRYPT`.** An unencrypted peer cannot read identity, subscribe to presses, or command a haptic. Encryption is established from the provisioned key before discovery (§9), so the permission is a second lock on a door already bolted — but it is the lock that fails safe if the first is ever misconfigured.

`RR_IDENTITY`, 20 bytes, read once per connection before the dongle subscribes:

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | `radio_proto_major` = 1 |
| 1 | 1 | `radio_proto_minor` = 0 |
| 2 | 1 | `role` — `0x01` RED, `0x02` GREEN |
| 3–5 | 3 | `fw_major`, `fw_minor`, `fw_patch` |
| 6 | 2 | `caps`, little-endian, `0` |
| 8 | 12 | `set_serial`, ASCII `[A-Z0-9-]`, NUL-padded |

`role` and `set_serial` come from the provisioning record, never from a compile-time constant.

**Notifications, not indications.** An indication needs an ATT confirmation before the next may be sent, which serialises the uplink and adds a round trip inside the 25 ms allocation — while telling us nothing the Link Layer acknowledgement has not already told us.

**A remote must not queue notifications before it is subscribed.** Presses in that window are dropped and counted (§6.2).

---

## 6. Uplink

### 6.1 Frames

All little-endian. Header is `[0] TYPE`, `[1] CTR`.

| Type | Name | Len | When |
|---|---|---|---|
| `0x01` | `UP_INPUT` | 4 | `[2] button`, `[3] gesture` |
| `0x02` | `UP_READY` | 4 | `[2] ctr_base`, `[3] reason` — `0x01` boot, `0x02` reconnect |
| `0x03` | `UP_TELEMETRY` | 6 | `[2] battery_pct`, `[3] flags`, `[4] dn_lost`, `[5] reserved` |
| `0x04` | `UP_DIAG` | ≤20 | Freeform counters. **Never semantic** |

Button values `0x01`–`0x07` in the order `ADD_POINT`, `TOGGLE_CLOCK`, `REMOVE_POINT`, `FORWARD`, `BACKWARD`, `F1`, `F2`. Gestures `0x01` `PRESS`, `0x02` `HOLD`, `0x03` `HOLD_REP`.

`UP_READY` is the **first uplink frame after subscription**, whenever the remote holds no indicator state: at first connection, after a transient radio interruption, and after a mid-match set substitution. It drives `JOIN` at the dongle, which the app answers with a `STATE` line unconditionally.

`UP_TELEMETRY` is sent on connection, on any `battery_pct` change greater than one point, on any `flags` change, and otherwise **every 10 s** — the floor exists so the dongle's 10 s `LINK` re-emission always has a fresh value rather than an arbitrarily old cached one.

### 6.2 `CTR`, and a distinction that decides whether loss is visible

8-bit, per-direction, incremented once per frame, wrapping at 256. Modulo arithmetic throughout — 255 → 0 is consecutive.

**`CTR` advances for every classified press, including presses that cannot be delivered.** That is what makes a press made out of range appear at the dongle as a `radio_gap` on reconnection (A18) rather than vanishing without trace. Dropping the press *and* the number would make the loss invisible, which defeats the counter's only real job.

The exception is presses made before subscription on a fresh connection: `UP_READY` carries `ctr_base` — the value `CTR` will take on the next uplink frame — so the dongle **re-baselines instead of reporting a gap** (A7). This is correct and not a contradiction: a fresh connection has no established baseline for a gap to be measured against, whereas a fade mid-connection does.

Count locally what could not be sent and report it in `UP_DIAG`.

**`CTR` is never the wire protocol's `seq`.** It is 8-bit, assigned by the remote, per remote; `seq` is 16-bit, assigned by the dongle, across both. The dongle translates. `CTR` never appears on the USB link except inside a `LOG` line.

### 6.3 No application-level retry

The remote does **not** retry `UP_INPUT`. Link-layer retransmission inside the connection event is the bounded effort this design wants; anything above it fires only when the press is already worthless and delivers the late tap the acknowledgement rule forbids.

A press that does not arrive produces no `EVT`, no `ACK` and no tap, and the referee's rule handles it: **no tap means the press did not land — press again.** There is deliberately **no failure haptic** — a "delivery failed" buzz would have to be told apart from an acknowledgement by a referee looking at the mat, and getting that wrong costs a point.

---

## 7. Downlink

| Type | Name | Len | Payload |
|---|---|---|---|
| `0x81` | `DN_HAPTIC` | 4 | `[2] waveform`, `[3] ttl` in 4 ms units, 0 = no deadline |
| `0x82` | `DN_INDICATOR` | 10 | `[2] f1_mode`, `[3..5] f1 rgb`, `[6] f2_mode`, `[7..9] f2 rgb` |
| `0x83` | `DN_CONFIG` | 4 | `[2] haptic_scale` 0–100, `[3] led_brightness` 0–100 |
| `0x84` | `DN_HOST` | 3 | `[2] state` — `0x00` DOWN, `0x01` UP |

Unknown `TYPE` → **ignore silently**; that tolerance is what lets v1.1 add frames without breaking v1.0 peers (A2). Known type with the wrong length → ignore, count, and report in `UP_DIAG` (A3). No padding: a frame is exactly as long as its type requires.

The downlink `CTR` is **diagnostic only**. Count gaps and report the total as `dn_lost` — it is the remote's only window onto downlink health, and it is unambiguous in a way nothing else is, because a remote cannot tell a suppressed heartbeat from a lost one by design.

### 7.1 `DN_INDICATOR` — assertion, never accumulation

One frame asserts the **complete app-owned indicator state**. Apply it whole. There is no partial update and no incremental command, so there is no version of this frame that can leave the remote holding a stale half of its state.

**Idempotent (A11):** a frame identical to the last one applied changes nothing visible and re-triggers nothing. Do not flash, pulse, or otherwise acknowledge receipt.

`OFF` and `SOLID` only. There is exactly one blinking indicator in the whole system — `LED_PWR` below 10% — and it is remote-local and unreachable from this link.

### 7.2 `LED_LINK` is a conjunction, and this is the case most likely to be missed

```
LED_LINK solid  ⟺  radio connection up  AND  last DN_HOST said UP
```

The remote can see its radio connection. It **cannot** see the USB cable, the browser tab, the laptop's sleep state, or the application watchdog deliberately dropping the serial link — and every one of those leaves the radio connection perfectly healthy while the scoreboard is gone. A remote rendering `LED_LINK` from radio state alone shows solid to a referee whose presses are going nowhere, which is the exact failure the indicator exists to prevent.

**On boot, assume `DOWN`** until told otherwise (A16). A remote powered on next to a dongle with no laptop attached must show link-lost, because that is what is true.

The remote's own link-lost rendering is **not debounced** — it renders the moment its supervision timer expires. The 2 s debounce lives at the dongle and exists to keep an alarm off the scoreboard, not to keep the referee uninformed.

Link-lost also drives a **repeating double buzz** (FS §10.2), which on the DK is the LED proxy. This and the low-battery haptic are remote-local behaviours with no representation on either protocol.

### 7.3 `DN_HAPTIC`, the waveform table, and the ratio that must be preserved

| Value | Waveform | Character | Amplitude |
|---|---|---|---|
| `0x01` | `TAP` | Single short tap | Full |
| `0x02` | `BEAT` | Single short tap | **Distinctly reduced** |
| `0x03` | `WARN` | Single distinct warning | Full |
| `0x04` | `BUZZ` | Short buzz | Full |
| `0x05` | `LONG` | Long buzz | Full |
| `0x06` | `DOUBLE` | Two pulses | Full |
| `0x07` | `TRIPLE` | Three pulses | Full |

**`BEAT` must be unmistakably weaker than `TAP`** — not different in principle, different in sensation. Repeated-press scoring works by the referee counting acknowledgement taps by feel, and a 1 Hz heartbeat will frequently fall inside a four-press burst. On the DK this is brightness, which proves nothing about the wrist; the requirement is written here because the **table** is what carries it, and the table is what ports to real hardware.

**`DN_CONFIG` scaling must preserve the `BEAT`:`TAP` ratio.** This is the trap RP §7.3 names explicitly: an implementation that applies `haptic_scale` by scaling one amplitude parameter shared by both waveforms satisfies the frame's contract and violates the requirement behind it. `DN_CONFIG` at 20 makes everything quieter; it must not make a heartbeat feel like an acknowledgement.

**Rendering rules:**

- The remote **renders; it decides nothing.** It does not know what a waveform means, does not know whether a match is running, and holds **no queue** beyond the one being rendered.
- A frame arriving mid-render **wins and restarts the motor** — except that `BEAT` is the lowest priority class and is dropped rather than allowed to interrupt. A `BEAT` arriving while a `TAP` renders is dropped; the `TAP` is never truncated (A10).
- **`ttl` check on receipt.** Discard a `TAP` that arrives more than `ttl` after the connection event in which it could first have been sent. **Where the determination cannot be made confidently, render.** The dongle's mechanism 1 carries the guarantee, and a remote guessing at deadlines it cannot measure would drop taps that were fine.

`BEAT` is the only frame on this link that may be dropped. Never retried, never queued. **Burst suppression lives in the scoreboard** — nothing in the remote or the dongle suppresses anything, because nothing below the app knows a scoring burst is in progress.

---

## 8. Connection management

### 8.1 Parameters

Set by the dongle as central; the remote accepts them and must not request changes.

| Parameter | Value |
|---|---|
| Connection interval | **7.5 ms** (RP §12.2 rung 3, baseline BLE) |
| Peripheral latency | **0, and must stay 0** |
| LE supervision timeout | 1000 ms |
| PHY | LE 2M |
| ATT MTU | Default 23. **Do not negotiate up** |
| Data Length Extension | **Do not enable** |
| Connection subrating | Not used |

Peripheral latency and subrating are the standard BLE power levers and both work by skipping connection events — which is exactly what delays an acknowledgement tap. Power must be found elsewhere.

### 8.2 Advertising and reconnection

| Phase | Value |
|---|---|
| After disconnection | Directed, 20 ms interval, for 2 s — the fast path back from a transient fade |
| Then | Undirected connectable, 100 ms, indefinitely, with the dongle's address in the accept list |
| Interval jitter | ±10 ms, randomised per attempt |

The jitter matters at density: thirty sets recovering from the same interference event must not synchronise their advertising.

**No state is resumed on reconnect.** Send `UP_READY`, let the dongle emit `JOIN`, and let the app assert indicator state afresh.

### 8.3 Telemetry is synthetic on the DK, and that is a trap

The DK is bus-powered and has no battery, so `battery_pct` has nothing true to report. It is a **constant**, and it feeds the scoreboard's battery indicator.

This is the same trap `CONFIG_DONGLE_FAKE_LINK` set on the dongle, wearing different clothes and with none of the visibility — there is no Kconfig symbol whose name gives it away. **Anything that appears to validate the battery indicator before real hardware exists is validating a constant.** Make the synthetic value obviously synthetic (a fixed, memorable number, and a `UP_DIAG` line saying so) rather than a plausible one that drifts.

---

## 9. Provisioning

**Redesigned 2026-08-12 — same mechanism as the dongle, sharing `common/provisioning.c`; PLAN.md §4.13 has the full reasoning.** Identity is baked into the firmware image at build time: `dongle/tools/provision.py` generates `provisioning_data.h` for each role, and `west build` refuses to configure without one (`CONFIG_PROVISIONING_HEADER_DIR`, a required Kconfig string — see `dongle/BUILD_SPEC.md` §9 for why this has to be a Kconfig option and not a plain CMake `-D`, and why it must be quoted).

This DK has its own working `storage_partition` reachable via SWD (the onboard debugger makes it a non-issue here), but the dongle's sealed enclosure and DFU-only access do not, and the two boards use one shared design rather than two — §4.13's decision was to keep dongle and remote symmetric.

A remote's record carries the dongle in `peer_addr[0]` and a zero slot in `peer_addr[1]`.

**On failure: do not advertise, render an unmistakable fault indication, and stop** (A19) — kept at two points. `CMakeLists.txt` refuses to configure at all without a real `provisioning_data.h`; `main.c` still calls `provisioning_validate(&PROV_RECORD)` before doing anything else, the same place `prov_flash_load()` used to be called, catching a header that exists but is wrong rather than absent. There is no safe default for *which set am I in*, and a unit that guesses is a unit that can join a neighbouring match.

**No pairing procedure is ever performed.** Both ends install the provisioned `set_key` as the LTK via `bt_nrf_conn_set_ltk()`. Pairing requests are rejected in both directions, the device is not bondable, and there is no bond store — therefore nothing a firmware update or a settings-schema change can silently clear. That was the whole argument against bonding at manufacture: a set that has forgotten its binding presents at an event as two remotes that will not connect, with no diagnostic.

---

## 10. Configuration

| Symbol | Default | Meaning |
|---|---|---|
| `CONFIG_REMOTE_HAPTIC_PROXY_LED` | `y` | Render haptics on `pwm_led0`. Set `n` when a real motor driver exists |
| `CONFIG_REMOTE_SYNTHETIC_BATTERY` | `y` | No battery on this board. §8.3 |

No console or shell is required on the DK, but neither is forbidden — unlike the dongle, the DK has an onboard debugger, so **RTT is free here and costs no bootloader.** Use it for the diagnostics the dongle cannot emit: the RGB values of a `DN_INDICATOR` that the LEDs cannot show, `CTR` state, and the gesture classifier's transitions.

---

## 11. Acceptance

Rung names are `PLAN.md`'s.

| Stage | Green when |
|---|---|
| **3** | ◐ **W0 green 2026-08-12; W1's positive case confirmed on hardware 2026-08-13** — `common/rframe.c/.h` shared with the dongle build, host suite green (`dongle/tests/rframe`, 20 checks, 67 assertions, 0 failures — `PLAN.md` §9.2/§9.3). **W0** — codec cases A1–A7, A11, A20 green on the host, with no board involved. **W1** — a DK (RED) connects, encrypted from the provisioned key, `RR_IDENTITY` read and validated, CCCD subscribed, `LED_LINK` (DK LED 4) solid, confirming the same from the remote's own side. **The negatives A12, A13, A14, A19 are still to be run — a pass on the positive case alone is not a pass — and A13 cannot pass yet as written**: the dongle's `set_serial` comparison in `handle_identity_read()` is a known, unimplemented deferral |
| **4** | ◐ **Code-complete 2026-08-12** — `src/link.c`, `buttons.c`, `haptic.c`, `indicators.c`, `main.c` all written, building clean against `nrf52840dk/nrf52840` with a generated `provisioning_data.h` (§9, PLAN.md §4.13). **W2** uplink — all three gestures, with 600 ms and 150 ms **measured, not assumed**; A4, A5, A6. **W3** downlink — idempotent indicators (A11), and **A20 verified by counting frames**, not by watching an LED that was never going to light. **W4** round trip — A8, A9, A10, and `taps_dropped_late` non-zero when provoked and zero when not. **W5** link state — **A15**, A16, A17, A18, A7 — all still to be run on hardware |
| **5** | Second peripheral, range, density, soak |

**The demonstration that closes Stage 4:** press Button 1 on the DK → `UP_INPUT` → `EVT` → the scoreboard scores → `ACK` → `DN_HAPTIC TAP` → LED 1 pulses on that DK, and on that DK only.

---

## 12. Traps

| Trap | Symptom | Guard |
|---|---|---|
| **`led0` driven as both GPIO and PWM** | Contention on P0.13 with no error | Use `pwm_led0` only; leave the `gpio-leds` node alone |
| **`HOLD` on release instead of on threshold** | Clock resets when the referee lets go, not when they expect | §4 detail 1 |
| **`PRESS` also emitted after a `HOLD`** | Every held button acts twice | §4 detail 2 |
| **One shared amplitude for `BEAT` and `TAP`** | Satisfies `DN_CONFIG`, violates the requirement behind it. Invisible until a referee miscounts | §7.3 |
| **`LED_LINK` from radio state alone** | Solid link indication to a referee whose presses go nowhere | §7.2 |
| **Rendering "connected" at boot pending contact** | A16. The remote must fail to link-lost, not to optimism | §7.2 |
| **Synthetic battery read as real** | The scoreboard's battery indicator validates a constant | §8.3 |
| **Queueing haptics** | A tap rendered late is worse than one not rendered | §7.3 |
| **Retrying `UP_INPUT`** | Produces exactly the late tap the design forbids | §6.3 |
| **Raising the ATT MTU** | Nothing fails; air time and latency both grow | §8.1 |

---

## 13. What changes when real hardware arrives

Recorded so that this firmware is written as something to extend rather than replace. The claim the staging buys is that the **port is a board layer swap**, and it is only true if the board layer is separable now.

| Arrives | Replaces | Untouched |
|---|---|---|
| Seven GPIO buttons in the FS §3.1 layout | The four-button map of §2 | The classifier of §4 |
| Four RGB indicators | `indicators.c`'s single-colour rendering | `DN_INDICATOR` parsing and idempotence |
| ERM and driver IC | `haptic.c`'s PWM back end | The waveform **table**, the `ttl` rule, the priority classes |
| nPM1300, real state of charge | The synthetic telemetry of §8.3 | `UP_TELEMETRY` framing |

Keep the board-dependent parts behind devicetree aliases and a thin back end per subsystem. Nothing in `link.c`, `rframe.c` or `provisioning.c` should need to know which board it is on.
