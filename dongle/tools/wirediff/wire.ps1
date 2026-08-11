# Scripted bench terminal for the V1/V3 rungs.
#
# Deliberately leaves DtrEnable false: the dongle must never gate transmission
# on DTR (CLAUDE.md §6), and the app never calls setSignals(). Driving it with
# DTR low is therefore the stronger test, not a shortcut.
#
# Sends \n, not \r\n. PROTOCOL.md strips a trailing \r (T2) but \n is the
# contract, and a bug in the stripper should not be hidden by never exercising
# the plain form.
param(
    [string]$Port = 'COM13',
    # Each step is "line|dwell_ms". An empty line means "listen only", which is
    # how the supervision timeout is observed without holding it open.
    [string[]]$Steps
)

$ErrorActionPreference = 'Stop'

$sp = New-Object System.IO.Ports.SerialPort $Port, 115200, 'None', 8, 'One'
$sp.ReadTimeout  = 100
$sp.WriteTimeout = 500
$sp.DtrEnable    = $false
$sp.RtsEnable    = $false
$sp.Open()

$t0  = [System.Diagnostics.Stopwatch]::StartNew()
$buf = ''

function Pump([int]$ms) {
    $deadline = $t0.Elapsed.TotalMilliseconds + $ms
    while ($t0.Elapsed.TotalMilliseconds -lt $deadline) {
        $chunk = $sp.ReadExisting()
        if ($chunk.Length -gt 0) {
            $script:buf += $chunk
            while ($script:buf.Contains("`n")) {
                $i    = $script:buf.IndexOf("`n")
                $line = $script:buf.Substring(0, $i).TrimEnd("`r")
                $script:buf = $script:buf.Substring($i + 1)
                $stamp = '{0,8:F0}' -f $t0.Elapsed.TotalMilliseconds
                Write-Output "$stamp ms  < $line"
            }
        }
        Start-Sleep -Milliseconds 5
    }
}

# Drain whatever was already in flight before attributing anything to a command.
Pump 300
Write-Output '--- drained ---'

foreach ($step in $Steps) {
    $parts = $step -split '\|'
    $line  = $parts[0]
    $dwell = [int]$parts[1]
    if ($line -ne '') {
        $stamp = '{0,8:F0}' -f $t0.Elapsed.TotalMilliseconds
        Write-Output "$stamp ms  > $line"
        $sp.Write($line + "`n")
    } else {
        $stamp = '{0,8:F0}' -f $t0.Elapsed.TotalMilliseconds
        Write-Output "$stamp ms  . listening ${dwell}ms"
    }
    Pump $dwell
}

$sp.Close()
$sp.Dispose()
Write-Output '--- closed ---'
