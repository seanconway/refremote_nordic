# Dongle firmware — USB bridge

Implements the dongle half of [`PROTOCOL.md`](../PROTOCOL.md) **v3.0** over USB CDC-ACM. The radio layer is not implemented yet; the `TEST` modes of §10.2 stand in for real remotes, which is what lets the whole USB interface be validated first.

- **Board:** `raytac_mdbt50q_cx_40_dongle/nrf52840` — Raytac MDBT50Q-CX-40, MDBT50Q-P1M module, nRF52840, USB-C
- **SDK:** nRF Connect SDK **v3.4.0** (LTS; the last release supporting nRF52)

> **Firmware 0.2.0 speaks protocol v3.0** as of 2026-08-11, with the radio compiled out (`CONFIG_DONGLE_RADIO=n`), and it is **flashed and answering on hardware**. `INFO` returns `HELLO 3.0 0.2.0 RR-0000 0`, both sweeps emit the right counts, and supervision fires at 2512 ms measured — [`PLAN.md`](../PLAN.md) §2.7.

> **What to build is [`BUILD_SPEC.md`](BUILD_SPEC.md)** — the implementable contract for v3.0 plus the radio. This README stays the *procedure* document: layout, build, flash, manual test. Sequence and status are [`PLAN.md`](../PLAN.md) §3.1, where this is M2, one programme in six stages ending in an end-to-end demonstration; §2.6 records what stages 0 and 1 delivered.

> **Before trusting this link in a match, work through the validation ladder in [`PLAN.md`](../PLAN.md) §5–§6.** V0 is green, and V1 and V3 are green on the half a terminal can reach. **Two things are still unverified and neither is a formality:** no one has watched the LEDs, so `indicator.c` has never been seen to do anything; and the application has never held the port, so V2 and V4–V6 are untouched. `PLAN.md` §3.10 lists the ways adding the radio layer can regress this link without touching any USB code.

## Layout

| File | Role |
|---|---|
| `src/protocol.c/.h` | Line assembler, parser, encoders. **No Zephyr dependencies** — compiles under host gcc. |
| `src/usb_link.c/.h` | The only file that touches the UART API. Ring buffers, interrupt-driven CDC-ACM, transmit drop counter. |
| `src/engine.c/.h` | Link supervision, acknowledgement routing, indicator and haptic relay, TEST modes. Owns the cooperative workqueue everything else runs on. |
| `src/radio.h` | The seam. One interface, two build-time implementations. No code. |
| `src/radio_null.c` | `CONFIG_DONGLE_RADIO=n`: nothing connected, downlink on the LEDs. **Permanent, not scaffolding.** |
| `src/indicator.c/.h` | Two-LED stand-in for the remote haptics and indicators. |
| `tests/protocol/` | Host unit tests for `PROTOCOL.md` §14. |
| `tools/hostenv.sh` | Puts a **host** compiler on `PATH` for the above. Not interchangeable with `ncsenv.sh`. |

Still to arrive, per [`BUILD_SPEC.md`](BUILD_SPEC.md) §2: `src/radio_ble.c` behind the seam (stage 3), and `../common/` carrying the radio frame codec and the provisioning record — both Zephyr-free, both shared with the remote firmware, both host-tested.

**The protocol owns the CDC-ACM port exclusively.** Console, shell and logging are disabled in `prj.conf`, and a `BUILD_ASSERT` in `usb_link.c` fails the build if a second CDC-ACM instance ever appears. Diagnostics leave the dongle as protocol `LOG` / `ERR` lines instead. See [`PLAN.md`](../PLAN.md) §4.4 for why — and for the consequence it has for radio bring-up, which is a decision to make before starting M3 rather than during it.

## Board facts worth knowing before you debug an LED

The devicetree gives two LED **nodes** and one button:

| | GPIO | Alias |
|---|---|---|
| `led0_d1` | P0.06 | `led0`, `led0-green`, `green-pwm-led` |
| `led1_d2` | P0.08 | `led1`, `led1-red`, `red-pwm-led` |
| `button0` | P1.06 | `sw0`, `mcuboot-button0` |

**Two nodes, one physical LED.** The board carries a single bi-colour package — a green die and a red die in one body, independently drivable, in one place. Reading the two nodes as two separate lamps is the natural mistake and it will have you hunting for a second LED that does not exist. Both channels are active-low and both are available as PWM.

That is the entire indicator budget standing in for two remotes' worth of haptics and four RGB indicators, which is why `indicator.c` is as coarse as it is and why bench observation is not a substitute for the real surface.

**The consequence that bites during testing:** because both remotes share one body, `HAP BOTH …` lights the same thing whether the routing is right or not. It is the one command that cannot tell correct per-remote addressing from a firmware that drives both channels regardless. Send `HAP RED TAP` and `HAP GREEN TAP` separately and read the colour.

## Build

From the nRF Connect VS Code extension (already configured), or on the CLI:

```bash
source dongle/tools/ncsenv.sh   # reconstructs the NCS v3.4.0 environment
west build -b raytac_mdbt50q_cx_40_dongle/nrf52840 dongle -d dongle/build
```

Output: `dongle/build/dongle/zephyr/zephyr.hex`

**The no-radio baseline is the current default**, and it is kept for the life of the project. Build it explicitly into its own directory when you want both configurations side by side:

```bash
west build -b raytac_mdbt50q_cx_40_dongle/nrf52840 dongle -d dongle/build-noradio \
    -- -DCONFIG_DONGLE_RADIO=n
```

`-DCONFIG_DONGLE_RADIO=y` refuses to configure until `src/radio_ble.c` exists at stage 3, with a message saying so.

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
INFO                                    → HELLO 3.0 0.2.0 RR-0000 0, a LINK line
                                          per remote, then two LOG counter lines
PING                                    → PONG
ECHO hello                              → ECHO hello
ACK 17                                  → tap on the remote that sent seq 17
ACK 17 SILENT                           → the entry clears and NOTHING fires
STATE RED SOLID 00A0FF OFF 000000       → red LED takes a steady level
HAP BOTH LONG                           → one long pulse on both LEDs
CFG BOTH 50 50                          → accepted and remembered; nothing visible
TEST 1                                  → one EVT per button, alternating RED/GREEN
TEST 4                                  → 32 events, 16 per remote
TEST 0                                  → stops test mode, re-enables supervision
```

**The supervision timeout will fire during manual idling — that is correct behaviour** (`PROTOCOL.md` §8). Type `PING` to hold it open, or `TEST 3` to suspend supervision for bench work. At v3.0 `TEST 3` is the *only* way to keep a bench terminal quiet, because the dongle-side clock that used to gate the timeout is gone — so the standing trap of leaving it on gets reached for more often, not less.

### What to look for

- `INFO` reports `conn=none-noradio` in its second counter line. A build with a radio says which interval it is using instead, so any latency figure recorded from a session is attributable to a known configuration.
- `STATE` is idempotent: sending the same line twice must change nothing and must not re-trigger anything. There is no dongle-side cache — the frame goes out both times, and that is deliberate.
- Leave a terminal idle: within ~2.5 s the dongle emits `ERR APP_TIMEOUT`. The next line recovers it with no reboot.
- `TEST 1` emits exactly seven events with contiguous `seq`.
- **`TEST 4` emits 32 events, not 42.** `HOLD_REP` appears only for `FORWARD` and `BACKWARD`.
- An `ACK` for a `seq` older than ~120 ms fires **nothing**, and logs that the budget was spent. A late tap is worse than no tap: the referee reads it as acknowledging their *next* press.

## Host unit tests

`protocol.c` has no Zephyr dependencies, so the parser cases run on any machine with a C compiler — no dongle, no SDK:

```bash
source dongle/tools/hostenv.sh
cd dongle/tests/protocol && make check
```

**131 checks, 0 failures** as of 2026-08-11 — see [`PLAN.md`](../PLAN.md) §5, rung V0.

**`hostenv.sh` is not `ncsenv.sh` and one cannot stand in for the other.** `west build` uses `arm-zephyr-eabi-gcc`, which cross-compiles for the nRF52840 and emits binaries this machine cannot execute; these tests need a compiler targeting the host. Assuming the board toolchain covered both is what left this suite unrun from M0 to M2.

**Nothing runs this for you.** It is not wired into `west build` and there is no CI, so it is a remembered step. Run it after any change to `protocol.c`.

## Configuration notes

| Symbol | Default | Meaning |
|---|---|---|
| `CONFIG_DONGLE_RADIO` | `n` | `y` selects `src/radio_ble.c`, which does not exist until stage 3 and which the build refuses until it does. `n` selects `src/radio_null.c`. |
| `CONFIG_DONGLE_CONN_INTERVAL_UNITS` | `6` | 7.5 ms. Rung 3 of `RADIO_PROTOCOL.md` §12.2. Feeds the power budget and therefore the battery sizing. |
| `CONFIG_DONGLE_SET_SERIAL_FALLBACK` | `"RR-0000"` | The `<set>` field of `HELLO` until the provisioning record is read at stage 2. Deliberately not a plausible serial. |

**The `n` configuration is retained permanently.** It is the no-radio baseline every later radio regression is attributed against — [`PLAN.md`](../PLAN.md) §3.10 lists eight ways a radio degrades a working USB link without touching any USB code, and each is diagnosed by asking whether it still happens with the radio compiled out.

> **`CONFIG_DONGLE_FAKE_LINK` is gone**, deleted rather than defaulted off — [`BUILD_SPEC.md`](BUILD_SPEC.md) §3.1 and [`PLAN.md`](../PLAN.md) §4.11. It reported both remotes `CONNECTED` with a fixed RSSI and battery, fabricating exactly the values a link test measures, and doing so plausibly. A default is no protection when the failure mode is forgetting, because a forgotten `y` produces a passing test. `radio_null` reports `DISCONNECTED`, which is true.
