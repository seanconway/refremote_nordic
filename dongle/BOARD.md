# Board reference — Raytac MDBT50Q-CX-40 Dongle

Facts about the dongle hardware that are **not** derivable from the firmware, gathered so nobody re-derives them from alias names and gets them wrong — which has already happened once here.

**Board target:** `raytac_mdbt50q_cx_40_dongle/nrf52840`
**In-tree at:** `$NCS/zephyr/boards/raytac/mdbt50q_cx_40_dongle/` (NCS v3.4.0)

Everything below is cited to a file in that directory. Where the board's own documentation and its devicetree disagree, both readings are given, because the disagreement is the trap.

## 1. Identity

| | |
|---|---|
| Module | Raytac MDBT50Q-P1M |
| SoC | nRF52840 (`nrf52840_qiaa`), Cortex-M4F |
| Memory | 1 MB flash, 256 kB RAM |
| Clocks | **Both external.** 32 MHz main, 32.768 kHz slow |
| USB | Type C, native nRF52840 USB device |
| Bootloader | Nordic nRF5 "Open bootloader", factory-programmed |
| Radio | BT 5.4/5.2/5.1/5 certified; supports Coded PHY (long range); 802.15.4 |

The external 32.768 kHz crystal matters for the radio: it is what makes a tight connection interval schedulable without the drift budget a synthesised LFCLK would impose. `RADIO_PROTOCOL.md` §12 assumes it.

## 2. LEDs — read this before debugging one

> **`$NCS/zephyr/boards/raytac/mdbt50q_cx_40_dongle/doc/index.rst`, lines 41–42, verbatim:**
> ```
> * LED0 ( blue ) = P0.8
> * LED1 ( blue ) = P0.6 (No pasted components by default)
> ```

Three facts, all of them contradicting what the devicetree aliases imply:

1. **Both LEDs are blue.** There is no red die and no green die. Nothing on this board renders colour.
2. **Only one is fitted.** "No pasted components by default" means no solder paste, means the part is absent from the board.
3. **The fitted one is on P0.06** — *not* P0.08 as the quoted table says. See the correction immediately below.

### The pin numbers in that table are the wrong way round

**Measured on this unit, 2026-08-11** (§2.1): `HAP GREEN LONG` — which drives alias `led0` → `led0_d1` → **P0.06** — produces a half-second blink. `HAP RED LONG` — alias `led1` → `led1_d2` → **P0.08** — produces nothing at all.

So the fitted lamp is on **P0.06**, and P0.08 is the empty footprint. That is the exact opposite of the doc's table.

**The measurement wins.** It is a direct observation of this hardware through a known code path, and the surrounding documentation is already demonstrably unreliable on LEDs — see the inherited aliases below and the red/green prose at the end of this section. Whether Raytac's table is simply transposed or whether production runs vary is unknown and does not matter here: **on the unit in hand, the lamp is on P0.06.**

### The alias names are inherited and wrong

The devicetree (`…_nrf52840.dts`, lines 60–74) carries these:

| Alias | Node | Pin | What the name claims | What is there |
|---|---|---|---|---|
| `led0`, `led0-green`, `green-pwm-led`, `mcuboot-led0` | `led0_d1` | **P0.06** | a green LED | **the one blue LED** — measured |
| `led1`, `led1-red`, `red-pwm-led` | `led1_d2` | **P0.08** | a red LED | **nothing visible** — measured |

`led0-green` / `led1-red` / `red-pwm-led` / `green-pwm-led` are copied from the **Nordic** nRF52840 Dongle board files, where they describe a genuine RGB part. On this board they describe nothing. They are not a bug to fix — samples depend on them — but they are not evidence about the hardware, and reading them as evidence is exactly the mistake made here on 2026-08-11.

### The numbering is inverted between the two sources

The board doc's `LED0` is P0.8; the devicetree's `led0` is P0.06. **They are opposite LEDs.** A sentence about "LED0" means different hardware depending on which document is open. This reference always names the **pin**.

| | Board doc calls it | Devicetree calls it | Silkscreen |
|---|---|---|---|
| P0.08 | `LED0` | `led1_d2` / alias `led1` | D2 |
| P0.06 | `LED1` | `led0_d1` / alias `led0` | D1 |

### Both are active-low, both are on PWM

