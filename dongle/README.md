# Dongle firmware — USB bridge

Implements the dongle half of [`PROTOCOL.md`](../PROTOCOL.md) **v4.0** over USB CDC-ACM. The radio layer is not implemented yet; the `TEST` modes of §10.2 stand in for real remotes, which is what lets the whole USB interface be validated first.

- **Board:** `raytac_mdbt50q_cx_40_dongle/nrf52840` — Raytac MDBT50Q-CX-40, MDBT50Q-P1M module, nRF52840, USB-C
- **SDK:** nRF Connect SDK **v3.4.0** (LTS; the last release supporting nRF52)

> **Firmware 0.2.0 speaks protocol v3.0** as of 2026-08-11, with the radio compiled out (`CONFIG_DONGLE_RADIO=n`), and it is **flashed and answering on hardware**. `INFO` returns `HELLO 3.0 0.2.0 RR-0000 0`, both sweeps emit the right counts, and supervision fires at 2512 ms measured — [`HISTORY.md`](../HISTORY.md) §2.7.

> **What to build is [`BUILD_SPEC.md`](BUILD_SPEC.md)** — the implementable contract for v3.0 plus the radio. This README stays the *procedure* document: layout, build, flash, manual test. Sequence and status are [`PLAN.md`](../PLAN.md) §2, the work queue, ending in an end-to-end demonstration; [`HISTORY.md`](../HISTORY.md) §2.6 records the early wire-layer work.

> **The no-radio baseline (V0–V6) is closed.** The application has held the port repeatedly, the handshake, the reverse path, supervision/disconnection, and reconnect all check out on hardware. A few rows are carried forward rather than chased — burst suppression, the remote-render half of supervision, a scripted 10× reconnect, and a dongle-swap test — each needs either hardware that doesn't exist yet or scripting rather than manual observation; see `PLAN.md` §3 (parked items) and `HISTORY.md` §9.2. `PLAN.md` §5.4 lists the ways adding the radio layer can regress this link without touching any USB code.

> **Provisioning redesigned 2026-08-12** — `BUILD_SPEC.md` §9, `PLAN.md` §4.13. The original flash-partition design (`storage_partition`, written over DFU as a separate step) turned out to be unreachable on this bootloader — Serial DFU always activates into the application slot regardless of the address a hex file claims, and this board has no SWD probe on the bench to write `storage_partition` directly. Identity is now baked into the firmware image at build time instead: `tools/provision.py` generates `provisioning_data.h`, and `west build` refuses to configure without `-DCONFIG_PROVISIONING_HEADER_DIR=<dir>` pointing at one. **Not yet confirmed on hardware** under this mechanism.

## Layout

| File | Role |
|---|---|
| `src/protocol.c/.h` | Line assembler, parser, encoders. **No Zephyr dependencies** — compiles under host gcc. |
| `src/usb_link.c/.h` | The only file that touches the UART API. Ring buffers, interrupt-driven CDC-ACM, transmit drop counter. |
| `src/engine.c/.h` | Link supervision, acknowledgement routing, indicator and haptic relay, TEST modes. Owns the cooperative workqueue everything else runs on. |
| `src/radio.h` | The seam. One interface, two build-time implementations. No code. |
| `src/radio_null.c` | `CONFIG_DONGLE_RADIO=n`: nothing connected, downlink on the LEDs. **Permanent, not scaffolding.** |
| `src/indicator.c/.h` | LED stand-in for the remote haptics and indicators. **One blue lamp for both remotes** — see [`BOARD.md`](BOARD.md) §2. |
| `../common/provisioning.h/.c` | The provisioning record — struct, `provisioning_validate()`. **No Zephyr dependencies**, host-tested, shared unchanged with the remote firmware. Baked into the image at build time, not read from flash — §9, `PLAN.md` §4.13. |
| `tests/protocol/` | Host unit tests for `PROTOCOL.md` §14. |
| `tests/provisioning/` | Host unit tests for `../common/provisioning.c` — 9 checks. |
| `tools/provision.py` | Bench tool: generates a set's three `provisioning_data.h` headers (dongle, RED, GREEN) as generated C, plus a manifest. No dependencies. |
| `tools/hostenv.sh` | Puts a **host** compiler on `PATH` for the above. Not interchangeable with `ncsenv.sh`. |
| [`BOARD.md`](BOARD.md) | **Hardware reference** — LEDs, button, flash map, the REGOUT0 reset. Facts not derivable from the firmware. |

Still to arrive, per [`BUILD_SPEC.md`](BUILD_SPEC.md) §2: `src/radio_ble.c` behind the seam, and `../common/rframe.c` for the radio frame codec — both stage 3, both Zephyr-free, both shared with the remote firmware, both host-tested.

**The protocol owns the CDC-ACM port exclusively.** Console, shell and logging are disabled in `prj.conf`, and a `BUILD_ASSERT` in `usb_link.c` fails the build if a second CDC-ACM instance ever appears. Diagnostics leave the dongle as protocol `LOG` / `ERR` lines instead. See [`PLAN.md`](../PLAN.md) §4.4 for why — and for the consequence it has for radio bring-up, which is a decision to make before starting M3 rather than during it.

## Board facts worth knowing before you debug an LED

**The hardware reference is [`BOARD.md`](BOARD.md)**, cited to the in-tree board files. The short version, because it is the thing most likely to waste a session:

