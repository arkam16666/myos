# Update MyOS on the Raspberry Pi over USB - no need to take the SD card out.
#   powershell -ExecutionPolicy Bypass -File arm\update-pi.ps1 [-Port COM16] [-NoReboot]
# Builds MyOS, sends it to the 'update' command of the MyOS already running
# on the Pi, which checks it, writes it over myos8.img and reboots.
param([string]$Port, [switch]$NoReboot, [switch]$NoBuild)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

if (-not $NoBuild) { & .\build.ps1 -NoRun; Set-Location $PSScriptRoot }
$data = [IO.File]::ReadAllBytes("$PSScriptRoot\build\kernel8.img")

if (-not $Port) {           # find the MyOS USB serial port (USB ID 1209:0001)
    $dev = Get-PnpDevice -PresentOnly -Class Ports -ErrorAction SilentlyContinue |
        Where-Object { $_.InstanceId -match 'VID_1209&PID_0001' } | Select-Object -First 1
    if (-not $dev -or $dev.FriendlyName -notmatch '\((COM\d+)\)') {
        throw "MyOS is not connected (no USB serial port 1209:0001). Is the Pi plugged in?"
    }
    $Port = $Matches[1]
}

Add-Type -TypeDefinition @'
public static class MyosCrc {
    public static uint Crc32(byte[] data) {
        uint crc = 0xFFFFFFFF;
        foreach (byte b in data) {
            crc ^= b;
            for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (uint)(-(int)(crc & 1)));
        }
        return ~crc;
    }
}
'@
$crc = [MyosCrc]::Crc32($data)

$sp = New-Object System.IO.Ports.SerialPort $Port, 115200
$sp.DtrEnable = $true
$sp.RtsEnable = $true
$sp.Open()
function Wait-For([string[]]$words, [int]$seconds) {
    $text = ''
    $end = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $end) {
        $text += $sp.ReadExisting()
        foreach ($w in $words) {       # the word and the rest of its line
            $i = $text.IndexOf($w)
            if ($i -ge 0 -and $text.IndexOf("`n", $i) -ge 0) { return $text }
        }
        Start-Sleep -Milliseconds 50
    }
    return $text
}
try {
    Start-Sleep -Milliseconds 800
    [void]$sp.ReadExisting()                        # banner shown when the port opens
    Write-Host "Sending MyOS ($($data.Length) bytes) to the Pi on $Port ..."
    $sp.Write("update`r")
    $answer = Wait-For @('UPDATE READY', 'UPDATE FAILED', 'Unknown command') 10
    if ($answer -notmatch 'UPDATE READY') { throw "The Pi didn't accept the update: $answer" }

    $header = [Text.Encoding]::ASCII.GetBytes('MYUP') + [BitConverter]::GetBytes([uint32]$data.Length) +
              [BitConverter]::GetBytes([uint32]$crc)
    $sp.Write($header, 0, $header.Length)
    for ($i = 0; $i -lt $data.Length; $i += 4096) {
        $sp.Write($data, $i, [Math]::Min(4096, $data.Length - $i))
    }
    Write-Host "Writing to the SD card..."
    $answer = Wait-For @('UPDATE OK', 'UPDATE FAILED') 120
    $line = ($answer -split "`n" | Where-Object { $_ -match 'UPDATE (OK|FAILED)' } | Select-Object -First 1)
    if ($answer -notmatch 'UPDATE OK') { throw "Update failed: $line" }
    Write-Host $line.Trim()
    if (-not $NoReboot) {
        $sp.Write("reboot`r")
        Start-Sleep -Milliseconds 500
        Write-Host "The Pi is restarting with the new MyOS."
    }
} finally {
    try { $sp.Close() } catch { }      # after 'reboot' the port has already gone away
}