`GPIO_ACTIVE_LOW` on both nodes, so `gpio_pin_set_dt(spec, 1)` lights it and the polarity is handled for you — do not invert by hand. `pwm0` channel 0 → P0.06, channel 1 → P0.08, `nordic,invert` set (`…-pinctrl.dtsi`, lines 31–37).

### The doc's own prose contradicts its own pin table

`doc/index.rst` line 86 says "The **red** LED should start a fade pattern" for the bootloader, and line 114 says "observe the **green** LED blinking". There is no red LED and no green LED on this board; both sentences are copied from the Nordic dongle documentation along with the aliases. **The pin table at lines 41–42 is the part that describes this hardware.** Treat the prose as unreliable.

### What this costs the bench

The dongle's entire indicator budget is **one blue lamp**, standing in for two remotes' worth of haptics and four RGB indicators. That is the reason `indicator.c` is as coarse as it is, and the reason bench observation cannot substitute for the real surface. Specifically, it cannot show:

- **Anything at all addressed to `RED`.** That channel drives the empty footprint. `HAP RED`, `STATE RED` and `indicator_error()` are all silent on this board — see §2.2.
- **Colour**, as `STATE` means it. `STATE` carries an RGB triple per indicator; what is here is one bit, and it is blue whatever was asked for.
- **Amplitude**, which is the one thing `PROTOCOL.md` §9.1 makes a requirement — `BEAT` must be "unmistakably weaker than `TAP`". A GPIO LED has no amplitude. `indicator.c` renders `BEAT` as a *shorter* flash, which is a different distinction wearing the same name.

It **can** show routing, which was not expected — §2.1.

### 2.1 Resolved: the lamp is on P0.06, and routing is proven — 2026-08-11

Run on the bench terminal, with `TEST 3` first so supervision could not contribute a blink:

| Sent | Drives | Observed |
|---|---|---|
| `TEST 3` | — | nothing |
| `HAP RED LONG` | `led1` → **P0.08** | **nothing** |
| `HAP GREEN LONG` | `led0` → **P0.06** | **~500 ms blink** |
| `TEST 0` | — | nothing |

Two conclusions, and the second is the valuable one.

**The fitted lamp is on P0.06.** Raytac's pin table has the two pins transposed; the measurement stands against it (§2).

**Per-remote haptic routing is correct.** This had been written off as unobservable here, and the asymmetry is what makes it observable — *because* only one lamp is fitted:

| | If routing is correct | If the firmware ignores the target and drives both channels | Observed |
|---|---|---|---|
| `HAP RED LONG` | dark | **blink** | dark |
| `HAP GREEN LONG` | blink | blink | blink |

A firmware that drove both channels unconditionally — the exact failure `HAP BOTH` could never rule out — would have blinked on **both** commands. It blinked on one, and the correct one. `indicator.c` honours the target.

**What is still not proven:** that `HAP RED` reaches P0.08. A correctly-routed command to an absent LED and a command that does nothing at all are indistinguishable here. The code path is symmetric and the negative result is exactly what correct routing predicts, so this is a gap in the *evidence*, not a suspicion about the code. It closes at stage 4 on the DK, which has four separate LEDs.

**This also retires the confound that prompted the test.** The earlier observation — `HAP BOTH TAP` and `HAP GREEN TAP` both blinking — was the haptic after all, since both commands include `GREEN`. It could not have been `indicator_error()`, because that borrows `RED`, which is the pin nothing is on.

### 2.2 Every fault indication on this board is invisible

`indicator_error()` drives `leds[PROTO_REMOTE_RED]` → P0.08 → the empty footprint. So **no `ERR` produces any visible signal on the dongle**, including the `APP_TIMEOUT` supervision raises after 2.5 s of silence.

**This is left as it is, deliberately.** Moving fault indication to the fitted channel would make errors visible at the cost of putting them on the same lamp as every `GREEN` haptic — which is precisely the ambiguity §2.1's test was constructed to escape, reintroduced permanently. The trade is not close: **every `ERR` is already reported on the wire as an `ERR` line**, which is a richer channel than a blink and one the app and the bench terminal both read. The LED is redundant for faults and load-bearing for haptics, so the fitted lamp goes to haptics.

The consequence to remember: **on this board, "no blink" never means "no error".** Read the wire.

## 3. Button

| | GPIO | Aliases |
|---|---|---|
| `button0` | **P1.06** | `sw0`, `mcuboot-button0` |

`GPIO_PULL_UP | GPIO_ACTIVE_LOW`, emits `INPUT_KEY_0`. One button, and it is the bootloader entry mechanism as well — see §6.

