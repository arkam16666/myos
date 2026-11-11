# Build MyOS for Raspberry Pi (ARM64).
#   .\build.ps1          build both images, run the QEMU one (serial console window)
#   .\build.ps1 -NoRun   build only
#   .\build.ps1 -Pi4     build for Raspberry Pi 4
#
# Output:
#   build\kernel8.img       real hardware (serial + USB serial port)
#   build\kernel8-qemu.img  for QEMU (no USB: QEMU can't emulate the Pi as a USB device)
param([switch]$NoRun, [switch]$Pi4)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot

$clang = Get-Command clang -ErrorAction SilentlyContinue
$clang = if ($clang) { $clang.Source } else { "$env:ProgramFiles\LLVM\bin\clang.exe" }
if (-not (Test-Path $clang)) { throw "clang not found (winget install LLVM.LLVM)" }

$tusb = '../third_party/tinyusb'
$bcm  = "$tusb/hw/mcu/broadcom"
if (-not (Test-Path "$bcm/broadcom")) { throw "TinyUSB / broadcom-peripherals missing in third_party" }

$soc = if ($Pi4) { '2711' } else { '2837' }
$flags = @('--target=aarch64-unknown-none-elf', '-mcpu=cortex-a53', '-ffreestanding', '-nostdlib',
           '-fno-pic', '-mgeneral-regs-only', '-mstrict-align', '-O2', '-Wall',
           '-Wno-unused-command-line-argument', '-Wno-unused-function',
           "-DBCM_VERSION=$soc", "-DCFG_TUSB_MCU=OPT_MCU_BCM$soc",
           '-I.', "-I$tusb/src", "-I$bcm", '-I../python/include',
           '-fuse-ld=lld', '-Wl,-T,link.ld', '-Wl,--oformat=binary')
if ($Pi4) { $flags += '-DPI4' }

$common = @("$bcm/broadcom/boot8.s", "$bcm/broadcom/interrupts.c", "$bcm/broadcom/gen/interrupt_handlers.c",
            "$bcm/broadcom/mmu.c", "$bcm/broadcom/caches.c", "$bcm/broadcom/vcmailbox.c",
            'vectors.S', 'uart.c', 'led.c', 'console.c', 'print.c', 'sd.c', 'fat.c', 'myfs.c',
            'user.c', 'pkg.c', 'http.c', 'sha256.c', 'kernel.c', '../csdk/libc.c')
$usb    = @('usb.c', 'net.c', 'remote.c', 'ssh.c', 'rng.c',
            "$tusb/src/tusb.c", "$tusb/src/common/tusb_fifo.c",
            "$tusb/src/device/usbd.c",
            "$tusb/src/class/cdc/cdc_device.c", "$tusb/src/class/net/ncm_device.c",
            "$tusb/src/portable/synopsys/dwc2/dcd_dwc2.c", "$tusb/src/portable/synopsys/dwc2/dwc2_common.c",
            "$tusb/lib/networking/dhserver.c", "$tusb/lib/networking/dnserver.c")
# lwIP: the TCP/IP stack used for the USB network
$lwip = "$tusb/lib/lwip/src"
if (-not (Test-Path $lwip)) { throw "lwIP missing (run tools\get-deps.ps1)" }
$usb += @('init.c', 'def.c', 'inet_chksum.c', 'ip.c', 'mem.c', 'memp.c', 'netif.c', 'pbuf.c',
          'tcp.c', 'tcp_in.c', 'tcp_out.c', 'timeouts.c', 'udp.c') | ForEach-Object { "$lwip/core/$_" }
$usb += @('etharp.c', 'icmp.c', 'ip4.c', 'ip4_addr.c', 'ip4_frag.c') | ForEach-Object { "$lwip/core/ipv4/$_" }
$usb += "$lwip/netif/ethernet.c"
# Monocypher: the cryptography for SSH
$mono = "../third_party/monocypher/src"
if (-not (Test-Path $mono)) { throw "Monocypher missing (run tools/get-deps.ps1)" }
$usb += @("$mono/monocypher.c", "$mono/optional/monocypher-ed25519.c")
$usbflags = @("-I$lwip/include", "-I$tusb/lib/networking", "-I$mono", "-I$mono/optional")

