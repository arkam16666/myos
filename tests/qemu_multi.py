"""Test running several programs at once on MyOS Pi, in QEMU (Raspberry Pi 3B).

usage: python tests/qemu_multi.py
"""
import os
import re
import socket
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import mksdimg  # noqa: E402

QEMU = r"C:\Program Files\qemu\qemu-system-aarch64.exe"
PORT = 45485
SPIN = "local t=os.clock() while os.clock()-t < %d do end print('%s')"


def main():
    tmp = tempfile.mkdtemp(prefix="myos-multitest-")
    card = os.path.join(tmp, "sd.img")
    image = "arm/build/kernel8-qemu.img"
    pkgs = [os.path.join(ROOT, "arm", "build", "packages", p) for p in ("lua.pkg", "calc.pkg")]
    mksdimg.build(card, pkgs, kernel=os.path.join(ROOT, image))
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

    results = []

    def check(name, ok, out):
        results.append(ok)
        print(f"{'PASS' if ok else 'FAIL'}  {name}" + ("" if ok else f"\n      got: {out[-500:]!r}"))

    try:
        read(8, "myos> ")
        send("pkg install lua")
        read(30, "Done.")
        send("pkg install calc")
        read(10, "Done.")

        # 1. a program in the background, the shell still answers
        send(f'lua -e "{SPIN % (6, "bg one done")}" &')
        out = read(3, "myos> ")
        m = re.search(r"\[(\d+)\] lua is running in the background", out)
        check("start in background", bool(m), out)
        bg = m.group(1) if m else "0"
        send("echo shell still here")
        out = read(3, "shell still here\r\n")
        check("shell works meanwhile", "shell still here" in out, out)

        # 2. ps shows it
        send("ps")
        out = read(3, "Free memory")
        check("ps lists it", re.search(rf"\b{bg}\s+serial\s+background\s+\d+ MB\s+\d+s\s+lua", out) is not None
              and "shell" in out, out)

        # 3. a foreground program runs at the same time
        send('lua -e "print(6*7)"')
        out = read(10, "42")
        check("foreground + background together", "42" in out, out)

        # 4. separate memory: both use big tables at the same addresses
        send('lua -e "local t={} for i=1,400000 do t[i]=i end local s=0 for i=1,#t do s=s+t[i] end '
             'print(\'sum\', s)" &')
        out = read(3, "background")
        m = re.search(r"\[(\d+)\] lua", out)
        big = m.group(1) if m else "0"
        send('lua -e "local t={} for i=1,300000 do t[i]=-i end print(#t, t[300000])"')
        out = read(60, "-300000")
        check("own memory (foreground)", "300000\t-300000" in out, out)
        if "sum\t80000200000" not in "".join(log):          # (it may have finished first)
            read(60, "sum\t80000200000")
        check("own memory (background)", "sum\t80000200000" in "".join(log), "".join(log))

        # 5. the first job finishes by itself; the shell reports it
        def bg_done():                                  # the output (the echoed command has ') after it)
            return re.search(r"bg one done\r?\n", "".join(log)) is not None
        if not bg_done():
            read(20, "bg one done\r\n")
        if not bg_done():                               # diagnose a hang
            log.append("\n=== DIAG: still waiting for bg one done ===\n")
            send("ps")
            read(3, "Free memory")
            send("uptime")
            read(3, "per second")
            read(5, "bg one done")
        send("")
        read(3, "myos> ")
        everything = "".join(log)
        check("background output + finished notice", bg_done() and
              re.search(rf"\[{bg}\] lua finished", everything) is not None, everything)

        # 6. kill
        send(f'lua -e "{SPIN % (60, "never")}" &')
        out = read(3, "background")
        m = re.search(r"\[(\d+)\] lua", out)
        victim = m.group(1) if m else "0"
        send(f"kill {victim}")
        out = read(5, "was stopped")
        check("kill", re.search(rf"\[{victim}\] lua was stopped", out) is not None and "never" not in out, out)

        # 7. Ctrl+C stops a program that never ends
        send('lua -e "while true do end"')
        time.sleep(2)
        s.sendall(b"\x03")
        out = read(5, "stopped")
        send("echo alive")
        out += read(3, "alive\r\n")
        check("Ctrl+C", "^C - stopped." in out and "alive" in out, out)

        # 8. fg brings a background job to the front
        send(f'lua -e "{SPIN % (3, "back in front")}" &')
        out = read(3, "background")
        m = re.search(r"\[(\d+)\] lua", out)
        job = m.group(1) if m else "0"
        send(f"fg {job}")
        out = read(10, "back in front")
        out += read(2, "myos> ")
        check("fg", "Bringing back lua" in out and "back in front" in out, out)

        # 9. a background job can't read the keyboard; the foreground one can
        send("calc &")
        read(3, "background")
        send("echo keys go to the shell")
        out = read(3, "keys go to the shell\r\n")
        check("background gets no keys", "keys go to the shell" in out and "I don't understand" not in out, out)
        send("ps")
        out = read(3, "Free memory")
        m = re.search(r"(\d+)\s+serial\s+background\s+\d+ MB\s+\d+s\s+calc", out)
        calc = m.group(1) if m else "0"
        send(f"fg {calc}")
        read(2, "Bringing back")
        send("2 + 3")
        out = read(3, "= 5")
        send("")
        out += read(3, "myos> ")
        check("fg program reads keys", "= 5" in out, out)

        # 10. Ctrl+C at the shell prompt drops the line; all memory is back
        send("this is not run", enter=False)
        s.sendall(b"\x03")
        out = read(2, "myos> ")
        send("ps")
        out += read(3, "Free memory")
        check("Ctrl+C at prompt, memory all free", "^C" in out and "Unknown command" not in out
              and "Free memory for programs: 256 MB" in out, out)
    finally:
        s.close()
        qemu.kill()
        qemu.wait()
        with open(os.path.join(tmp, "log.txt"), "w", encoding="utf-8") as f:
            f.write("".join(log))
        print("log:", os.path.join(tmp, "log.txt"))
    failed = results.count(False)
    print("ALL MULTITASKING TESTS PASSED" if not failed else f"{failed} multitasking test(s) failed")
    return failed


if __name__ == "__main__":
    sys.exit(1 if main() else 0)