## 4. Flash map, and the partition-address trap

From `fstab-stock.dtsi` — the default; `fstab-debugger.dtsi` is the variant for an external SWD probe and is **not** what this project builds against.

| Partition | Address | Size |
|---|---|---|
| `boot_partition` (mcuboot) | `0x00000000` | 64 kB |
| `slot0_partition` (image-0) | `0x00010000` | 408 kB |
| `slot1_partition` (image-1) | `0x00076000` | 408 kB |
| **`storage_partition`** | **`0x000f0000`** | **16 kB** |
| nRF5 bootloader | `0x000f4000` | 40 kB |

> **The `storage_partition` node is named `partition@dc000` and its `reg` is `0x000f0000`.** The two do not agree and the node name is the wrong one. Zephyr binds on `reg`, so the real address is `0xf0000` — but anyone grepping the node name, or reading the DTS at a glance, gets `0xdc000` and lands 80 kB low, inside `slot1_partition`.

`storage_partition` was where the provisioning record of `RADIO_PROTOCOL.md` §10.1 was meant to live (stage 2) — sitting **below** the nRF5 bootloader and **outside** `slot0_partition` so it would survive an ordinary application reflash. **Unreachable in practice on this board**: this bootloader's Serial DFU always activates a received image into `slot0_partition`/`slot1_partition` regardless of what address a hex file claims (see `slot0_partition`/`slot1_partition` above), so nothing sent over DFU ever actually lands here, and this board has no SWD probe on the bench to write it directly. `PLAN.md` §4.13 reverses the design: provisioning identity is now baked into the firmware image at build time instead, and `storage_partition` is unused. Left in the devicetree unchanged — moving or removing it isn't necessary now that nothing reads it.

`FLASH_LOAD_OFFSET` is forced to `0x1000` by `Kconfig.defconfig` so the application links after the Nordic MBR. This is why an image built for this target cannot be flashed at 0 with a debugger without also editing the board's Kconfig.

## 5. The silent reset on first boot after flashing

`board.c` contains `board_early_init_hook()`, and it is worth knowing about before it confuses a bring-up session:

> When powered from USB (high-voltage mode) the nRF52840's GPIO output voltage defaults to **1.8 V, which is not enough to light the LEDs**. The hook detects this, rewrites `UICR->REGOUT0` to 3.0 V, and then calls `NVIC_SystemReset()`, because the change only takes effect across a reset.

Consequences:

- **A freshly flashed board reboots once, by itself, before your `main()` ever runs.** It is not a crash and not a watchdog. It happens once per board, not once per flash — `REGOUT0` is in UICR and persists.
- On a unit that has not yet been through it, **the LEDs do not light at all**, regardless of firmware. An LED debugging session on a virgin board can chase a firmware fault that does not exist.
- UICR is only erased by a full chip erase, so this does not recur.

## 6. Flashing

**Enter the bootloader by holding the button while plugging the board in.** The button is on the far side from the USB connector and pushes *sideways*, toward the connector — not down. The LED fades when the bootloader is running.

This is **not** the Nordic nRF52840 Dongle, which enters its bootloader with RESET. The two board targets are not interchangeable and neither are the procedures.

Then nRF Connect Programmer with `zephyr.hex` (known-good on this setup), or `nrfutil` — the exact commands are in [`README.md`](README.md).

## 7. Sources

All local; none fetched from the web.

| Fact | File, under `$NCS/zephyr/boards/raytac/mdbt50q_cx_40_dongle/` |
|---|---|
| LED colours, population, pin numbering | `doc/index.rst` lines 38–48 |
| LED/button nodes, aliases, polarity | `raytac_mdbt50q_cx_40_dongle_nrf52840.dts` lines 24–74 |
| PWM channel mapping | `raytac_mdbt50q_cx_40_dongle_nrf52840-pinctrl.dtsi` lines 31–45 |
| Flash map, `storage_partition` | `fstab-stock.dtsi` |
| `FLASH_LOAD_OFFSET` | `Kconfig.defconfig` |
| REGOUT0 raise and reset | `board.c` |
| Module, clocks, certifications | `doc/index.rst` lines 6–28 |

Raytac's own product page and hardware spec are linked from `doc/index.rst` lines 285–288 if a question ever needs the schematic — in particular, **whether D1 is fitted on a given production run** (§2.1) is a question only the vendor documentation or a magnifier can answer.
