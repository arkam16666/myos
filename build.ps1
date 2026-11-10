# Build MyOS into a floppy image and (optionally) run it in QEMU.
#   .\build.ps1          build + run
#   .\build.ps1 -NoRun   build only
param([switch]$NoRun)
$ErrorActionPreference = 'Stop'
Set-Location $PSScriptRoot   # use relative paths (folder name has non-ASCII chars)

function Find-Tool($name, $candidates) {
    $cmd = Get-Command $name -ErrorAction SilentlyContinue
    if ($cmd) { return $cmd.Source }
    foreach ($p in $candidates) { if (Test-Path $p) { return $p } }
    throw "$name not found. Install it (see README.md)."
}

$nasm = Find-Tool 'nasm' @(
    "$env:ProgramFiles\NASM\nasm.exe",
    "$env:LOCALAPPDATA\bin\NASM\nasm.exe")

New-Item -ItemType Directory -Force build | Out-Null
& $nasm -f bin boot.asm   -o build/boot.bin;   if ($LASTEXITCODE) { throw 'boot.asm failed' }
& $nasm -f bin kernel.asm -o build/kernel.bin; if ($LASTEXITCODE) { throw 'kernel.asm failed' }

# boot sector + kernel, padded to a 1.44 MB floppy
$img = New-Object byte[] 1474560
$boot   = [IO.File]::ReadAllBytes("$PWD\build\boot.bin")
$kernel = [IO.File]::ReadAllBytes("$PWD\build\kernel.bin")
[Array]::Copy($boot,   0, $img, 0,   $boot.Length)
[Array]::Copy($kernel, 0, $img, 512, $kernel.Length)
[IO.File]::WriteAllBytes("$PWD\build\os.img", $img)
Write-Host "Built build\os.img (boot $($boot.Length) B, kernel $($kernel.Length) B)"

# 32 MB hard disk image. Created once and never overwritten,
# so whatever MyOS saves on it survives rebuilds.
# A new disk gets a MyFS file system with everything in files\.
if (-not (Test-Path disk.img)) {
    $fs = [IO.File]::Create("$PWD\disk.img"); $fs.SetLength(32MB); $fs.Close()
    Write-Host "Created disk.img (32 MB hard disk)"
    if (Get-Command python -ErrorAction SilentlyContinue) {
        python tools/myfs.py disk.img format
    } else {
        Write-Host "Python not found: type 'format yes' inside MyOS to set up the disk."
    }
}

# user programs: apps\NAME.asm -> build\apps\NAME -> copied onto disk.img
New-Item -ItemType Directory -Force build/apps | Out-Null
$apps = Get-ChildItem apps -Filter *.asm -ErrorAction SilentlyContinue
foreach ($a in $apps) {
    & $nasm -f bin -i apps/ "apps/$($a.Name)" -o "build/apps/$($a.BaseName)"
    if ($LASTEXITCODE) { throw "$($a.Name) failed" }
}
$havePython = [bool](Get-Command python -ErrorAction SilentlyContinue)

# files\: copy files that aren't on the disk yet (never overwrites your changes)
$files = Get-ChildItem files -File -ErrorAction SilentlyContinue
if ($files -and $havePython) {
    python tools/myfs.py disk.img putnew @($files | ForEach-Object { "files/$($_.Name)" })
}
if ($apps -and $havePython) {
    foreach ($a in $apps) { python tools/myfs.py disk.img put "build/apps/$($a.BaseName)" $a.BaseName | Out-Null }
    Write-Host "Installed programs: $($apps.BaseName -join ', ')"
}

# packages: packages\NAME.asm or NAME.c -> build\packages\NAME.pkg -> disk.img ("the repository")
New-Item -ItemType Directory -Force build/packages | Out-Null
$pkgs = @(Get-ChildItem packages -Filter *.asm -ErrorAction SilentlyContinue)
foreach ($p in $pkgs) {
    & $nasm -f bin -i apps/ "packages/$($p.Name)" -o "build/packages/$($p.BaseName)"
    if ($LASTEXITCODE) { throw "$($p.Name) failed" }
}

