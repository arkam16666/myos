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

PROGRAMS = [
    ("pkg list",          "pkg list",             ["calc", "available", "DESCRIPTION"]),
    ("pkg install",       "pkg install calc",     ["Installing calc 1.1", "Done."]),
    ("program in EL0",    "calc",                 ["calc>"]),
    ("readline syscall",  "12 + 34",              ["= 46"]),
    ("program exits",     "",                     ["myos> "]),
    ("floating point",    "pkg install cdemo",    ["Done."]),
    ("cdemo",             "cdemo hi there",       ["fib(25) = 75025", "circle area x 100 = 1963",
                                                   "args: hi there"]),
    ("crash in EL0",      "pkg install crashme",  ["Done."]),
    ("crashme",           "crashme",              ["Program crashed: Data Abort",
                                                   "address 0x0000000000001000"], 2.5),
    ("kernel survives",   "uptime",               ["Uptime:", "Timer interrupts so far"]),
    ("file syscalls",     "fortune",              ["Program crashed"], 0.1),   # placeholder, replaced below
]
# fortune is a plain program; make sure it is not installed yet, then install + run it
PROGRAMS[-1] = ("not installed",  "fortune",      ["Unknown command"])
PROGRAMS += [
    ("pkg info",          "pkg info guess",       ["Installed:   no", "Guess the number"]),
    ("pkg remove",        "pkg remove calc",      ["Removed calc"]),
    ("removed",           "calc",                 ["Unknown command"]),
    ("install python",    "pkg install python",   ["Done."], 3),
    ("write a script",    "write t.py print(6 * 7, 7 / 2)", ["Wrote"]),
    ("python file",       "python t.py",          ["42 3.5"], 4),
    ("python repl",       "python",               [">>> "], 4),
    ("python float",      "7 / 2",                ["3.5"]),
    ("python bigint",     "2 ** 100",             ["1267650600228229401496703205376"]),
    ("python files",      "import os; sorted(os.listdir())[:2]", ["calc.pkg"]),
    ("python leaves",     "import sys; sys.exit()", ["myos> "], 2),
]

SD_AFTER_REBOOT = [
    ("files survive",   None,                     ["MyFS mounted from myfs.img, 10 file(s)"]),
    ("still there",     "cat pi.txt",             ["made on a pi"]),
]

