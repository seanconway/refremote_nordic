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
| **Remote (Red)** | this repo, *not started* | Worn on the referee's red (left) wristband. |
| **Remote (Green)** | this repo, *not started* | Worn on the referee's green (right) wristband. |

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
PROTOCOL.md              dongle ↔ scoreboard wire protocol v3.0
RADIO_PROTOCOL.md        dongle ↔ remote radio protocol v1.0 — this repo only
PLAN.md                  living status document — completed work, planned work,
                         binding decisions, the validation ladder, version history
dongle/                  USB bridge firmware
  README.md              build, flash, manual test
  src/
    protocol.c/.h        framing, parse, encode. No Zephyr dependencies.
    usb_link.c/.h        the only file that touches the UART API
    engine.c/.h          supervision, ACK routing, indicator relay, TEST modes
    indicator.c/.h       LED stand-in for the remote haptics
  tests/protocol/        host unit tests for PROTOCOL.md §14
  tools/ncsenv.sh        reconstructs the NCS environment for CLI builds
remote/                  wrist remote firmware — not started
```

`PROTOCOL.md` is kept **byte-identical in this repo and in `wrsl-app`**. Edit it here and copy; they were previously kept in step by a filesystem hard link, which does not survive an editor writing a new file, so verify the hashes match after any change.

## Status

The USB half of the dongle is built and working on real hardware against protocol **v2.0**. Protocol **v3.0** is a breaking revision written against the functional specification, and the firmware has not yet been brought up to it. The radio layer is **specified but not implemented** — [`RADIO_PROTOCOL.md`](RADIO_PROTOCOL.md) v1.0 — and the remotes do not exist.

[`PLAN.md`](PLAN.md) carries the current state in detail, the record of completed work, the planned work in order, the decisions that still bind, and the validation ladder with its results log. **Read it before writing code**, particularly §3.4, which lists the ways the radio layer can regress the USB link without touching any USB code.

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
