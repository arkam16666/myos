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
            'uart.c', 'led.c', 'console.c', 'kernel.c', '../csdk/libc.c')
$usb    = @("$bcm/broadcom/mmu.c", "$bcm/broadcom/caches.c", "$bcm/broadcom/vcmailbox.c", 'usb.c',
            "$tusb/src/tusb.c", "$tusb/src/common/tusb_fifo.c",
            "$tusb/src/device/usbd.c",
            "$tusb/src/class/cdc/cdc_device.c",
            "$tusb/src/portable/synopsys/dwc2/dcd_dwc2.c", "$tusb/src/portable/synopsys/dwc2/dwc2_common.c")

New-Item -ItemType Directory -Force build | Out-Null
$ErrorActionPreference = 'Continue'     # compiler warnings on stderr are not errors
& $clang @flags @common @usb -o build/kernel8.img 2>&1 | ForEach-Object { "$_" } |
    Where-Object { $_ -match 'error' } | ForEach-Object { Write-Host $_ }
$failed = $LASTEXITCODE
& $clang @flags '-DNO_USB' @common -o build/kernel8-qemu.img 2>&1 | ForEach-Object { "$_" } |
    Where-Object { $_ -match 'error' } | ForEach-Object { Write-Host $_ }
$failed = $failed -or $LASTEXITCODE
$ErrorActionPreference = 'Stop'
if ($failed) { throw "build failed" }
Write-Host "Built arm\build\kernel8.img ($((Get-Item build/kernel8.img).Length) bytes) and kernel8-qemu.img"

if (-not $NoRun) {
    $machine = if ($Pi4) { 'raspi4b' } else { 'raspi3b' }
    # -nographic: the Pi's serial port becomes this console. Quit with Ctrl+A then X.
    Start-Process "$env:ProgramFiles\qemu\qemu-system-aarch64.exe" -ArgumentList `
        '-M', $machine, '-kernel', 'build/kernel8-qemu.img', '-nographic'
}
