"""Automatic tests for MyOS Pi (ARM64) in QEMU (Raspberry Pi 3B and 4B).

Boots arm/build/kernel8-qemu.img, talks to the Pi's serial port over
TCP and checks the answers. The Pi 3B also gets a test SD card made by
tools/mksdimg.py (MBR + FAT32 + myfs.img), and afterwards the files MyOS
wrote are read back with tools/myfs.py.

usage: python tests/qemu_arm.py
"""
import os
import socket
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import mksdimg  # noqa: E402
import myfs  # noqa: E402

QEMU = r"C:\Program Files\qemu\qemu-system-aarch64.exe"
PORT = 45481

# (name, command, text that must appear in the answer)
BASIC = [
    ("boot banner", None,                 ["for Raspberry Pi (ARM64)", "myos> "]),
    ("help",        "help",               ["uptime", "hexdump"]),
    ("info",        "info",               ["Cortex-A53", "exception level EL1"]),
    ("echo",        "echo hello pi",      ["hello pi"]),
    ("uptime",      "uptime",             ["Uptime:"]),
    ("unknown",     "nope",               ["Unknown command"]),
]

SD = [
    # QEMU makes a 64 MB card an old-style SDSC card (byte addresses);
    # real cards today are SDHC (sector addresses): both paths are covered
    ("sd info",         "sd",                     ["SD card:", "64 MB", "myfs.img: sectors 4099-20482"]),
    ("raw sector",      "hexdump 2048",           ["Sector 2048", "MYOS"]),
    ("ls",              "ls",                     ["readme.txt", "hello.py", "2 file(s)"]),
    ("cat (from PC)",   "cat readme.txt",         ["Welcome to MyOS!"]),
    ("write",           "write pi.txt made on a pi", ["Wrote 13 bytes to pi.txt"]),
    ("cat (own file)",  "cat pi.txt",             ["made on a pi"]),
    ("rm",              "rm hello.py",            ["Removed hello.py"]),
    ("rm gone",         "cat hello.py",           ["No such file: hello.py"]),
]

SD_AFTER_REBOOT = [
    ("files survive",   None,                     ["MyFS mounted from myfs.img, 2 file(s)"]),
    ("still there",     "cat pi.txt",             ["made on a pi"]),
]


def run(label, machine, image, tests, sd=None):
    args = [QEMU, "-M", machine, "-kernel", image, "-display", "none", "-monitor", "none",
            "-serial", f"tcp:127.0.0.1:{PORT},server=on,wait=on"]
    if sd:
        args += ["-drive", f"file={sd},if=sd,format=raw"]
    qemu = subprocess.Popen(args, cwd=ROOT, stderr=subprocess.DEVNULL)
    s = None
    for _ in range(50):
        try:
            s = socket.create_connection(("127.0.0.1", PORT))
            break
        except OSError:
            time.sleep(0.2)
    s.settimeout(0.2)

    def read_for(seconds):
        data = b""
        end = time.time() + seconds
        while time.time() < end:
            try:
                data += s.recv(4096)
            except socket.timeout:
                pass
        return data.decode("ascii", "replace")

    failed = 0
    try:
        for name, command, expect in tests:
            if command is None:
                out = read_for(2.5)
            else:
                for ch in command:
                    s.sendall(ch.encode())
                    time.sleep(0.01)
                s.sendall(b"\r")
                out = read_for(1)
            missing = [e for e in expect if e not in out]
            if missing:
                failed += 1
                print(f"FAIL  {label}  {name}   (missing: {missing})\n      got: {out!r}")
            else:
                print(f"PASS  {label}  {name}")
    finally:
        s.close()
        qemu.kill()
        qemu.wait()
    return failed


def check_with_myfs_py(card, lba, sectors):
    """read MYFS.IMG back out of the card and check MyOS's changes with tools/myfs.py"""
    out = os.path.join(os.path.dirname(card), "myfs-after.img")
    mksdimg.read_myfs(card, lba, sectors, out)
    disk = myfs.Disk(out)
    _, entries = myfs.load(disk)
    names = {e["name"]: e for e in entries if e["flags"] & 1}
    data = b""
    if "pi.txt" in names:
        e = names["pi.txt"]
        data = disk.read(e["start"], e["alloc"])[:e["size"]]
    disk.f.close()
    ok = data == b"made on a pi\n" and "hello.py" not in names and "readme.txt" in names
    print(f"{'PASS' if ok else 'FAIL'}  raspi3b  myfs.py reads what MyOS wrote"
          + ("" if ok else f"   (files: {sorted(names)}, pi.txt = {data!r})"))
    return 0 if ok else 1


def main():
    tmp = tempfile.mkdtemp(prefix="myos-armtest-")
    card = os.path.join(tmp, "sd.img")
    lba, sectors = mksdimg.build(card, [os.path.join(ROOT, "files", "readme.txt"),
                                        os.path.join(ROOT, "files", "hello.py")])
    image = "arm/build/kernel8-qemu.img"
    boot_with_sd = [("boot banner + disk", None, ["for Raspberry Pi (ARM64)",
                                                 "MyFS mounted from myfs.img, 2 file(s)"])]
    failed = run("raspi3b", "raspi3b", image, boot_with_sd + BASIC[1:] + SD, sd=card)
    failed += run("raspi3b", "raspi3b", image, SD_AFTER_REBOOT, sd=card)
    failed += check_with_myfs_py(card, lba, sectors)

    # Pi 4 build: no SD support yet, CPU is a Cortex-A72
    pi4 = [(n, c, ["Cortex-A72" if e == "Cortex-A53" else e for e in x]) for n, c, x in BASIC]
    failed += run("raspi4b", "raspi4b", "arm/build/kernel8-qemu-pi4.img", pi4)
    return failed


if __name__ == "__main__":
    sys.exit(1 if main() else 0)
