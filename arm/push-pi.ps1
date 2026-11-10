# Copy files from the PC into MyOS on the Raspberry Pi over USB (like scp).
#   powershell -ExecutionPolicy Bypass -File arm\push-pi.ps1 file1 [file2 ...] [-Port COM16]
#   powershell -ExecutionPolicy Bypass -File arm\push-pi.ps1 -Packages   (all Pi packages)
# Each file is saved in MyFS under its own name (e.g. calc.pkg, game.py).
param([Parameter(ValueFromRemainingArguments)][string[]]$Files, [string]$Port, [switch]$Packages)
$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot

if ($Packages) { $Files += Get-ChildItem "$here\build\packages\*.pkg" | ForEach-Object FullName }
if (-not $Files) { throw "Nothing to send. Give file names, or -Packages." }

if (-not $Port) {           # find the MyOS USB serial port (USB ID 1209:0001)
    $dev = Get-PnpDevice -PresentOnly -Class Ports -ErrorAction SilentlyContinue |
        Where-Object { $_.InstanceId -match 'VID_1209&PID_0001' } | Select-Object -First 1
    if (-not $dev -or $dev.FriendlyName -notmatch '\((COM\d+)\)') {
        throw "MyOS is not connected (no USB serial port 1209:0001). Is the Pi plugged in?"
    }
    $Port = $Matches[1]
}

if (-not ('MyosCrc' -as [type])) {
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
}

$sp = New-Object System.IO.Ports.SerialPort $Port, 115200
$sp.DtrEnable = $true
$sp.RtsEnable = $true
$sp.Open()
function Wait-For([string[]]$words, [int]$seconds) {
    $text = ''
    $end = (Get-Date).AddSeconds($seconds)
    while ((Get-Date) -lt $end) {
        $text += $sp.ReadExisting()
        foreach ($w in $words) {
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
    foreach ($f in $Files) {
        $path = (Resolve-Path $f).Path
        $name = Split-Path $path -Leaf
        $data = [IO.File]::ReadAllBytes($path)
        $sp.Write("recv $name`r")
        $answer = Wait-For @('RECV READY', 'RECV FAILED', 'Unknown command') 10
        if ($answer -notmatch 'RECV READY') { throw "The Pi didn't accept ${name}: $answer" }
        $header = [Text.Encoding]::ASCII.GetBytes('MYUP') + [BitConverter]::GetBytes([uint32]$data.Length) +
                  [BitConverter]::GetBytes([uint32][MyosCrc]::Crc32($data))
        $sp.Write($header, 0, $header.Length)
        for ($i = 0; $i -lt $data.Length; $i += 4096) {
            $sp.Write($data, $i, [Math]::Min(4096, $data.Length - $i))
        }
        $answer = Wait-For @('RECV OK', 'RECV FAILED') 60
        $line = ($answer -split "`n" | Where-Object { $_ -match 'RECV (OK|FAILED)' } | Select-Object -First 1)
        if ($answer -notmatch 'RECV OK') { throw "Sending $name failed: $line" }
        Write-Host "  $name ($($data.Length) bytes)"
    }
    Write-Host "Sent $($Files.Count) file(s) to MyOS on $Port."
} finally {
    try { $sp.Close() } catch { }
}
