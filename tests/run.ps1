# Build everything and run all automatic tests in QEMU.
#   powershell -ExecutionPolicy Bypass -File tests\run.ps1
# Uses a copy of disk.img, so your files are never touched.
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot
Set-Location $root

Write-Host "== build MyOS PC =="
& "$root\build.ps1" -NoRun
Write-Host "== build MyOS Pi (Pi 3 / Zero 2 W, then Pi 4) =="
& "$root\arm\build.ps1" -NoRun -Pi4
Copy-Item "$root\arm\build\kernel8-qemu.img" "$root\arm\build\kernel8-qemu-pi4.img" -Force
& "$root\arm\build.ps1" -NoRun
Set-Location $root          # the build scripts change folder

Write-Host "== tests =="
python tests\qemu_x86.py
$x86 = $LASTEXITCODE
python tests\qemu_arm.py
$arm = $LASTEXITCODE
python tests\qemu_apps.py
$apps = $LASTEXITCODE
python tests\qemu_langs.py
$langs = $LASTEXITCODE
python tests\qemu_multi.py
$multi = $LASTEXITCODE

if ($x86 -or $arm -or $apps -or $langs -or $multi) { Write-Host "SOME TESTS FAILED" -ForegroundColor Red; exit 1 }
Write-Host "ALL TESTS PASSED" -ForegroundColor Green