New-Item -ItemType Directory -Force build | Out-Null
$ErrorActionPreference = 'Continue'     # compiler warnings on stderr are not errors
& $clang @flags @usbflags @common @usb -o build/kernel8.img 2>&1 | ForEach-Object { "$_" } |
    Where-Object { $_ -match 'error' } | ForEach-Object { Write-Host $_ }
$failed = $LASTEXITCODE
& $clang @flags '-DNO_USB' @common -o build/kernel8-qemu.img 2>&1 | ForEach-Object { "$_" } |
    Where-Object { $_ -match 'error' } | ForEach-Object { Write-Host $_ }
$failed = $failed -or $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($failed) { throw "build failed" }

# myos8.img for the SD card: the kernel padded with zeros to a fixed 1 MB,
# so 'update' can always rewrite it in place (MyOS can't grow FAT files)
$kernel = [IO.File]::ReadAllBytes("$PSScriptRoot\build\kernel8.img")
if ($kernel.Length -gt 1MB) { throw "kernel8.img is bigger than 1 MB" }
$padded = New-Object byte[] 1MB
[Array]::Copy($kernel, $padded, $kernel.Length)
[IO.File]::WriteAllBytes("$PSScriptRoot\build\myos8.img", $padded)

# packages for the Pi: the same packages\*.c as MyOS PC, built for ARM64
# -> build\packages\NAME.pkg (send them to the Pi with push-pi.ps1)
New-Item -ItemType Directory -Force build/packages | Out-Null
$appflags = @('--target=aarch64-unknown-none-elf', '-mcpu=cortex-a53', '-ffreestanding', '-nostdlib',
              '-fno-pic', '-fno-pie', '-O2', '-I../csdk',
              '-fuse-ld=lld', '-Wl,-T,../csdk/link.ld', '-Wl,--defsym=MYOS_ARCH=2', '-Wl,--oformat=binary')
$ErrorActionPreference = 'Continue'
foreach ($p in Get-ChildItem ../packages -Filter *.c) {
    & $clang @appflags ../csdk/crt0.c ../csdk/libc.c "../packages/$($p.Name)" -o "build/packages/$($p.BaseName)" 2>&1 |
        ForEach-Object { "$_" } | Where-Object { $_ -match 'error' } | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE) { $ErrorActionPreference = 'Stop'; throw "$($p.Name) failed for ARM" }
    python ../tools/mkpkg.py "build/packages/$($p.BaseName)" "../packages/$($p.Name)" "build/packages/$($p.BaseName).pkg"
}
$ErrorActionPreference = 'Stop'

# MicroPython for the Pi (python\ built with ARCH=arm64) -> build\packages\python.pkg
$make = Get-Command make -ErrorAction SilentlyContinue
$make = if ($make) { $make.Source } else {
    Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Recurse -Filter make.exe -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName }
if ((Test-Path ../third_party/micropython/py) -and $make) {
    # make can't handle the non-ASCII folder name, so build through an ASCII junction
    $junction = "$env:TEMP\myos-src"
    if (-not (Test-Path $junction)) { New-Item -ItemType Junction -Path $junction -Target (Resolve-Path ..).Path | Out-Null }
    $savedPath = $env:PATH
    $env:PATH = "$(Split-Path $clang);$env:ProgramFiles\Git\usr\bin;$env:PATH"
    Push-Location "$junction\python"
    $ErrorActionPreference = 'Continue'
    try { & $make ARCH=arm64 -j8 2>&1 | ForEach-Object { "$_" } | Where-Object { $_ -match '\berror\b' } | ForEach-Object { Write-Host "  $_" } }
    finally { Pop-Location; $env:PATH = $savedPath; $ErrorActionPreference = 'Stop' }
    if (-not (Test-Path ../python/build-arm64/python)) { throw "MicroPython (ARM) build failed" }
    python ../tools/mkpkg.py ../python/build-arm64/python ../python/python.info build/packages/python.pkg
}
Write-Host "Built arm\build\kernel8.img ($((Get-Item build/kernel8.img).Length) bytes) and kernel8-qemu.img"

if (-not $NoRun) {
    $machine = if ($Pi4) { 'raspi4b' } else { 'raspi3b' }
    # -nographic: the Pi's serial port becomes this console. Quit with Ctrl+A then X.
    Start-Process "$env:ProgramFiles\qemu\qemu-system-aarch64.exe" -ArgumentList `
        '-M', $machine, '-kernel', 'build/kernel8-qemu.img', '-nographic'
}
