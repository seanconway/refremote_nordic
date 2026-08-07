# Dongle ↔ Scoreboard Interface — Validation Plan

**Scope:** the USB CDC-ACM link between the dongle and the scoreboard web app,
as specified in [`PROTOCOL.md`](../PROTOCOL.md) v2.0. BLE is out of scope except
where it can *regress* this link — see §6, which is the section that matters
most on a return visit.

**Status as of 2026-08-07:** the interface is functionally working in both
directions. It is **not yet validated.** Rungs V1–V3 pass; V0 and V4–V8 have
never been run. Two significant risks are unmeasured (§7).

**How to use this document.** Work the ladder in order — each rung isolates one
failure domain, and a failure high up is uninterpretable if a lower rung was
skipped. Record the date and firmware version against each result in §9.

---

## 1. Why this exists

The interface "works" in the sense that a happy path completed once. That is a
much weaker claim than "reliable", and the gap between them is where this class
of system fails: at hour three, on a cable pull, on a backgrounded tab, on
someone else's laptop.

Everything below is either a test that has not been run or a risk that has not
been measured. Nothing here is speculative — each item traces to a specific
mechanism in the protocol or a specific behaviour observed during bring-up.

---

## 2. Test rig

| Item | Value |
|---|---|
| Board target | `raytac_mdbt50q_cx_40_dongle/nrf52840` |
| SDK | nRF Connect SDK **v3.4.0** (LTS; last release supporting nRF52) |
| Build | `source dongle/tools/ncsenv.sh` then `west build -b raytac_mdbt50q_cx_40_dongle/nrf52840 dongle -d dongle/build` |
| Artifact | `dongle/build/dongle/zephyr/zephyr.hex` |
| Flash | Hold the button while plugging in (red LED fades), then write the hex with nRF Connect Programmer |
| App | `npm run dev` in `wrsl-app`, `http://localhost:5173/` |
| Browser | Chrome/Edge/Opera 89+, or Firefox 151+ |

**The COM port is exclusive.** A serial terminal and the web app cannot both
hold it. Commands are sent to the dongle from the app's debug panel — click
**Show debug log** to reveal the command input and the quick-command buttons.

**Two standing traps:**

- **`TEST 3` suspends link supervision until `TEST 0` or reboot.** If it is left
  on, every supervision test in V5 passes for the wrong reason. Send `TEST 0`
  before any V5 work and confirm the `LOG TEST 0: stopped, supervision active`
  reply.
- **`CONFIG_DONGLE_FAKE_LINK=y` fabricates `LINK ... CONNECTED`** with synthetic
  RSSI and battery. Any V-test that appears to validate link reporting is
  validating a constant until this is turned off. It **must** be `n` for all
  post-BLE runs.

---

## 3. The ladder

### V0 — Parser unit tests (host, no hardware) — **NEVER RUN**

`protocol.c` has no Zephyr dependencies precisely so this can run anywhere.
It has never executed: the development machine has no host C compiler.

```bash
cd dongle/tests/protocol && make check
```

**Pass:** all cases green, exit 0. Covers PROTOCOL.md §10 T1–T10, plus T9c
(discard survives a chunk boundary), encoder round-trips, and the LINK
constraint of §5 below.

**Priority: highest.** The firmware parser is currently trusted on inspection
alone. Every bug caught here is a bug not chased over USB. Run it on any Linux
box, WSL, macOS, or MinGW/MSYS2 install.

The app's counterpart suite (`npm test` in `wrsl-app`, 57 tests) does run and
passes.

### V1 — Manual terminal, no browser ✅ *passed 2026-08-07*

Disconnect the app first. With a terminal on the port at 115200 8-N-1:

| Send | Expect |
|---|---|
| `INFO` | `HELLO 2.0 0.1.0 0`, then one `LINK` line per remote |
| `PING` | `PONG` |
| `ECHO hello` | `ECHO hello` |
| `CLOCK RUN` | green LED pulses at 1 Hz |
| `CLOCK STOP` | pulsing stops |
| `EXPIRE` | one long red pulse, ~500 ms |