| | GPIO | Alias | Actually — **measured 2026-08-11** |
|---|---|---|---|
| `led0_d1` | P0.06 | `led0`, `led0-green`, `green-pwm-led` | **blue — the one lamp on the board** |
| `led1_d2` | P0.08 | `led1`, `led1-red`, `red-pwm-led` | **not fitted; nothing visible** |
| `button0` | P1.06 | `sw0`, `mcuboot-button0` | |

**Both LEDs are blue, only one is fitted, and it is not the one the documentation says.** Raytac's pin table gives `LED0 (blue) = P0.8` and `LED1 (blue) = P0.6 (No pasted components by default)` — the two pins are transposed. `HAP GREEN LONG` blinks and `HAP RED LONG` does not, which puts the fitted part on P0.06. The `-green` and `-red` alias spellings are copied from the **Nordic** nRF52840 Dongle, which has a real RGB part; this board does not, and reasoning from those names is how this README twice claimed hardware that is not there. **Alias names are not evidence; a measurement is.**

So the entire indicator budget is **one blue lamp on the `GREEN` channel**, standing in for two remotes' worth of haptics and four RGB indicators. That is why `indicator.c` is as coarse as it is, and why bench observation is not a substitute for the real surface.

**What that one lamp can and cannot tell you:**

- ✅ **Per-remote routing — and only because the other LED is missing.** `HAP RED` dark and `HAP GREEN` lit is a firmware that honours the target; one driving both channels would light the same lamp for both.
- ❌ **Anything addressed to `RED`** — `HAP RED`, `STATE RED`, and `indicator_error()`, which borrows `RED`. **No `ERR` produces any visible signal on this board**, `APP_TIMEOUT` included. *"No blink" never means "no error"* — read the wire.
- ❌ **`CFG`.** `indicator.c` drives GPIO, not PWM, so there is no amplitude or brightness for a scale factor to act on.

[`BOARD.md`](BOARD.md) §2 has the evidence and §2.2 explains why fault indication is deliberately left invisible.

## Build

From the nRF Connect VS Code extension (already configured), or on the CLI:

**Every build needs a provisioning header** (§9, `PLAN.md` §4.13) — generate one first:

```bash
python dongle/tools/provision.py RR-0001 -o dongle/tools/out/
```

Then build, pointing at the role you need:

```bash
source dongle/tools/ncsenv.sh   # reconstructs the NCS v3.4.0 environment
west build -b raytac_mdbt50q_cx_40_dongle/nrf52840 dongle -d dongle/build \
    -- -DCONFIG_PROVISIONING_HEADER_DIR="<abs path>/dongle/tools/out/RR-0001_dongle"
```

`CONFIG_PROVISIONING_HEADER_DIR` must be quoted — it's a Kconfig string, not a plain CMake variable, for the reason in `BUILD_SPEC.md` §9. `west build` refuses to configure at all without it.

Output: `dongle/build/dongle/zephyr/zephyr.hex`

**On PowerShell**, `../tools/ncsenv.ps1` (repo root, not here — it serves `remote/` too) is the equivalent of `ncsenv.sh` above: `. tools\ncsenv.ps1` from the repo root. For programming a whole set at once — provisioning, all three firmwares, the dongle's DFU zip — `../tools/build_set.ps1 RR-0001` does the entire sequence in one command; see its own header comment for what it does and deliberately does not do (it stops short of the actual flash, which needs a physical bootloader-entry press per device).

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
STATE GREEN SOLID 00FF00 OFF 000000     → the lamp takes a steady level and holds
                                          it (blue; the RGB in the line is not
                                          rendered). Address GREEN, not RED
HAP GREEN LONG                          → one 500 ms pulse
HAP RED LONG                            → nothing — RED is the unfitted pin, and
                                          that silence is the routing proof
CFG BOTH 50 50                          → accepted and remembered; nothing visible
                                          here and nothing visible ever — GPIO has
                                          no amplitude to scale
TEST 1                                  → one EVT per button, alternating RED/GREEN
TEST 4                                  → 32 events, 16 per remote
TEST 0                                  → stops test mode, re-enables supervision
```

**The supervision timeout will fire during manual idling — that is correct behaviour** (`PROTOCOL.md` §8). Type `PING` to hold it open, or `TEST 3` to suspend supervision for bench work. At v4.0 `TEST 3` is the *only* way to keep a bench terminal quiet, because the dongle-side clock that used to gate the timeout is gone — so the standing trap of leaving it on gets reached for more often, not less.

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

**The `n` configuration is retained permanently.** It is the no-radio baseline every later radio regression is attributed against — [`PLAN.md`](../PLAN.md) §5.4 lists eight ways a radio degrades a working USB link without touching any USB code, and each is diagnosed by asking whether it still happens with the radio compiled out.

> **`CONFIG_DONGLE_FAKE_LINK` is gone**, deleted rather than defaulted off — [`BUILD_SPEC.md`](BUILD_SPEC.md) §3.1 and [`PLAN.md`](../PLAN.md) §4.11. It reported both remotes `CONNECTED` with a fixed RSSI and battery, fabricating exactly the values a link test measures, and doing so plausibly. A default is no protection when the failure mode is forgetting, because a forgotten `y` produces a passing test. `radio_null` reports `DISCONNECTED`, which is true.