PANIC = [
    ("boot",            None,                     ["myos> "]),
    ("kernel panic",    "crash",                  ["KERNEL PANIC", "Undefined Instruction", "x30"]),
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
        for test in tests:
            name, command, expect = test[:3]
            wait = test[3] if len(test) > 3 else 1
            if command is None:
                out = read_for(5)               # boot: about 3 s in QEMU with an SD card
            else:
                for ch in command:
                    s.sendall(ch.encode())
                    time.sleep(0.01)
                s.sendall(b"\r")
                out = read_for(wait)
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


def update_test(machine, image, card, kernel_at):
    """send a 'new' kernel with the update protocol (as arm/update-pi.ps1 does),
    check it lands in myos8.img on the card, then reboot"""
    import struct
    import zlib
    new = open(os.path.join(ROOT, image), "rb").read() + b"UPDATED-BY-TEST"
    qemu = subprocess.Popen([QEMU, "-M", machine, "-kernel", image, "-display", "none",
                             "-monitor", "none", "-drive", f"file={card},if=sd,format=raw",
                             "-serial", f"tcp:127.0.0.1:{PORT},server=on,wait=on"],
                            cwd=ROOT, stderr=subprocess.DEVNULL)
    for _ in range(50):
        try:
            s = socket.create_connection(("127.0.0.1", PORT))
            break
        except OSError:
            time.sleep(0.2)
    s.settimeout(0.2)

    def read_until(words, seconds):
        """read until one of words has arrived along with the rest of its line"""
        data = b""
        end = time.time() + seconds
        while time.time() < end:
            hit = [data.find(w) for w in words if w in data]
            if hit and b"\n" in data[min(hit):]:
                break
            try:
                data += s.recv(4096)
            except socket.timeout:
                pass
        return data.decode("ascii", "replace")

    results = []
    try:
        read_until([b"myos> "], 5)
        s.sendall(b"update\r")
        out = read_until([b"UPDATE READY", b"UPDATE FAILED"], 5)
        results.append(("update ready", "UPDATE READY 1048576" in out, out))
        s.sendall(b"MYUP" + struct.pack("<II", len(new), zlib.crc32(new)) + new)
        out = read_until([b"UPDATE OK", b"UPDATE FAILED"], 120)
        results.append(("update written", f"UPDATE OK {len(new)} bytes" in out, out))
        s.sendall(b"update\r")                      # a broken transfer must be refused
        read_until([b"UPDATE READY"], 5)
        bad = b"\x00" * 100
        s.sendall(b"MYUP" + struct.pack("<II", len(bad), 12345) + bad)
        out = read_until([b"UPDATE OK", b"UPDATE FAILED"], 30)
        results.append(("bad checksum refused", "UPDATE FAILED checksum mismatch" in out, out))
        s.sendall(b"reboot\r")
        out = read_until([b"Type 'help'"], 10) + read_until([b"myos> "], 3)
        results.append(("reboot", "Rebooting" in out and "Type 'help'" in out, out))
    finally:
        s.close()
        qemu.kill()
        qemu.wait()

    lba, sectors = kernel_at
    with open(card, "rb") as f:
        f.seek(lba * 512)
        on_card = f.read(sectors * 512)
    results.append(("myos8.img on card replaced",
                    on_card[:len(new)] == new and on_card[len(new):] == b"\0" * (len(on_card) - len(new)),
                    f"first bytes {on_card[:16]!r}"))
    failed = 0
    for name, ok, info in results:
        print(f"{'PASS' if ok else 'FAIL'}  {machine}  {name}" + ("" if ok else f"   got: {info!r}"))
        failed += not ok
    return failed


def main():
    tmp = tempfile.mkdtemp(prefix="myos-armtest-")
    card = os.path.join(tmp, "sd.img")
    image = "arm/build/kernel8-qemu.img"
    pkgs = sorted(os.path.join(ROOT, "arm", "build", "packages", f)
                  for f in os.listdir(os.path.join(ROOT, "arm", "build", "packages")) if f.endswith(".pkg"))
    layout = mksdimg.build(card, [os.path.join(ROOT, "files", "readme.txt"),
                                  os.path.join(ROOT, "files", "hello.py")] + pkgs,
                           kernel=os.path.join(ROOT, image))
    lba, sectors = layout["myfs"]
    files = 2 + len(pkgs)
    boot_with_sd = [("boot banner + disk", None, ["for Raspberry Pi (ARM64)",
                                                 f"MyFS mounted from myfs.img, {files} file(s)"])]
    sd = [(n, c, [f"{files} file(s)" if e == "2 file(s)" else e for e in x]) for n, c, x in SD]
    failed = run("raspi3b", "raspi3b", image, boot_with_sd + BASIC[1:] + sd + PROGRAMS, sd=card)
    # after: pi.txt + t.py added, hello.py removed, cdemo + crashme + python installed, pkg.db
    after = [(n, c, [e.replace("10 file(s)", f"{files + 5} file(s)") for e in x])
             for n, c, x in SD_AFTER_REBOOT]
    failed += run("raspi3b", "raspi3b", image, after, sd=card)
    failed += check_with_myfs_py(card, lba, sectors)
    failed += update_test("raspi3b", image, card, layout["kernel"])
    failed += run("raspi3b", "raspi3b", image, PANIC, sd=card)

    # Pi 4 build: no SD support yet, CPU is a Cortex-A72
    pi4 = [(n, c, ["Cortex-A72" if e == "Cortex-A53" else e for e in x]) for n, c, x in BASIC]
    failed += run("raspi4b", "raspi4b", "arm/build/kernel8-qemu-pi4.img", pi4)
    return failed


if __name__ == "__main__":
    sys.exit(1 if main() else 0)
