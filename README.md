# RefRemote — firmware

Firmware for a **wireless single-operator officiating system**: a pair of wrist-worn referee remotes and a USB dongle that bridges them to a browser-based scoreboard.

A conventional grappling mat needs a referee, a scoreboard operator, and often a towel tapper to signal the referee when time expires. This system lets **one referee run a complete match** — clock, score, and the one or two ruleset-specific states that matter — with no table support. Expiry is felt on the wrist, which replaces the towel tapper directly.

## Authoritative documents

Two documents define the project. **They are the authority on direction; everything else in this repo is an implementation of them, and where any other document disagrees, they win.**

| Document | Covers |
|---|---|
| [`SCOPE.md`](SCOPE.md) | Problem, market, rulesets in and out of scope, deployment envelope, operating assumptions, design principles, external officiating responsibilities, release scope, project risks |
| [`SYSTEM_FUNC_SPEC.md`](SYSTEM_FUNC_SPEC.md) | How the system behaves: division of responsibility, physical interface, input model, button functions, the secondary athlete clock, communication-layer and application requirements, indicators, haptics, ruleset configuration |

The two protocols answer to them, and to each other: [`PROTOCOL.md`](PROTOCOL.md) is the dongle ↔ scoreboard wire contract, [`RADIO_PROTOCOL.md`](RADIO_PROTOCOL.md) is the dongle ↔ remote radio contract, and `PROTOCOL.md` §12 — *what this link assumes of the radio* — is the acceptance criteria of the latter.

Read `SCOPE.md` §7 before proposing a design change. Those five principles — operable by feel, the referee owns rule application, pure front-end and offline, ruleset logic in one place, simplicity over coverage — decide most arguments before they start.

## System composition

| Node | Repo | Role |
|---|---|---|
| **Scoreboard** | [`wrsl-app`](https://github.com/seanconway/wrsl-app) | Browser application, served once and run offline. Owns all ruleset logic, all match state, the clocks of record, and the match event log. |
| **Dongle** | this repo, `dongle/` | USB-C bridge between the remote pair and the host. Isolates wireless performance from host hardware variability. |
| **Remote (Red)** | this repo, `remote/` | Worn on the referee's red (left) wristband. Firmware code-complete, confirmed connecting on a DK. |
| **Remote (Green)** | this repo, `remote/` | Worn on the referee's green (right) wristband. Same firmware as Red; not yet built/flashed as a separate unit. |

A dongle plus its two remotes constitute an **officiating set** — the unit of pairing, labelling, deployment and field replacement. Sets are paired in firmware and labelled by serial number; there is no field pairing procedure, and the supported remedy for a hardware failure mid-match is substituting a complete spare set.

## The one architectural fact that explains the rest

**The remote is stateless with respect to the match.** It reports which button was pressed, on which wrist, with what gesture — and renders whatever LED and haptic output it is told to render. It holds no score, no clock, no ownership, and no ruleset knowledge. The only state it holds is device-local: its current LED render, its own link status, and its own battery charge.

Everything follows from that:

- A ruleset change is a **scoreboard change only**. No firmware update, no interface change, no protocol change.
- The per-second heartbeat during riding time is **commanded per beat by the scoreboard**, not generated locally. A remote beating on its own stale ownership state would report one athlete on the wrist while the scoreboard reported another — quietly, with nothing to correct it.
- A failed remote can be swapped mid-match with **no state re-entry**, because the substituted remote has nothing of its own to be stale and the correct state is one node away.
- The wire protocol carries **buttons and waveforms, never officiating concepts**. See [`PROTOCOL.md`](PROTOCOL.md) §R2.

## Hardware

| | |
|---|---|
| **Dongle** | Raytac **MDBT50Q-CX-40** — nRF52840, MDBT50Q-P1M module, PCB trace antenna, USB-C. Board target `raytac_mdbt50q_cx_40_dongle/nrf52840`. |
| **Remotes** | nRF52840, seven buttons, four RGB indicators, one ERM plus haptic driver IC, USB-C charging. Not yet built. |
| **SDK** | nRF Connect SDK **v3.4.0**, pinned. |

**The board target is not interchangeable with `nrf52840dongle/nrf52840`.** LED and button mapping differ, and so does bootloader entry — **hold the button while plugging the dongle in**, rather than pressing RESET.

**On the SDK pin:** v3.4.0 is the LTS release, with five years of security and critical fixes, and the **last release supporting the nRF52 Series**, which it declares feature complete. Nordic recommends it for new nRF52840 designs. Older advice recommending v3.1.x for this dongle predates that announcement. Do not downgrade, and expect no newer SDK to support this chip.

## Layout

```
SCOPE.md                 project scope — authoritative
SYSTEM_FUNC_SPEC.md      functional specification — authoritative
PROTOCOL.md              dongle ↔ scoreboard wire protocol v4.0
RADIO_PROTOCOL.md        dongle ↔ remote radio protocol v2.0 — this repo only
PLAN.md                  living forward-looking document — current state, the work
                         queue, parked items, binding decisions, the validation reference
HISTORY.md                append-only record — completed work, results log, project history
tools/
  ncsenv.ps1             PowerShell translation of dongle/tools/ncsenv.sh
  build_set.ps1          one-shot per-unit step: generate provisioning, build
                         dongle + both remotes, package the dongle DFU zip
dongle/                  USB bridge firmware
  README.md              build, flash, manual test
  src/
    protocol.c/.h        framing, parse, encode. No Zephyr dependencies.
    usb_link.c/.h        the only file that touches the UART API
    engine.c/.h          supervision, ACK routing, indicator relay, TEST modes
    indicator.c/.h       LED stand-in for the remote haptics
  tests/protocol/        host unit tests for PROTOCOL.md §14
  tools/
    ncsenv.sh            reconstructs the NCS environment for CLI builds (bash)
    provision.py         bench tool: generates a set's provisioning_data.h headers
remote/                  wrist remote firmware — code-complete, confirmed connecting on a DK (RED)
```

`PROTOCOL.md` is kept **byte-identical in this repo and in `wrsl-app`**. Edit it here and copy; they were previously kept in step by a filesystem hard link, which does not survive an editor writing a new file, so verify the hashes match after any change.

## Status

The dongle firmware is at protocol **v4.0**, flashed and answering on hardware. The radio layer — [`RADIO_PROTOCOL.md`](RADIO_PROTOCOL.md) v2.0 — is implemented and, as of 2026-08-13, **confirmed on hardware for the first time**: a DK remote (RED) connects to the dongle over BLE, encrypted, and the scoreboard app shows it connected with live telemetry. GREEN and the negative-case conformance tests (A12–A14, A19) are still open, and one of them (A13) needs a small firmware fix before it can even be run. Custom remote hardware (the table above) is not yet designed — the DK is the prototyping platform in use until M6.

[`PLAN.md`](PLAN.md) carries the current state in detail, the work queue in order, the decisions that still bind, and the validation reference; [`HISTORY.md`](HISTORY.md) carries the record of completed work and the results log. **Read `PLAN.md` before writing code**, particularly §5.4, which lists the ways the radio layer can regress the USB link without touching any USB code.

## Build and flash

See [`dongle/README.md`](dongle/README.md). In short:

```bash
source dongle/tools/ncsenv.sh
west build -b raytac_mdbt50q_cx_40_dongle/nrf52840 dongle -d dongle/build
```

Host parser tests need no SDK and no hardware — `protocol.c` has no Zephyr dependencies precisely so they can run anywhere:

```bash
cd dongle/tests/protocol && make check
```

## License

Not yet determined.