**Pass:** all of the above. Observing `ERR APP_TIMEOUT` ~5 s after the last
typed line is **correct** (§5.1) and is itself strong evidence — it can only be
emitted if the clock genuinely entered RUNNING, so it proves RX, parse, state
transition, timer, and TX in one message.

### V2 — App handshake ✅ *passed 2026-08-07*

**Pass:** app reaches `ready`; debug log shows `INFO` → `HELLO` → two `LINK`
lines → `CLOCK STOP` → `PING` every 2 s thereafter.

### V3 — Deterministic stimulus ✅ *passed 2026-08-07*

Send `TEST 1`.

**Pass:** exactly seven `EVT` lines, alternating RED/GREEN, 500 ms apart, with
contiguous `seq`. Scoreboard state changes for all seven. `CONFIRM <seq>` goes
back for `ADD_POINT` and `REMOVE_POINT` **only** (§6), each drawing a double
green LED pulse. No sequence-gap warnings.

### V4 — Reverse path (app → dongle) — **NOT RUN**

Drive the scoreboard from its own UI, not from `TEST`.

| Action | Expect on the wire |
|---|---|
| Start the match timer | exactly one `CLOCK RUN`; LED begins pulsing |
| Stop the timer | exactly one `CLOCK STOP`; pulsing stops |
| Start it twice without stopping | **no second `CLOCK RUN`**, and no change in blink phase or rate |
| Let a period run to 0:00 | one `EXPIRE`; long red pulse |

**Pass:** one line per state transition and no per-second traffic — the whole
point of R2. A repeated `CLOCK RUN` that doubles the rate or resets phase is a
§5 idempotency failure.

### V5 — Supervision and disconnection — **NOT RUN**

The most important rung. Send `TEST 0` first.

| # | Procedure | Pass criteria |
|---|---|---|
| V5.1 | `CLOCK RUN`, then close the browser tab | LED stops within 5 s; on reconnect the log shows the dongle emitted `ERR APP_TIMEOUT` |
| V5.2 | `CLOCK RUN`, then unplug the dongle | App shows disconnected, stops the PING cadence, offers reconnect |
| V5.3 | `CLOCK RUN`, then sleep the laptop 30 s and wake it | Dongle is `STOPPED` on wake; app either recovers or cleanly reports a stale link |
| V5.4 | `CLOCK RUN`, then kill the browser process outright | LED stops within 5 s |
| V5.5 | Pull the dongle while the app is idle (clock stopped) | Clean disconnect, no spurious `ERR` |
| V5.6 | After any of the above, reconnect and `CLOCK RUN` | Normal operation resumes with no reboot required |

**Pass:** in every case the heartbeat stops within 5 s and recovery is
automatic. This single mechanism is what covers browser crash, tab close, sleep
and cable pull — none of which a transport ACK layer would catch.

### V6 — Reconnect lifecycle — **NOT RUN**

| # | Procedure | Pass criteria |
|---|---|---|
| V6.1 | Disconnect in the app, reconnect | No port picker — `getPorts()` reuses the grant (§8) |
| V6.2 | Unplug, replug, reconnect | Handshake re-runs from scratch: `INFO` → `HELLO` → `LINK` → `CLOCK STOP` |
| V6.3 | Set a score, disconnect, reconnect | **No dongle state is resumed.** Clock forced to a known state by `CLOCK STOP` |
| V6.4 | Reconnect 10× in a row | No leaked readers/writers; no duplicate PING cadences (watch for PING arriving faster than every 2 s) |

V6.4 is the one that finds real bugs — a reader lock or interval leaked per
reconnect is invisible until it isn't.

### V7 — Version guard — **NOT RUN**

Temporarily change `FW_VERSION`/`PROTO_VERSION` handling to emit `HELLO 3.0 …`,
flash, connect.

**Pass:** app enters `refused`, refuses to operate, and tells the user to update
the dongle firmware. Then emit `HELLO 2.1 …`: app warns and **continues**.

Revert afterwards. This is cheap and it is the only mechanism protecting you
from a mixed-firmware fleet later.

### V8 — Soak — **NOT RUN**

`TEST 3` (to hold supervision open only if running without the app; with the
app connected leave supervision **on**), then `TEST 2`. Leave it running.

**Minimum: 4 hours. Preferred: overnight.**

**Pass criteria:**

