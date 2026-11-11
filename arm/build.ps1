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

# macOS: Apple's /usr/bin/clang can't link bare-metal ARM, so prefer Homebrew's LLVM (brew install llvm lld)
$clang = @('/opt/homebrew/opt/llvm/bin/clang', '/usr/local/opt/llvm/bin/clang') |
    Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $clang) {
    $clang = Get-Command clang -ErrorAction SilentlyContinue
    $clang = if ($clang) { $clang.Source } else { "$env:ProgramFiles\LLVM\bin\clang.exe" }
}
if (-not (Test-Path $clang)) { throw "clang not found (winget install LLVM.LLVM / brew install llvm lld)" }

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
            'vectors.S', 'uart.c', 'led.c', 'console.c', 'print.c', 'sd.c', 'sdhost.c', 'sdio.c', 'wifi.c', 'fat.c', 'myfs.c',
            'user.c', 'proc.c', 'linux.c', 'pkg.c', 'http.c', 'sha256.c', 'kernel.c', '../csdk/libc.c')
$usb    = @('usb.c', 'net.c', 'remote.c', 'ssh.c', 'rng.c', 'tls.c',
            "$tusb/src/tusb.c", "$tusb/src/common/tusb_fifo.c",
            "$tusb/src/device/usbd.c",
            "$tusb/src/class/cdc/cdc_device.c", "$tusb/src/class/net/ncm_device.c",
            "$tusb/src/portable/synopsys/dwc2/dcd_dwc2.c", "$tusb/src/portable/synopsys/dwc2/dwc2_common.c",
            "$tusb/lib/networking/dhserver.c", "$tusb/lib/networking/dnserver.c")
# lwIP: the TCP/IP stack used for the USB network
$lwip = "$tusb/lib/lwip/src"
if (-not (Test-Path $lwip)) { throw "lwIP missing (run tools\get-deps.ps1)" }
$usb += @('init.c', 'def.c', 'dns.c', 'inet_chksum.c', 'ip.c', 'mem.c', 'memp.c', 'netif.c', 'pbuf.c', 'raw.c',
          'tcp.c', 'tcp_in.c', 'tcp_out.c', 'timeouts.c', 'udp.c') | ForEach-Object { "$lwip/core/$_" }
$usb += @('dhcp.c', 'etharp.c', 'icmp.c', 'ip4.c', 'ip4_addr.c', 'ip4_frag.c') | ForEach-Object { "$lwip/core/ipv4/$_" }
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
$kernel = [IO.File]::ReadAllBytes("$PSScriptRoot/build/kernel8.img")
if ($kernel.Length -gt 1MB) { throw "kernel8.img is bigger than 1 MB" }
$padded = New-Object byte[] 1MB
[Array]::Copy($kernel, $padded, $kernel.Length)
[IO.File]::WriteAllBytes("$PSScriptRoot/build/myos8.img", $padded)

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

# Ported C programs (ports\NAME\NAME.info + source in third_party) built with
# the full C library (csdk\libc: stdio, malloc, printf, math ...)
New-Item -ItemType Directory -Force build/ports | Out-Null
$libm = @('__cos', '__expo2', '__fpclassify', '__rem_pio2', '__rem_pio2_large', '__signbit', '__sin', '__tan',
          'acos', 'acosh', 'asin', 'asinh', 'atan', 'atan2', 'atanh', 'ceil', 'cos', 'cosh', 'copysign', 'erf',
          'exp', 'expm1', 'floor', 'fmod', 'frexp', 'ldexp', 'lgamma', 'log', 'log10', 'log1p', 'modf',
          'nearbyint', 'pow', 'rint', 'round', 'scalbn', 'sin', 'sinh', 'sqrt', 'tan', 'tanh', 'tgamma', 'trunc') |
        ForEach-Object { "../third_party/micropython/lib/libm_dbl/$_.c" }
$libc = @('../csdk/crt0.c', '../csdk/libc.c', '../csdk/libc/setjmp.S') +
        (Get-ChildItem ../csdk/libc -Filter *.c | ForEach-Object { "../csdk/libc/$($_.Name)" }) + $libm
