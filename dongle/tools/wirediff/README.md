# Wire-log diff — firmware against the emulator

`PLAN.md` §3.2 calls `wrsl-app/src/emulator/dongleModel.js` the executable reference the firmware has to match. This turns that claim into a check. It is a **stage 1 exit criterion** (§5) and it is wanted again every time the firmware changes underneath the wire layer — §3.10 lists eight ways the radio can regress this link without touching any USB code, and a clean diff against the pre-radio trace is how each becomes attributable.

First run: 2026-08-11, firmware 0.2.0. **All 39 `EVT` lines identical.** The seven remaining differences are enumerated in `PLAN.md` §2.7.

## Running it

The COM port is exclusive — close the scoreboard's connection first.

```powershell
$T = "$PWD\dongle\tools\wirediff"
$steps = Get-Content "$T\script.txt" | Where-Object { $_ -ne '' }
& "$T\wire.ps1" -Port COM13 -Steps $steps > fw.raw
node "$T\emu.mjs" "$T\script.txt" > emu.raw

node "$T\norm.mjs" fw.raw  fw  > fw.n
node "$T\norm.mjs" emu.raw emu > emu.n
diff -u fw.n emu.n
```

Takes about 20 s a side; most of it is the two `TEST` sweeps running in real time on both.

## What the pieces do

| File | Role |
|---|---|
| `script.txt` | The canonical stimulus, one `line\|dwell_ms` per step. **Both ends get this and nothing else** — the diff is only worth anything if the input is identical |
| `wire.ps1` | Scripted bench terminal over `System.IO.Ports`. Timestamps every line, so timings are recorded rather than eyeballed |
| `emu.mjs` | Drives `dongleModel.js` under Node with the same script, real timers |
| `norm.mjs` | Puts both traces in one shape and rebases `seq` |
| `ack.ps1` | Separate: times an `ACK` against the 120 ms budget. Not part of the diff |

## Three things that will waste your time otherwise

- **`wire.ps1` leaves DTR low deliberately.** The dongle must never gate transmission on it (`CLAUDE.md` §6), so driving it with DTR low is the stronger test. If you "fix" this by asserting DTR you delete the check.
- **`norm.mjs` rebases `seq` on the first `EVT`.** The firmware has been running since it was flashed and is well past zero; the model starts fresh every run. An absolute comparison reports dozens of differences that mean nothing. Both sequences are contiguous, so a rebased comparison still catches a real gap, duplicate or wrap error.
- **The first lines a freshly-opened port delivers may be stale.** The boot `HELLO` and the boot-time `ERR APP_TIMEOUT` sit in the Windows driver buffer until something opens the handle, and then arrive at once looking current. `wire.ps1` drains for 300 ms and prints `--- drained ---` before attributing anything to a command.

## Differences that are expected and are not defects

Under `CONFIG_DONGLE_RADIO=n` the emulator mocks two connected remotes and the firmware truthfully reports none, so `LINK` lines differ — and because `link_reemit_handler` re-emits only `CONNECTED` remotes, the firmware's 10 s re-emit is absent too. The second follows from the first; do not chase it separately.

`ERR APP_TIMEOUT` can land one position either side of an `EVT`. That is a race between the 2500 ms supervision timer and the test timer, not a difference in behaviour.
