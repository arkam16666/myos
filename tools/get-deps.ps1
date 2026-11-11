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

# Raspberry Pi firmware, only needed for USB boot (arm\usbboot.ps1)
$fw = 'https://github.com/raspberrypi/firmware/raw/stable/boot'
foreach ($f in 'bootcode.bin', 'start.elf', 'fixup.dat', 'start_cd.elf', 'fixup_cd.dat', 'LICENCE.broadcom') {
    if (-not (Test-Path "arm/usbboot/$f")) {
        Invoke-WebRequest "$fw/$f" -OutFile "arm/usbboot/$f" -UseBasicParsing
    }
}
Write-Host "Done."