$portflags = @('--target=aarch64-unknown-none-elf', '-mcpu=cortex-a53', '-ffreestanding', '-nostdlib',
               '-fno-pic', '-fno-pie', '-O2', '-w', '-I../csdk/include', '-I../csdk', '-DMYOS_LIBC',
               '-fuse-ld=lld', '-Wl,-T,../csdk/link.ld', '-Wl,--defsym=MYOS_ARCH=2', '-Wl,--oformat=binary')
$ports = @{
    # Lua 5.4: everything in third_party\lua except the test library
    'lua' = { Get-ChildItem ../third_party/lua -Filter 'l*.c' | Where-Object Name -ne 'ltests.c' |
              ForEach-Object { "../third_party/lua/$($_.Name)" } }
    # JavaScript: QuickJS engine + our Node-like front end (ports\node\node.c instead of qjs.c)
    'node' = { if (Test-Path ../third_party/quickjs/quickjs.c) {
              @('-Os', '-include', '../ports/node/config.h', '-I../ports/node', '-I../third_party/quickjs',
                '../ports/node/node.c') +
              (@('quickjs', 'libregexp', 'libunicode', 'cutils', 'dtoa') | ForEach-Object { "../third_party/quickjs/$_.c" }) } }
    # npm: the same program built with -DNODE_NPM, running ports\npm\npm.js instead of a script
    'npm' = { if (Test-Path ../third_party/quickjs/quickjs.c) {
              @('-Os', '-DNODE_NPM', '-include', '../ports/node/config.h', '-I../ports/node', '-I../third_party/quickjs',
                '../ports/node/node.c') +
              (@('quickjs', 'libregexp', 'libunicode', 'cutils', 'dtoa') | ForEach-Object { "../third_party/quickjs/$_.c" }) } }
    # SQLite: the amalgamation + our shell and MyFS file layer (ports\sqlite3\cli.c instead of shell.c)
    'sqlite3' = { if (Test-Path ../third_party/sqlite/sqlite3.c) {
              @('-Os', '-include', '../ports/sqlite3/config.h', '-I../third_party/sqlite',
                '../ports/sqlite3/cli.c', '../third_party/sqlite/sqlite3.c') } }
}
foreach ($name in $ports.Keys) {
    $src = & $ports[$name]
    if (-not $src) { Write-Host "  skipping $name (source missing: run tools\get-deps.ps1)"; continue }
    $ErrorActionPreference = 'Continue'
    & $clang @portflags "-DMYOS_PROGNAME=$name" @libc @src -o "build/ports/$name" 2>&1 |
        ForEach-Object { "$_" } | Where-Object { $_ -match 'error' } | ForEach-Object { Write-Host "  $_" }
    $ErrorActionPreference = 'Stop'
    if ($LASTEXITCODE) { throw "$name failed for ARM" }
    python ../tools/mkpkg.py "build/ports/$name" "../ports/$name/$name.info" "build/packages/$name.pkg"
}

# Linux programs (arm/linux.c runs static AArch64 ELF files):
#  - tests\linux\*.c: tiny test programs with raw system calls -> build\linux\NAME
#    (lxtest is also built as a static PIE: build\linux\lxpie)
#  - prebuilt ones from tools\get-deps.ps1 (third_party\busybox ...) -> build\packages\NAME.pkg
New-Item -ItemType Directory -Force build/linux | Out-Null
$lxflags = @('--target=aarch64-linux-gnu', '-ffreestanding', '-fno-builtin', '-nostdlib', '-O2',
             '-fuse-ld=lld', '-Wno-unused-command-line-argument')
