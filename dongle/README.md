# Dongle firmware — USB bridge

Implements the dongle half of [`PROTOCOL.md`](../PROTOCOL.md) over USB CDC-ACM. The radio layer is not implemented yet; the `TEST` modes of §10.2 stand in for real remotes, which is what lets the whole USB interface be validated first.

- **Board:** `raytac_mdbt50q_cx_40_dongle/nrf52840` — Raytac MDBT50Q-CX-40, MDBT50Q-P1M module, nRF52840, USB-C
- **SDK:** nRF Connect SDK **v3.4.0** (LTS; the last release supporting nRF52)

> **This firmware currently implements protocol v2.0.** `PROTOCOL.md` is at **v3.0**, a breaking revision written against `SYSTEM_FUNC_SPEC.md`. Bringing this firmware up to it is milestone M2 in [`PLAN.md`](../PLAN.md) §3.1. A v2.0 dongle and a v3.0 app will refuse each other at the `HELLO` version check, which is the intended behaviour.

> **Before trusting this link in a match, work through the validation ladder in [`PLAN.md`](../PLAN.md) §5–§6.** The interface is functionally working but not validated: the host parser tests have never been executed, and the supervision, reconnect and soak rungs are untouched. `PLAN.md` §3.4 lists the ways adding the radio layer can regress this link without touching any USB code.

## Layout

| File | Role |
|---|---|
| `src/protocol.c/.h` | Line assembler, parser, encoders. **No Zephyr dependencies** — compiles under host gcc. |
| `src/usb_link.c/.h` | The only file that touches the UART API. Ring buffers, interrupt-driven CDC-ACM. |
| `src/engine.c/.h` | Link supervision, acknowledgement routing, TEST modes. |
| `src/indicator.c/.h` | LED stand-in for the remote haptics. |
| `tests/protocol/` | Host unit tests for `PROTOCOL.md` §14. |

**The protocol owns the CDC-ACM port exclusively.** Console, shell and logging are disabled in `prj.conf`, and a `BUILD_ASSERT` in `usb_link.c` fails the build if a second CDC-ACM instance ever appears. Diagnostics leave the dongle as protocol `LOG` / `ERR` lines instead. See [`PLAN.md`](../PLAN.md) §4.4 for why — and for the consequence it has for radio bring-up, which is a decision to make before starting M3 rather than during it.

## Board facts worth knowing before you debug an LED

The board devicetree gives two LEDs and one button:

| | GPIO | Alias |
|---|---|---|
| `led0_d1` | P0.06 | `led0`, `led0-green`, `green-pwm-led` |
| `led1_d2` | P0.08 | `led1`, `led1-red`, `red-pwm-led` |
| `button0` | P1.06 | `sw0`, `mcuboot-button0` |

Both LEDs are active-low and both are also available as PWM channels. That is the entire indicator budget standing in for two remotes' worth of haptics and four RGB indicators, which is why `indicator.c` is as coarse as it is and why bench observation is not a substitute for the real surface.

## Build

From the nRF Connect VS Code extension (already configured), or on the CLI:

```bash
source tools/ncsenv.sh        # reconstructs the NCS v3.4.0 environment
cd <repo root>
west build -b raytac_mdbt50q_cx_40_dongle/nrf52840 dongle -d dongle/build
```

Output: `dongle/build/dongle/zephyr/zephyr.hex`

## Flash

**Enter the bootloader first: hold the button while plugging the dongle in.** The button is on the far side from the USB connector and pushes sideways, toward the connector. The LED fades when the bootloader is running.

This is *not* the same as the Nordic nRF52840 Dongle, which enters its bootloader by pressing RESET. The board targets are not interchangeable.

Then either:

- **nRF Connect Programmer** — select `zephyr.hex` and write. (Known-good on this setup.)
- **nrfutil**:
  ```bash
  nrfutil nrf5sdk-tools pkg generate --hw-version 52 --sd-req=0x00 \
      --application dongle/build/dongle/zephyr/zephyr.hex \
      --application-version 1 dongle.zip
  nrfutil nrf5sdk-tools dfu usb-serial -pkg dongle.zip -p COMx
  ```

## Manual test

Open a terminal on the dongle's COM port at 115200 8-N-1 and type. No browser, no scoreboard, no harness — this is the fastest way to tell whether a bug is in firmware or in the app.

```
INFO                                    → HELLO, then a LINK line per remote
PING                                    → PONG
ECHO hello                              → ECHO hello
STATE RED SOLID 00A0FF OFF 000000       → indicator stand-in follows
HAP BOTH LONG                           → one long pulse
CFG BOTH 50 50                          → half-scale haptics and LEDs
TEST 1                                  → one EVT per button, alternating RED/GREEN
TEST 4                                  → every gesture on every button
TEST 0                                  → stops test mode, re-enables supervision
```

**The supervision timeout will fire during manual idling — that is correct behaviour** (`PROTOCOL.md` §8). Type `PING` to hold it open, or `TEST 3` to suspend supervision for bench work.

### What to look for

- `STATE` is idempotent: sending the same line twice must change nothing and must not re-trigger anything.
- Leave a terminal idle: within the supervision window the dongle emits `ERR APP_TIMEOUT`. The next line from the app recovers it with no reboot.
- `TEST 1` emits exactly seven events with contiguous `seq`.
- `TEST 4` emits `HOLD_REP` only for `FORWARD` and `BACKWARD`.

## Host unit tests

`protocol.c` has no Zephyr dependencies, so the parser cases run on any machine with a C compiler — no dongle, no SDK:

```bash
cd dongle/tests/protocol && make check
```

**These have never been executed.** See [`PLAN.md`](../PLAN.md) §5, rung V0 — it is the highest-value outstanding item in the project and needs nothing but a compiler.

## Configuration notes

`CONFIG_DONGLE_FAKE_LINK` (default `y`) reports both remotes as `CONNECTED` with plausible RSSI and battery, so the scoreboard's signal and battery indicators can be exercised before the radio exists. It changes nothing on the wire. **Turn it off the moment the radio is real** — left on, the dongle reports remotes as connected that are not.
