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
       Commit = '1ab3208a1fceb12fca8f24ba57d6e13c5bff15e3' },   # Lua 5.4.7 (package 'lua')
    @{ Path = 'third_party/quickjs'; Url = 'https://github.com/bellard/quickjs.git'
       Commit = 'e585640ffa2cf88ce7c2d587583c6fe15928ec2f' }    # QuickJS JavaScript (package 'node')
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

# Wi-Fi firmware for the Pi Zero 2 W (and Pi 3 / Zero W) from Raspberry Pi OS's
# firmware-nonfree, pinned -> third_party/wifi-firmware. Not in git (Broadcom/Cypress
# licence); copy the right ones into MyFS:  python3 tools/pi.py push third_party/wifi-firmware/FILE
#   chip rev 2 (most Zero 2 W, 'wifi -v on' says ID register 0x....A9A6 with rev 2):
#       brcmfmac43436-sdio.bin  brcmfmac43436-sdio.txt  brcmfmac43436-sdio.clm_blob
#   chip rev 1 (some Zero 2 W):  brcmfmac43436s-sdio.bin  brcmfmac43436s-sdio.txt
#   Pi 3B / Zero W:  brcmfmac43430-sdio.bin  brcmfmac43430-sdio.txt  brcmfmac43430-sdio.clm_blob
$wifiCommit = 'c91cd2804cf7463aab913e7247c176049f16bbd6'     # RPi-Distro/firmware-nonfree, bookworm
$wifiUrl = "https://raw.githubusercontent.com/RPi-Distro/firmware-nonfree/$wifiCommit/debian/config/brcm80211"
$wifiFiles = [ordered]@{
    'brcmfmac43436-sdio.bin'       = 'brcm/brcmfmac43436-sdio.bin'
    'brcmfmac43436-sdio.txt'       = 'brcm/brcmfmac43436-sdio.txt'
    'brcmfmac43436-sdio.clm_blob'  = 'brcm/brcmfmac43436-sdio.clm_blob'
    'brcmfmac43436s-sdio.bin'      = 'brcm/brcmfmac43436s-sdio.bin'
    'brcmfmac43436s-sdio.txt'      = 'brcm/brcmfmac43436s-sdio.txt'
    'brcmfmac43430-sdio.bin'       = 'cypress/cyfmac43430-sdio.bin'
    'brcmfmac43430-sdio.clm_blob'  = 'cypress/cyfmac43430-sdio.clm_blob'
    'brcmfmac43430-sdio.txt'       = 'brcm/brcmfmac43430-sdio.txt'
}
New-Item -ItemType Directory -Force third_party/wifi-firmware | Out-Null
foreach ($name in $wifiFiles.Keys) {
    if (-not (Test-Path "third_party/wifi-firmware/$name")) {
        Write-Host "third_party/wifi-firmware/${name}: downloading..."
        Invoke-WebRequest "$wifiUrl/$($wifiFiles[$name])" -OutFile "third_party/wifi-firmware/$name" -UseBasicParsing
    }
}

# sources released only as zip files: checked against the SHA-256 they had when tested
$zips = @(
    @{ Path = 'third_party/sqlite'; Url = 'https://www.sqlite.org/2026/sqlite-amalgamation-3540100.zip'
       Sha256 = '0a9c8e34352dad46006aa82836d5383bff9dc6bc9ce5a60553c05aa30e6ad1f9' }   # SQLite 3.54.1 (package 'sqlite3')
)
foreach ($z in $zips) {
    if (Test-Path $z.Path) {
        Write-Host "$($z.Path): already there"
        continue
    }
    Write-Host "$($z.Path): downloading..."
    $zip = Join-Path ([IO.Path]::GetTempPath()) 'myos-dep.zip'
    $unpack = "$($z.Path).unpack"
    Invoke-WebRequest $z.Url -OutFile $zip -UseBasicParsing
    if ((Get-FileHash $zip -Algorithm SHA256).Hash -ne $z.Sha256) { throw "$($z.Url): checksum doesn't match" }
    Expand-Archive $zip -DestinationPath $unpack -Force
    Move-Item (Get-ChildItem $unpack -Directory | Select-Object -First 1).FullName $z.Path    # drop the zip's top folder
    Remove-Item $unpack, $zip -Recurse
}