$ErrorActionPreference = 'Continue'
foreach ($t in Get-ChildItem ../tests/linux -Filter *.c) {
    & $clang @lxflags '-static' '-Wl,--image-base=0x400000' "../tests/linux/$($t.Name)" -o "build/linux/$($t.BaseName)" 2>&1 |
        ForEach-Object { "$_" } | Where-Object { $_ -match 'error' } | ForEach-Object { Write-Host "  $_" }
    if ($LASTEXITCODE) { $ErrorActionPreference = 'Stop'; throw "$($t.Name) failed (Linux test program)" }
}
& $clang @lxflags '-static-pie' '-fPIE' ../tests/linux/lxtest.c -o build/linux/lxpie 2>&1 | Out-Null
$ErrorActionPreference = 'Stop'
foreach ($name in @('busybox')) {
    $bin = "../third_party/$name/$name"
    if (-not (Test-Path $bin)) { Write-Host "  skipping $name (missing: run tools\get-deps.ps1)"; continue }
    python ../tools/mkpkg.py $bin "../ports/$name/$name.info" "build/packages/$name.pkg"
}

# MicroPython for the Pi (python\ built with ARCH=arm64) -> build\packages\python.pkg
$make = Get-Command make -ErrorAction SilentlyContinue
$make = if ($make) { $make.Source } else {
    Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Recurse -Filter make.exe -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName }
if ((Test-Path ../third_party/micropython/py) -and $make) {
    $root = (Resolve-Path ..).Path
    $savedPath = $env:PATH
    if ($IsWindows -or $env:OS -eq 'Windows_NT') {
        # make can't handle the non-ASCII folder name, so build through an ASCII junction
        $src = "$env:TEMP\myos-src"
        if (-not (Test-Path $src)) { New-Item -ItemType Junction -Path $src -Target $root | Out-Null }
        $env:PATH = "$(Split-Path $clang);$env:ProgramFiles\Git\usr\bin;$env:PATH"
    } else {
        $src = $root
        if ($root -match '[^\x00-\x7F]') {      # same problem, solved with a symlink
            $src = Join-Path ([IO.Path]::GetTempPath()) 'myos-src'
            if (-not (Test-Path $src)) { New-Item -ItemType SymbolicLink -Path $src -Target $root | Out-Null }
        }
        $env:PATH = "$(Split-Path $clang):$env:PATH"
    }
    Push-Location (Join-Path $src 'python')
    $ErrorActionPreference = 'Continue'
    # Run make twice: the first run sometimes regenerates the QSTR headers but
    # leaves the old binary in place without an error; the second links it.
    try {
        foreach ($pass in 1, 2) {
            & $make ARCH=arm64 -j8 2>&1 | ForEach-Object { "$_" } | Where-Object { $_ -match '\berror\b' } | ForEach-Object { Write-Host "  $_" }
        }
    }
    finally { Pop-Location; $env:PATH = $savedPath; $ErrorActionPreference = 'Stop' }
    if (-not (Test-Path ../python/build-arm64/python)) { throw "MicroPython (ARM) build failed" }
    # a half-finished build once left an old binary behind: make sure the myos module is in it
    $pybin = [Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes((Resolve-Path ../python/build-arm64/python)))
    if (-not ($pybin.Contains('sysinfo') -and $pybin.Contains('getkey') -and $pybin.Contains('download'))) {
        throw "MicroPython (ARM) binary is missing the myos module - run arm\build.ps1 again"
    }
    python ../tools/mkpkg.py ../python/build-arm64/python ../python/python.info build/packages/python.pkg
}
Write-Host "Built arm\build\kernel8.img ($((Get-Item build/kernel8.img).Length) bytes) and kernel8-qemu.img"

if (-not $NoRun) {
    $machine = if ($Pi4) { 'raspi4b' } else { 'raspi3b' }
    # -nographic: the Pi's serial port becomes this console. Quit with Ctrl+A then X.
    $qemu = Get-Command qemu-system-aarch64 -ErrorAction SilentlyContinue
    $qemu = if ($qemu) { $qemu.Source } else { "$env:ProgramFiles\qemu\qemu-system-aarch64.exe" }
    $qargs = @('-M', $machine, '-kernel', 'build/kernel8-qemu.img', '-nographic')
    if ($IsWindows -or $env:OS -eq 'Windows_NT') { Start-Process $qemu -ArgumentList $qargs }
    else { & $qemu @qargs }     # no separate console window on macOS/Linux: run in this terminal
}
