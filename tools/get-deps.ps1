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

# Raspberry Pi firmware, only needed for USB boot (arm\usbboot.ps1)
$fw = 'https://github.com/raspberrypi/firmware/raw/stable/boot'
foreach ($f in 'bootcode.bin', 'start.elf', 'fixup.dat', 'start_cd.elf', 'fixup_cd.dat', 'LICENCE.broadcom') {
    if (-not (Test-Path "arm/usbboot/$f")) {
        Invoke-WebRequest "$fw/$f" -OutFile "arm/usbboot/$f" -UseBasicParsing
    }
}
Write-Host "Done."