- **Zero sequence gaps.** On a 3 cm USB link there should never be one. A gap is
  a real finding about the link, and the `seq` field exists solely to surface it.
- Heartbeat phase stable — no drift, no stalls.
- No RAM growth (compare `k_uptime`-stamped free-heap or thread stack high-water
  marks before and after, if instrumented).
- COM port never drops; app never goes stale.
- App remains responsive; the debug log ring buffer does not degrade the UI.

**Instrumentation gap:** the app currently logs sequence gaps as one-off
warnings into a 200-entry ring buffer, which will have rolled over long before
you read it. A running gap **counter**, connection uptime, and log export do not
exist yet. Add them before attempting a meaningful V8, or the result is
unfalsifiable.

**Do not judge scoreboard correctness during V8.** `TEST 2` fires actions at
random, so the board will look nonsensical by design. V8 measures throughput and
`seq` integrity only; V3 is the behavioural test.

---

## 4. Deployment validation — **NOT RUN**

The product ships from a website onto organization-managed computers. That
introduces failure modes no bench test will reveal.

| # | Test | Pass criteria |
|---|---|---|
| D1 | Serve over real HTTPS (not localhost) and connect | Works — a proper cert is a secure context. Plain `http://` on a LAN IP will **not** work |
| D2 | Chrome, Edge, and Firefox 151+ | Connects on all three. Safari is unsupported and should degrade with a clear message |
| D3 | Linux client | Port opens. Requires `dialout` group membership or a udev rule — without it Chrome lists the port and fails to open it, opaquely |
| D4 | Machine with `DefaultSerialGuardSetting=2` | App detects the block and says so in plain language, rather than surfacing a raw DOMException |
| D5 | Machine with `SerialAllowUsbDevicesForUrls` allowlisting your origin + VID/PID | Connects with **no picker and no prompt** |
| D6 | Change the site origin, then reconnect | Confirms the expected loss of all grants — decide the production domain before deployment, not after |

**Blocker for D5:** the dongle currently enumerates with Zephyr's test identity
(`VID 0x2fe3`, `PID 0x0004`, `"CDC ACM serial backend"`). A policy allowlisting
`0x2fe3` would grant the site access to any Zephyr-based device the user plugs
in, and IT will reject it. A real VID/PID and product string are prerequisites
for the enterprise path, and `requestPort()` should carry a matching `filters:`
array.

---

## 5. Interop constraints that are stricter than the spec reads

These are real, discovered during implementation, and each would present as a
silent mystery rather than an error.

1. **`LINK <remote> CONNECTED` is rejected by the app without an RSSI value**
   (`protocol.js` `parseLink`), even though §3.2 reads as though RSSI is
   optional. Firmware must always emit it when connected. Symptom if violated:
   the signal indicator silently never updates.
2. **`CONFIRM` is sent only for `ADD_POINT` and `REMOVE_POINT`.** The pending
   table must not expect confirmation for anything else.
3. **Resynchronisation happens at the next `\n`, never at a chunk boundary.**
   A read boundary carries no information about the stream. The app had a bug
   here (fixed 2026-08-07); PROTOCOL.md §10 was amended with T9c and a
   clarifying paragraph. Both parsers are now pinned by tests.
4. **Nothing but the protocol may write to the CDC-ACM port.** Console, shell
   and logging are disabled in `prj.conf`, and a `BUILD_ASSERT` in `usb_link.c`
   fails the build if a second CDC-ACM instance appears. If either guard is ever
   relaxed, log output will interleave with protocol traffic — corrupting lines
   intermittently and silently.

---

## 6. Regression risks introduced by BLE

**Read this first on returning.** The USB interface is currently validated in an
environment with no radio. Adding BLE can degrade it without touching a line of
USB code.