# Prebuilt Linux programs (static AArch64 ELF) that MyOS-ARM runs with its Linux
# layer (arm/linux.c), taken from Alpine Linux packages. Each is checked against
# its SHA-256 and becomes a package in arm\build.ps1 (ports\NAME\NAME.info).
$bins = @(
    @{ Path = 'third_party/busybox/busybox'; Member = 'bin/busybox.static'
       Url = 'https://dl-cdn.alpinelinux.org/alpine/v3.20/main/aarch64/busybox-static-1.36.1-r31.apk'
       Sha256 = '1d8e7a7fc2ed69bdb8eb7be9d962489c21a0f75b72d8a325437c8a09d4cfac70' }  # BusyBox 1.36.1 (GPL-2.0)
    @{ Path = 'third_party/musl/ld-musl-aarch64.so.1'; Member = 'lib/ld-musl-aarch64.so.1'
       Url = 'https://dl-cdn.alpinelinux.org/alpine/v3.20/main/aarch64/musl-1.2.5-r4.apk'
       Sha256 = '71d53cd568d014bf71fa3f76819ddcd68d4f2ec5d9e9203e8ad4dd44667b9e9f' }  # musl 1.2.5 C library + dynamic loader (MIT)
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

# musl's licence, shipped inside the 'musl' package
$cr = 'third_party/musl/COPYRIGHT'
if (-not (Test-Path $cr)) {
    Invoke-WebRequest 'https://git.musl-libc.org/cgit/musl/plain/COPYRIGHT?h=v1.2.5' -OutFile $cr -UseBasicParsing
    if ((Get-FileHash $cr -Algorithm SHA256).Hash -ne 'F9BC4423732350EB0B3F7ED7E91D530298476F8FEC0C6C427A1C04ADE22655AF') {
        Remove-Item $cr
        throw "${cr}: wrong SHA-256"
    }
}

# CPython for the Linux layer (package 'python3', needs 'musl'): a prebuilt one from
# python-build-standalone (aarch64, linked against musl's libc.so only), checked
# against the SHA-256 its release publishes. Only the program and the standard
# library are unpacked (the rest has symlinks Windows can't make).
$py3 = @{ Path = 'third_party/python3'
          Url = 'https://github.com/astral-sh/python-build-standalone/releases/download/20261110/cpython-3.12.16%2B20261110-aarch64-unknown-linux-musl-install_only_stripped.tar.gz'
          Sha256 = '64bb304e59d5d6ed5eaae24e95e743a2420a8897d2f6c854b1438ff9cb47e432' }   # Python 3.12.16 (PSF licence + bundled libraries)
if (Test-Path "$($py3.Path)/python/bin/python3.12") {
    Write-Host "$($py3.Path): already there"
} else {
    Write-Host "$($py3.Path): downloading..."
    New-Item -ItemType Directory -Force $py3.Path | Out-Null
    $tgz = "$($py3.Path)/python.tar.gz"
    Invoke-WebRequest $py3.Url -OutFile $tgz -UseBasicParsing
    if ((Get-FileHash $tgz -Algorithm SHA256).Hash -ne $py3.Sha256.ToUpper()) { throw "${tgz}: wrong SHA-256" }
    tar -xzf $tgz -C $py3.Path python/bin/python3.12 python/lib/python3.12
    if (-not (Test-Path "$($py3.Path)/python/bin/python3.12")) { throw "failed to unpack $tgz" }
    Remove-Item $tgz
}

# Raspberry Pi firmware, only needed for USB boot (arm\usbboot.ps1)
$fw = 'https://github.com/raspberrypi/firmware/raw/stable/boot'
foreach ($f in 'bootcode.bin', 'start.elf', 'fixup.dat', 'start_cd.elf', 'fixup_cd.dat', 'LICENCE.broadcom') {
    if (-not (Test-Path "arm/usbboot/$f")) {
        Invoke-WebRequest "$fw/$f" -OutFile "arm/usbboot/$f" -UseBasicParsing
    }
}
Write-Host "Done."