# C packages need clang (winget install LLVM.LLVM)
$cpkgs = @(Get-ChildItem packages -Filter *.c -ErrorAction SilentlyContinue)
$clang = Get-Command clang -ErrorAction SilentlyContinue
$clang = if ($clang) { $clang.Source } elseif (Test-Path "$env:ProgramFiles\LLVM\bin\clang.exe") { "$env:ProgramFiles\LLVM\bin\clang.exe" }
$cflags = @('--target=x86_64-unknown-none-elf', '-ffreestanding', '-nostdlib', '-fno-pic', '-fno-pie',
            '-mno-red-zone', '-fno-stack-protector', '-O2', '-Icsdk',
            '-fuse-ld=lld', '-Wl,-T,csdk/link.ld', '-Wl,--oformat=binary')
if ($cpkgs -and $clang) {
    foreach ($p in $cpkgs) {
        & $clang @cflags csdk/crt0.c csdk/libc.c "packages/$($p.Name)" -o "build/packages/$($p.BaseName)"
        if ($LASTEXITCODE) { throw "$($p.Name) failed" }
    }
    $pkgs += $cpkgs
} elseif ($cpkgs) {
    Write-Host "clang not found: skipping C packages ($($cpkgs.BaseName -join ', '))"
}

# MicroPython package (python\ + third_party\micropython). Needs make, clang, python.
$make = Get-Command make -ErrorAction SilentlyContinue
$make = if ($make) { $make.Source } else {
    Get-ChildItem "$env:LOCALAPPDATA\Microsoft\WinGet\Packages" -Recurse -Filter make.exe -ErrorAction SilentlyContinue |
        Select-Object -First 1 -ExpandProperty FullName }
if ((Test-Path third_party/micropython/py) -and $make -and $clang -and $havePython) {
    # make can't handle the non-ASCII folder name, so build through an ASCII junction
    $junction = "$env:TEMP\myos-src"
    if (-not (Test-Path $junction)) { New-Item -ItemType Junction -Path $junction -Target $PWD.Path | Out-Null }
    $savedPath = $env:PATH
    $env:PATH = "$(Split-Path $clang);$env:ProgramFiles\Git\usr\bin;$env:PATH"
    Push-Location "$junction\python"
    try { & $make -j8 2>&1 | Where-Object { "$_" -match 'error|LINK' } | ForEach-Object { Write-Host "  $_" } }
    finally { Pop-Location; $env:PATH = $savedPath }
    if (-not (Test-Path python/build/python)) { throw "MicroPython build failed" }
    python tools/mkpkg.py python/build/python python/python.info build/packages/python.pkg
    if ($LASTEXITCODE) { throw "mkpkg python failed" }
    python tools/myfs.py disk.img put build/packages/python.pkg | Out-Null
    Write-Host "Packages in the repository: python (MicroPython)"
}
if ($pkgs -and $havePython) {
    foreach ($p in $pkgs) {
        python tools/mkpkg.py "build/packages/$($p.BaseName)" "packages/$($p.Name)" "build/packages/$($p.BaseName).pkg"
        if ($LASTEXITCODE) { throw "mkpkg $($p.Name) failed" }
        python tools/myfs.py disk.img put "build/packages/$($p.BaseName).pkg" | Out-Null
    }
    Write-Host "Packages in the repository: $($pkgs.BaseName -join ', ')"
}

if (-not $NoRun) {
    $qemu = Find-Tool 'qemu-system-x86_64' @("$env:ProgramFiles\qemu\qemu-system-x86_64.exe")
    & $qemu -drive format=raw,file=build/os.img,if=floppy `
            -drive format=raw,file=disk.img,if=ide,index=0,media=disk `
            -boot a -rtc base=localtime -vga std
}