| # | Risk | Test |
|---|---|---|
| B1 | **Workqueue contention.** Every engine timer — heartbeat, supervision, LINK re-emit, TEST — runs on the system workqueue. BLE work submitted to the same queue can delay them. A blocked or slow BLE handler shows up as heartbeat jitter or a late supervision timeout. | Re-run V4 and V5 with BLE active and both remotes connected. Watch heartbeat regularity. Consider a dedicated workqueue for the engine if jitter appears. |
| B2 | **TX ring saturation.** The TX buffer is 1024 bytes and drops whole lines when full. Real remotes plus 10 s `LINK` re-emission plus event traffic raises the line rate well above bench conditions. | Run V8 with both remotes connected and pressing buttons. Any dropped line is a silent loss — instrument the drop path with a counter before trusting this. |
| B3 | **`CONFIG_DONGLE_FAKE_LINK` still enabled.** Leaves the dongle reporting synthetic `CONNECTED` while real remotes are disconnected. | Set it to `n`. Confirm `INFO` reports `DISCONNECTED` with no remotes powered. |
| B4 | **`LINK` state churn.** Real BLE connections flap. Each transition emits a line; a flapping remote could flood the link. | Power-cycle a remote repeatedly at the edge of range. Confirm no flood, and that the app's indicators track state correctly. |
| B5 | **Real RSSI/battery values.** Bench values are constants. Real ones can be out of the ranges the app accepts (`batt` 0–100; RSSI signed). An out-of-range value is dropped silently. | Verify with a real remote at various distances and battery levels. |
| B6 | **`CONFIRM` routing.** Currently every confirmation pulses the same LED. With BLE it must reach the **originating remote only**, routed by the `src` recorded against that `seq`. | Press RED and GREEN in quick succession; confirm each buzz lands on the correct wrist. |
| B7 | **End-to-end confirmation latency.** See §7 — the 500 ms window now includes a BLE hop. | Re-measure after BLE lands. This is the risk most likely to bite in a real match. |

---

## 7. Unmeasured risks

Both are testable in minutes and neither has been attempted.

### R1 — Background-tab throttling vs. the 2 s PING

Chrome throttles timers in hidden tabs; after several minutes hidden a tab can
drop to roughly one timer callback per minute. The app's PING is a
`setInterval` at 2 s, and the dongle drops to `STOPPED` after 5 s of silence.

§5.1 immunises the *heartbeat generation* against throttling, but the liveness
proof it depends on is still a browser timer.

**Test:** connect, `CLOCK RUN`, confirm the LED pulses. Fully hide the
scoreboard window for 6+ minutes. Return and read the debug log.

**Fail:** `ERR APP_TIMEOUT` present. That means alt-tabbing kills the referees'
heartbeat mid-match.

**Mitigations if it fails,** in order of preference: a screen wake lock
(appropriate anyway for a scoreboard driving a display); moving the PING into a
Web Worker; an operational rule that the tab stays foregrounded. Raising the 5 s
timeout would work but weakens the fail-safe the whole design rests on.

### R2 — The 500 ms `CONFIRM` window is an unmeasured latency budget

§6 drops the pending entry after 500 ms. The real `EVT`→`CONFIRM` round trip
through React render and the browser event loop has never been measured.

**Test:** with `TEST 2` running as background load, pair each `RX EVT … <seq>`
with its `TX CONFIRM <seq>` in the debug log and take the difference. Measure
the **distribution**, not the median — p99 is what matters.

**Fail:** any sample approaching 500 ms.

**Why it matters:** exceeding the window means no haptic. The referee follows
the rule correctly — *no buzz means the point did not land, press again* — and
scores twice. The failure is silent and looks like referee error.

---

## 8. Definition of done

- [ ] V0 green on a machine with a C compiler
- [ ] V1–V8 pass, recorded in §9 with dates and firmware version
- [ ] R1 and R2 measured, with mitigations applied if either fails
- [ ] D1–D6 pass; real VID/PID assigned and `requestPort()` filtered
- [ ] V8 clean for ≥4 h with zero sequence gaps, using real instrumentation
- [ ] §6 re-run in full after BLE lands
- [ ] PROTOCOL.md amended for any further constraint that proves real — the
      spec should describe what shipped

---

## 9. Results log

| Date | FW | Rung | Result | Notes |
|---|---|---|---|---|
| 2026-08-07 | 0.1.0 | V1 | pass | `ERR APP_TIMEOUT` observed and correct |
| 2026-08-07 | 0.1.0 | V2 | pass | handshake and PING cadence confirmed |
| 2026-08-07 | 0.1.0 | V3 | pass | `TEST 1`, seven events, CONFIRM round trip |
| | | | | |
