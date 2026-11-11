# Download the outside source code MyOS builds on, at the exact versions it was tested with.
#   powershell -ExecutionPolicy Bypass -File tools\get-deps.ps1
# Everything lands in third_party\ (not stored in git).
$ErrorActionPreference = 'Stop'
Set-Location (Split-Path $PSScriptRoot)

$deps = @(
    @{ Path = 'third_party/micropython'; Url = 'https://github.com/micropython/micropython.git'
       Commit = 'c1c4e7b6ffaf3e7c343250f6531d466f2324b570' },   # Python for MyOS (python\)
    @{ Path = 'third_party/tinyusb'; Url = 'https://github.com/hathach/tinyusb.git'
       Commit = '12611f81a2c7fe99ddf63ce050e8df22346ee009' },   # USB for MyOS-ARM
    @{ Path = 'third_party/tinyusb/hw/mcu/broadcom'; Url = 'https://github.com/adafruit/broadcom-peripherals.git'
       Commit = '08370086080759ed54ac1136d62d2ad24c6fa267' },   # Raspberry Pi boot/MMU/IRQ code
    @{ Path = 'third_party/tinyusb/lib/lwip'; Url = 'https://github.com/lwip-tcpip/lwip.git'
       Commit = '159e31b689577dbf69cf0683bbaffbd71fa5ee10' },   # TCP/IP for the USB network
    @{ Path = 'third_party/monocypher'; Url = 'https://github.com/LoupVaillant/Monocypher.git'
       Commit = '0d85f98c9d9b0227e42cf795cb527dff372b40a4' },   # cryptography for SSH (4.0.2)
    @{ Path = 'third_party/lua'; Url = 'https://github.com/lua/lua.git'
       Commit = '1ab3208a1fceb12fca8f24ba57d6e13c5bff15e3' }    # Lua 5.4.7 (package 'lua')
)

foreach ($d in $deps) {
    if (Test-Path "$($d.Path)/.git") {
        Write-Host "$($d.Path): already there"
        continue
    }
    Write-Host "$($d.Path): downloading..."
    git init -q $d.Path
    git -C $d.Path remote add origin $d.Url
    git -C $d.Path fetch -q --depth 1 origin $d.Commit
    git -C $d.Path checkout -q FETCH_HEAD
    if ($LASTEXITCODE) { throw "failed to get $($d.Path)" }
}

# Prebuilt Linux programs (static AArch64 ELF) that MyOS-ARM runs with its Linux
# layer (arm/linux.c), taken from Alpine Linux packages. Each is checked against
# its SHA-256 and becomes a package in arm\build.ps1 (ports\NAME\NAME.info).
$bins = @(
    @{ Path = 'third_party/busybox/busybox'; Member = 'bin/busybox.static'
       Url = 'https://dl-cdn.alpinelinux.org/alpine/v3.20/main/aarch64/busybox-static-1.36.1-r31.apk'
       Sha256 = '1d8e7a7fc2ed69bdb8eb7be9d962489c21a0f75b72d8a325437c8a09d4cfac70' }  # BusyBox 1.36.1 (GPL-2.0)
)
foreach ($b in $bins) {
    if (Test-Path $b.Path) {
        Write-Host "$($b.Path): already there"
        continue
    }
    Write-Host "$($b.Path): downloading..."
    $dir = Split-Path $b.Path
    New-Item -ItemType Directory -Force "$dir/apk" | Out-Null
    $apk = "$dir/apk/$(Split-Path $b.Url -Leaf)"
    Invoke-WebRequest $b.Url -OutFile $apk -UseBasicParsing
    if ((Get-FileHash $apk -Algorithm SHA256).Hash -ne $b.Sha256.ToUpper()) { throw "${apk}: wrong SHA-256" }
    tar -xzf $apk -C "$dir/apk" $b.Member 2>$null     # an .apk is a gzip'd tar (several streams)
    if (-not (Test-Path "$dir/apk/$($b.Member)")) { throw "failed to unpack $apk" }
    Move-Item "$dir/apk/$($b.Member)" $b.Path
    Remove-Item -Recurse -Force "$dir/apk"
}

# Raspberry Pi firmware, only needed for USB boot (arm\usbboot.ps1)
$fw = 'https://github.com/raspberrypi/firmware/raw/stable/boot'
foreach ($f in 'bootcode.bin', 'start.elf', 'fixup.dat', 'start_cd.elf', 'fixup_cd.dat', 'LICENCE.broadcom') {
    if (-not (Test-Path "arm/usbboot/$f")) {
        Invoke-WebRequest "$fw/$f" -OutFile "arm/usbboot/$f" -UseBasicParsing
    }
}
Write-Host "Done."
