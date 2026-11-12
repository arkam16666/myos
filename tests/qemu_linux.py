"""Test running Linux programs on MyOS Pi, in QEMU (Raspberry Pi 3B):
the ELF loader and Linux system calls (arm/linux.c) with tests/linux/lxtest.c,
then BusyBox (pkg install busybox, applets, sh, vi, signals, /proc).

usage: python tests/qemu_linux.py
needs: arm/build.ps1 (arm/build/linux/*, arm/build/packages/busybox.pkg;
       BusyBox comes from tools/get-deps.ps1)
"""
import hashlib
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import mksdimg  # noqa: E402

QEMU = (shutil.which("qemu-system-aarch64")
        or r"C:\Program Files\qemu\qemu-system-aarch64.exe")


def _free_port():           # a port nobody uses, so several test runs can go at once
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


PORT = int(os.environ.get("MYOS_TEST_PORT", 0)) or _free_port()

NOTES = "".join(f"line {i} {'apple' if i % 3 == 0 else 'pear'} {100 - i}\n" for i in range(1, 41))


def dynamic_elf():
    """A (fake) dynamically linked AArch64 program whose loader (glibc's) isn't there:
    MyOS must say so politely."""
    eh = b"\x7fELF" + bytes([2, 1, 1]) + bytes(9)
    eh += struct.pack("<HHIQQQIHHHHHH", 2, 183, 1, 0x400078, 64, 0, 0, 64, 56, 1, 64, 0, 0)
    ph = struct.pack("<IIQQQQQQ", 3, 4, 120, 0x400078, 0x400078, 28, 28, 1)    # PT_INTERP
    return eh + ph + b"/lib/ld-linux-aarch64.so.1\0\0"


