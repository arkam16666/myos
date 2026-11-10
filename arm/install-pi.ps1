# Install MyOS on a Raspberry Pi Zero 2 W over SSH (Linux files stay on the card).
#   .\install-pi.ps1 -User <name> [-PiHost raspberrypi.local]
# You will be asked for the Pi's password.
param([Parameter(Mandatory)][string]$User, [string]$PiHost = 'raspberrypi.local')
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

& .\build.ps1 -NoRun
Copy-Item build\kernel8.img sdcard\myos8.img -Force

# scp is happier with a plain ASCII path
$tmp = Join-Path $env:TEMP 'myos-pi'
New-Item -ItemType Directory -Force $tmp | Out-Null
Copy-Item sdcard\myos8.img, sdcard\config-myos.txt, sdcard\install-remote.sh $tmp -Force

# A Pi on a USB cable is often only reachable by an IPv6 link-local address
# (fe80::...). ssh/scp need to be told which network adapter that is (%index).
# Windows finds ".local" names only some of the time, so try a few times.
for ($try = 1; $try -le 6; $try++) {
    $t = Test-NetConnection $PiHost -Port 22 -WarningAction SilentlyContinue
    if ($t.TcpTestSucceeded) { break }
    Write-Host "Looking for $PiHost ... (try $try of 6)"
    Start-Sleep -Seconds 3
}
if (-not $t.TcpTestSucceeded) { throw "Can't reach $PiHost on port 22. Is the Pi switched on and plugged in?" }
$addr = "$($t.RemoteAddress)"
if ($addr -like 'fe80:*' -and $addr -notlike '*%*') { $addr += "%$($t.InterfaceIndex)" }
$sshHost = $addr
$scpHost = if ($addr -like '*:*') { "[$addr]" } else { $addr }
Write-Host "Pi found at $addr (via $($t.InterfaceAlias))"

Write-Host "Copying MyOS to $User@$PiHost ..."
scp "$tmp\myos8.img" "$tmp\config-myos.txt" "$tmp\install-remote.sh" "${User}@${scpHost}:/tmp/"
if ($LASTEXITCODE) { throw "scp failed" }
ssh -t "$User@$sshHost" "sh /tmp/install-remote.sh"
