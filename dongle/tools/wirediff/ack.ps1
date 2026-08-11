# Times an ACK against the 120 ms budget of PROTOCOL.md §11.
#
# The three outcomes are not distinguishable by watching an LED, which is why
# this exists:
#   fresh   (< 100 ms)  -> tap fires, late unchanged
#   late    (100-120)   -> entry found, budget spent, tap WITHHELD, late++
#   swept   (> 120 ms)  -> entry already gone, indistinguishable from unknown
#
# The middle band is the one that matters. MOTOR_SPINUP_MS is 20, so a tap
# ordered at T+105 could not reach the wrist before T+125 and is suppressed
# rather than deferred (CLAUDE.md §4.5).
param(
    [string]$Port = 'COM13',
    [int]$OffsetMs = 105
)

$ErrorActionPreference = 'Stop'

$sp = New-Object System.IO.Ports.SerialPort $Port, 115200, 'None', 8, 'One'
$sp.ReadTimeout = 50
$sp.DtrEnable   = $false
$sp.Open()

$buf   = ''
$lines = New-Object System.Collections.ArrayList
$sw    = [System.Diagnostics.Stopwatch]::StartNew()

# Tight poll, no sleep: the arrival timestamp of the EVT is the reference the
# whole measurement hangs off, so it must not be quantised by a sleep interval.
function Drain {
    $chunk = $sp.ReadExisting()
    if ($chunk.Length -gt 0) {
        $script:buf += $chunk
        while ($script:buf.Contains("`n")) {
            $i = $script:buf.IndexOf("`n")
            [void]$script:lines.Add($script:buf.Substring(0, $i).TrimEnd("`r"))
            $script:buf = $script:buf.Substring($i + 1)
        }
    }
}
function Spin([int]$ms) {
    $end = $sw.Elapsed.TotalMilliseconds + $ms
    while ($sw.Elapsed.TotalMilliseconds -lt $end) { Drain }
}
function LateCount {
    $script:lines.Clear()
    $sp.Write("INFO`n")
    Spin 400
    foreach ($l in $script:lines) {
        if ($l -match 'late=(\d+)') { return [int]$Matches[1] }
    }
    return -1
}

Spin 200
$before = LateCount

$sp.Write("TEST 0`n"); Spin 250
$lines.Clear()
$sp.Write("TEST 1`n")

# Wait for the first EVT and stamp it the instant it lands.
$seq = -1; $arrival = -1
$deadline = $sw.Elapsed.TotalMilliseconds + 3000
while ($sw.Elapsed.TotalMilliseconds -lt $deadline) {
    Drain
    foreach ($l in $lines) {
        if ($l -match '^EVT ') { $seq = [int](($l -split ' ')[4]); $arrival = $sw.Elapsed.TotalMilliseconds; break }
    }
    if ($seq -ge 0) { break }
}
if ($seq -lt 0) { $sp.Close(); throw 'no EVT seen' }

# Sleep the bulk, spin the tail: Start-Sleep alone overshoots by tens of ms.
$target = $arrival + $OffsetMs
$gap = $target - $sw.Elapsed.TotalMilliseconds - 15
if ($gap -gt 0) { Start-Sleep -Milliseconds ([int]$gap) }
while ($sw.Elapsed.TotalMilliseconds -lt $target) { }

$sent = $sw.Elapsed.TotalMilliseconds
$sp.Write("ACK $seq`n")
$actual = $sent - $arrival

Spin 300
$sp.Write("TEST 0`n"); Spin 300
$after = LateCount

$sp.Close(); $sp.Dispose()

'{0,-14} seq={1}  ack sent at EVT+{2:F1} ms   late: {3} -> {4}   {5}' -f `
    "offset ${OffsetMs}ms", $seq, $actual, $before, $after,
    $(if ($after -gt $before) { 'WITHHELD' } else { 'tap fired or entry swept' })