def main():
    tmp = tempfile.mkdtemp(prefix="myos-linuxtest-")
    card = os.path.join(tmp, "sd.img")
    image = "arm/build/kernel8-qemu.img"
    build = os.path.join(ROOT, "arm", "build")
    files = [os.path.join(build, "linux", "lxtest"), os.path.join(build, "linux", "lxpie"),
             os.path.join(build, "packages", "busybox.pkg")]
    for f in files:
        if not os.path.exists(f):
            sys.exit(f"missing {f}: run arm/build.ps1 (and tools/get-deps.ps1 for BusyBox)")
    notes = os.path.join(tmp, "notes.txt")
    with open(notes, "w", newline="\n") as f:
        f.write(NOTES)
    dyn = os.path.join(tmp, "dynprog")
    with open(dyn, "wb") as f:
        f.write(dynamic_elf())
    mksdimg.build(card, files + [notes, dyn], kernel=os.path.join(ROOT, image))
    qemu = subprocess.Popen([QEMU, "-M", "raspi3b", "-kernel", image, "-display", "none",
                             "-monitor", "none", "-drive", f"file={card},if=sd,format=raw",
                             "-serial", f"tcp:127.0.0.1:{PORT},server=on,wait=on"],
                            cwd=ROOT, stderr=subprocess.DEVNULL)
    for _ in range(50):
        try:
            s = socket.create_connection(("127.0.0.1", PORT))
            break
        except OSError:
            time.sleep(0.2)
    s.settimeout(0.1)
    log = []

    def read(seconds, until=None):
        data = b""
        end = time.time() + seconds
        while time.time() < end:
            try:
                data += s.recv(65536)
            except socket.timeout:
                if until and until.encode() in data:
                    break
        out = data.decode("utf-8", "replace")
        log.append(out)
        return out

    def send(text, enter=True):
        for ch in text:
            s.sendall(ch.encode())
            time.sleep(0.01)
        if enter:
            s.sendall(b"\r")

    def run(cmd, seconds=20, until="myos> "):
        send(cmd)
        return read(seconds, until)

    results = []

    def check(name, ok, out):
        results.append(ok)
        print(f"{'PASS' if ok else 'FAIL'}  {name}" + ("" if ok else f"\n      got: {out[-700:]!r}"))

    def free_mb():
        out = run("ps", 5, "Free memory")
        m = re.search(r"Free memory for programs: (\d+) MB", out)
        return int(m.group(1)) if m else -1

    try:
        read(8, "myos> ")
        mem_before = free_mb()

        # --- the loader and system calls (raw-syscall test program)
        out = run("lxtest one 'two words' three", 30, "lxtest done")
        oks = re.findall(r"^ok (.+?)\r?$", out, re.M)
        check("lxtest: arguments", "argv[2]=two words" in out and "argc=4" in out, out)
        check(f"lxtest: {len(oks)} system call checks", len(oks) >= 23 and "FAIL" not in out
              and "lxtest done" in out, out)
        read(3, "myos> ")
        out = run("lxpie echo static pie works")
        check("static PIE program", "static pie works" in out, out)
        out = run("lxtest exit 7")
        check("exit code", "Program exited" not in out and out.rstrip().endswith("myos>"), out)
        out = run("lxtest crash")
        check("crash is caught", "Program crashed: Data Abort" in out and "0x0000000030000000" in out, out)
        out = run("dynprog")
        check("dynamic program without its loader: a clear message", "needs its dynamic loader /lib/ld-linux-aarch64.so.1" in out, out)
        out = run("strace lxtest exit 3")
        check("strace", "] exit_group(0x3" in out, out)

        # --- BusyBox from its package
        out = run("pkg install busybox", 30)
        check("pkg install busybox", "Installing busybox 1.36.1" in out and "Done." in out, out)
        out = run("pkg info busybox")
        check("pkg info: Linux program", "Linux program" in out, out)
        out = run("busybox uname -a")
        check("busybox uname", "Linux myos" in out and "aarch64" in out, out)
        out = run("wc -l notes.txt")
        check("applet by name (wc)", re.search(r"\b40 notes.txt", out) is not None, out)
        out = run("grep -c apple notes.txt")
        check("grep", re.search(r"^13\r?$", out, re.M) is not None, out)
        out = run("sed -n 's/pear/PEAR/p' notes.txt")
        check("sed", "line 1 PEAR 99" in out and "apple" not in out, out)
        out = run("awk '$3 == \"apple\" { s += $4 } END { print \"sum\", s }' notes.txt")
        want = sum(100 - i for i in range(1, 41) if i % 3 == 0)
        check("awk", f"sum {want}" in out, out)
        out = run("md5sum notes.txt")
        check("md5sum", hashlib.md5(NOTES.encode()).hexdigest() in out, out)
        out = run("busybox ls -l /bin/busybox notes.txt")       # (programs in /bin, files in /home)
        check("ls -l", re.search(r"-rwxr-xr-x\s+1 0\s+0\s+1115944 .*busybox", out) is not None
              and "notes.txt" in out, out)
        out = run("sh -c 'cut -d\" \" -f3 notes.txt | sort | uniq -c | sort -rn | head -1'")
        check("sh pipeline", re.search(r"27 pear", out) is not None, out)
        out = run("sh -c 'for i in 1 2 3; do echo n$i; done > out.txt; tail -n 2 out.txt'")
        out2 = run("cat out.txt")
        check("redirection to a MyFS file", "n2\r\nn3" in out and "n1\r\nn2\r\nn3" in out2, out + out2)
        out = run("sh -c 'echo $(echo sub shell) | tr a-z A-Z; x=0; while [ $x -lt 9 ]; do x=$((x+1)); done; echo x=$x'")
        check("sh: substitution, loops, arithmetic", "SUB SHELL" in out and "x=9" in out, out)
        out = run("sh -c 'trap \"echo got USR1\" USR1; kill -USR1 $$; echo after'")
        check("signal handler (trap)", "got USR1\r\nafter" in out, out)
        out = run("sh -c 'sleep 5 & kill $!; wait; echo waited; sleep 1 & sleep 1 & wait; echo both'")
        check("kill + wait", "waited" in out and "both" in out, out)

        # --- /proc
        out = run("free")
        check("free (/proc/meminfo)", re.search(r"Mem:\s+\d+\s+\d+\s+\d+", out) is not None, out)
        out = run("sh -c 'sleep 3 & sleep 1; ps'")
        check("ps (/proc/PID/stat)", re.search(r"\d+ 0\s+0:00 sleep", out) is not None, out)
        out = run("busybox df")
        check("df (statfs)", re.search(r"myfs\s+\d+\s+\d+\s+\d+\s+\d+% /", out) is not None, out)

        # --- folders (MyFS v2)
        out = run("sh -c 'mkdir -p /work/sub; cd /work; echo hi > sub/f.txt; pwd; ls --color=never; mv sub/f.txt g.txt; "
                  "rmdir sub; ls --color=never; cat /work/g.txt'")
        out2 = run("cat /work/g.txt")
        check("folders: mkdir, cd, mv, rmdir", "/work\r\nsub\r\ng.txt\r\nhi" in out and "hi" in out2, out + out2)
        out = run("sh -c 'cd /work; find / -name g.txt; rm -r /work; ls /work'")
        check("folders: find, rm -r", "/work/g.txt" in out and "No such file" in out, out)

        # --- interactive: vi, the shell, Ctrl+C
        send("vi vi.txt")
        read(5, "vi.txt")
        for keys in ("ihello from vi", "\x1b", "othe second line", "\x1b", ":wq\r"):
            send(keys, enter=False)
            read(1)
        out = read(5, "myos> ")
        out += run("cat vi.txt")
        check("vi writes a file", "hello from vi\r\nthe second line" in out, out)
        send("sh")
        read(5, "# ")
        send("sleep 30")
        time.sleep(2)
        s.sendall(b"\x03")
        read(2)
        send("echo shell survived")
        out = read(5, "survived\r\n")
        send("exit")
        out += read(5, "myos> ")
        check("sh: Ctrl+C stops the job, not the shell", "shell survived" in out
              and out.rstrip().endswith("myos>"), out)
        out = run("busybox sleep 30 &", 5)
        m = re.search(r"\[(\d+)\] busybox is running in the background", out)
        check("background Linux program", m is not None, out)
        if m:
            out = run(f"kill {m.group(1)}", 5)
            out += read(3)
            out += run("echo x")
        time.sleep(1)
        after = free_mb()                   # (up to 4 recent programs stay cached in RAM, 2 MB each here)
        check("all memory back", mem_before - 8 <= after <= mem_before, f"before {mem_before} MB, after {after}")
    finally:
        s.close()
        qemu.kill()
        qemu.wait()
        with open(os.path.join(tmp, "log.txt"), "w", encoding="utf-8") as f:
            f.write("".join(log))
        print("log:", os.path.join(tmp, "log.txt"))
    failed = results.count(False)
    print("ALL LINUX TESTS PASSED" if not failed else f"{failed} Linux test(s) failed")
    return failed


if __name__ == "__main__":
    sys.exit(1 if main() else 0)
