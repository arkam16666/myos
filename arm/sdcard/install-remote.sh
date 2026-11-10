#!/bin/sh
# Runs ON the Raspberry Pi (copied there by install-pi.ps1).
# Adds MyOS next to Linux on the boot partition and reboots into it.
set -e
B=/boot/firmware
[ -f "$B/config.txt" ] || B=/boot
echo "Boot partition: $B"

# keep the original config so Linux can come back
[ -f "$B/config.txt.linux-backup" ] || sudo cp "$B/config.txt" "$B/config.txt.linux-backup"

sudo cp /tmp/myos8.img "$B/myos8.img"
if ! grep -q 'kernel=myos8.img' "$B/config.txt"; then
    sudo tee -a "$B/config.txt" < /tmp/config-myos.txt > /dev/null
fi
sync
echo "MyOS installed. Rebooting into MyOS - watch the serial console (115200 baud)."
sudo reboot
