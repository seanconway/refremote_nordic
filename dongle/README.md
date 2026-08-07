# Dongle firmware — USB bridge

Implements the dongle half of [`PROTOCOL.md`](../PROTOCOL.md) v2.0 over USB
CDC-ACM. BLE is not implemented yet; the `TEST` modes of §7.2 stand in for real
remotes, which is what lets the whole USB interface be validated first.

- **Board:** `raytac_mdbt50q_cx_40_dongle/nrf52840`
- **SDK:** nRF Connect SDK **v3.4.0** (LTS; the last release supporting nRF52)

> **Before trusting this link in a match, work through
> [`VALIDATION.md`](VALIDATION.md).** The interface is functionally working but
> not yet validated: the host parser tests have never been executed, and the
> supervision, reconnect and soak rungs are untouched. That document also lists
> the ways adding BLE can regress this link without touching any USB code.

## Layout

| File | Role |
|---|---|
| `src/protocol.c/.h` | Line assembler, parser, encoders. **No Zephyr dependencies** — compiles under host gcc (§11). |
| `src/usb_link.c/.h` | The only file that touches the UART API. Ring buffers + interrupt-driven CDC-ACM. |
| `src/engine.c/.h` | Clock state, 1 Hz heartbeat, link supervision, CONFIRM table, TEST modes. |
| `src/indicator.c/.h` | LED stand-in for the BLE haptics of §5.2. |
| `tests/protocol/` | Host unit tests for §10 T1–T10. |

**The protocol owns the CDC-ACM port exclusively.** Console, shell and logging
are disabled in `prj.conf`, and a `BUILD_ASSERT` in `usb_link.c` fails the build
if a second CDC-ACM instance ever appears. Diagnostics leave the dongle as
protocol `LOG` / `ERR` lines instead. See PLAN.md §1.4 for why.

## Build

From the nRF Connect VS Code extension (already configured), or on the CLI:

```bash
source tools/ncsenv.sh        # reconstructs the NCS v3.4.0 environment
cd <repo root>
west build -b raytac_mdbt50q_cx_40_dongle/nrf52840 dongle -d dongle/build
```

Output: `dongle/build/dongle/zephyr/zephyr.hex`

## Flash

**Enter the bootloader first: hold the button while plugging the dongle in.**
The button is on the far side from the USB connector and pushes sideways,
toward the connector. The red LED fades when the bootloader is running.

Then either:

- **nRF Connect Programmer** — select `zephyr.hex` and write. (Known-good on
  this setup.)
- **nrfutil**:
  ```bash
  nrfutil nrf5sdk-tools pkg generate --hw-version 52 --sd-req=0x00 \
      --application dongle/build/dongle/zephyr/zephyr.hex \
      --application-version 1 dongle.zip
  nrfutil nrf5sdk-tools dfu usb-serial -pkg dongle.zip -p COMx
  ```

## Manual test (PROTOCOL.md §7.1)

Open a terminal on the dongle's COM port at 115200 8-N-1 and type. No browser,
no scoreboard, no harness — this is the fastest way to tell whether a bug is in
firmware or in the web app.

```
INFO
```
→ `HELLO 2.0 0.1.0 0`, then a `LINK` line per remote.

```
PING            → PONG
ECHO hello      → ECHO hello
CLOCK RUN       → green LED starts pulsing at 1 Hz
CLOCK STOP      → pulsing stops
EXPIRE          → red LED, one long 500 ms pulse
TEST 1          → seven EVT lines, alternating RED/GREEN, 500 ms apart
TEST 2          → random EVT lines at ~5 Hz until TEST 0
TEST 0          → stops test mode, re-enables supervision
```

**The 5 s supervision timeout will fire during manual idling and stop the
heartbeat — that is correct behaviour (§5.1).** Type `PING` to hold it open, or
`TEST 3` to suspend supervision for bench work.

### What to look for

- `CLOCK RUN` twice in a row must **not** double the blink rate (idempotent, §5).
- Leave `CLOCK RUN` running and stop typing: within 5 s the LED stops and
  `ERR APP_TIMEOUT` appears. `CLOCK RUN` again recovers.
- `TEST 1` emits exactly seven events with contiguous `seq` values.

## Host unit tests

`protocol.c` has no Zephyr dependencies, so §10 runs on any machine with a C
compiler — no dongle, no SDK:

```bash
cd dongle/tests/protocol && make check
```

## Configuration notes

`CONFIG_DONGLE_FAKE_LINK` (default `y`) reports both remotes as `CONNECTED`
with plausible RSSI and battery, so the scoreboard's signal and battery
indicators can be exercised before BLE exists. It changes nothing on the wire.
Turn it off once BLE is real.
