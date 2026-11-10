# Boot MyOS on a Raspberry Pi Zero 2 W straight from this PC over USB.
#   1. Take the SD card OUT of the Pi.
#   2. Plug the Pi's "USB" port (not "PWR") into the PC.
#   3. .\usbboot.ps1
# rpiboot sends the firmware + MyOS to the Pi; afterwards MyOS shows up as a COM port.
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

& .\build.ps1 -NoRun
Copy-Item build\kernel8.img usbboot\kernel8.img -Force

$rpiboot = @("${env:ProgramFiles(x86)}\Raspberry Pi\rpiboot.exe",
             "$env:ProgramFiles\Raspberry Pi\rpiboot.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $rpiboot) {
    $cmd = Get-Command rpiboot -ErrorAction SilentlyContinue
    if ($cmd) { $rpiboot = $cmd.Source } else { throw "rpiboot not found - install rpiboot_setup.exe first" }
}

# rpiboot is picky about paths: give it a plain ASCII copy of the boot folder
$dir = Join-Path $env:TEMP 'myos-usbboot'
New-Item -ItemType Directory -Force $dir | Out-Null
Copy-Item usbboot\* $dir -Force

Write-Host "Waiting for the Pi (SD card out, USB port plugged in)..."
& $rpiboot -d $dir
Write-Host "Done. MyOS should appear as a new COM port in a few seconds."
