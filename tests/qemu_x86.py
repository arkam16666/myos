"""Automatic tests for MyOS PC (x86-64) in QEMU.

Boots build/os.img with a throw-away copy of disk.img, types commands
through the QEMU monitor, reads the text screen straight out of video
memory (0xB8000) and checks it.

usage: python tests/qemu_x86.py [--keep-screens]
"""
import os
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
QEMU = r"C:\Program Files\qemu\qemu-system-x86_64.exe"
PORT = 45480

# (name, commands to type, text that must appear, seconds to wait after the last command)
TESTS = [
    ("boot banner",      [],                                   ["v1.0", "Type 'help'"], 0),
    ("help",             ["help"],                             ["pkg", "shutdown"], 0.5),
    ("uptime",           ["uptime"],                           ["Uptime:", "per second"], 0.5),
    ("cpu is 64-bit",    ["cpu"],                              ["64-bit long mode"], 0.5),
    ("disk",             ["disk"],                             ["QEMU HARDDISK", "32 MB"], 0.5),
    ("file system",      ["write t.txt hello", "cat t.txt"],   ["Wrote", "hello"], 0.5),
    ("rm",               ["rm t.txt", "cat t.txt"],            ["No such file: t.txt"], 0.5),
    ("pkg list",         ["pkg list"],                         ["calc", "python", "DESCRIPTION"], 0.5),
    ("pkg install + C",  ["pkg install calc", "calc", "12 + 34", ""], ["= 46"], 1),
    ("ring 3 crash",     ["pkg install crashme", "crashme"],   ["Program crashed: Page Fault"], 2.5),
    ("kernel survives",  ["uptime"],                           ["Uptime:"], 0.5),
    ("python file",      ["pkg install python", "python hello.py a b"],
                                                               ["Hello from a Python file", "'a', 'b'"], 4),
    ("python import",    ["python", "import shapes", "shapes.square_area(7)", "import sys; sys.exit()"],
                                                               ["49"], 5),
    ("python game",      "play_game",                          ["Correct in", "New best score saved",
                                                                "best.txt"], 0),
    ("reboot",           ["reboot"],                           [], 7),
    ("uptime restarted", ["uptime"],                           ["Uptime: 0h 0m"], 0.5),
    ("kernel panic",     ["crash"],                            ["KERNEL PANIC", "Divide Error"], 1),
]


def play_game(mon):
    """win files/game.py by binary search, reading the hints off the screen"""
    mon.type_line("clear")
    mon.type_line("rm best.txt")
    mon.type_line("python game.py")
    time.sleep(4)
    lo, hi = 1, 100
    for _ in range(8):
        guess = (lo + hi) // 2
        mon.type_line(str(guess))
        time.sleep(1)
        text = mon.screen()
        last = max(text.rfind("Higher!"), text.rfind("Lower!"), text.rfind("Correct in"))
        if last == text.rfind("Correct in"):
            break
        if last == text.rfind("Higher!"):
            lo = guess + 1
        else:
            hi = guess - 1
    time.sleep(1)
    mon.type_line("ls")
    time.sleep(0.5)

KEYS = {" ": "spc", "-": "minus", "'": "apostrophe", ".": "dot", "+": "shift-equal",
        "*": "shift-8", "/": "slash", "=": "equal", "(": "shift-9", ")": "shift-0",
        '"': "shift-apostrophe", "[": "bracket_left", "]": "bracket_right",
        ":": "shift-semicolon", ",": "comma", "_": "shift-minus", ";": "semicolon"}


class Monitor:
    def __init__(self, port):
        self.s = socket.create_connection(("127.0.0.1", port))
        self.s.settimeout(0.2)
        self.drain()

    def drain(self):
        data = b""
        try:
            while True:
                chunk = self.s.recv(65536)
                if not chunk:
                    break
                data += chunk
        except socket.timeout:
            pass
        return data.decode("latin-1")

    def cmd(self, line, wait=0.05):
        self.s.sendall((line + "\n").encode())
        time.sleep(wait)
        return self.drain()

    def type_line(self, text):
        for ch in text:
            if ch.isupper():
                self.cmd("sendkey shift-" + ch.lower(), 0.06)
            else:
                self.cmd("sendkey " + KEYS.get(ch, ch), 0.06)
        self.cmd("sendkey ret", 0.15)

    def screen(self):
        """the 80x25 text screen, read from video memory"""
        out = self.cmd("xp /2000xh 0xb8000", 0.4)
        out += self.drain()
        cells = [int(v, 16) for v in re.findall(r"0x([0-9a-f]{4})\b", out)]
        chars = "".join(chr(c & 0xFF) if 32 <= (c & 0xFF) < 127 else " " for c in cells[:2000])
        return "\n".join(chars[i:i + 80].rstrip() for i in range(0, len(chars), 80))


def main():
    keep = "--keep-screens" in sys.argv
    tmp = tempfile.mkdtemp(prefix="myos-test-")
    disk = os.path.join(tmp, "disk.img")
    shutil.copy(os.path.join(ROOT, "disk.img"), disk)     # never touch the real disk
    qemu = subprocess.Popen([QEMU, "-drive", "format=raw,file=build/os.img,if=floppy",
                             "-drive", f"format=raw,file={disk},if=ide,index=0,media=disk",
                             "-boot", "a", "-display", "none",
                             "-monitor", f"tcp:127.0.0.1:{PORT},server,nowait"], cwd=ROOT)
    failed = 0
    try:
        time.sleep(2)
        mon = Monitor(PORT)
        time.sleep(3)                                     # let MyOS finish booting
        for name, commands, expect, wait in TESTS:
            if commands == "play_game":
                play_game(mon)
            else:
                if commands:
                    mon.type_line("clear")
                for c in commands:
                    mon.type_line(c)
            time.sleep(wait)
            text = mon.screen()
            missing = [e for e in expect if e not in text]
            if missing:
                failed += 1
                print(f"FAIL  x86  {name}   (missing: {missing})")
                print("      screen:\n      " + text.replace("\n", "\n      "))
            else:
                print(f"PASS  x86  {name}")
            if keep:
                open(os.path.join(tmp, name.replace(" ", "_") + ".txt"), "w").write(text)
        try:
            mon.cmd("quit", 0.5)
        except OSError:
            pass                                          # QEMU closes the link as it quits
    finally:
        qemu.kill()
        qemu.wait()
        if not keep:
            shutil.rmtree(tmp, ignore_errors=True)
        else:
            print("screens kept in", tmp)
    return failed


if __name__ == "__main__":
    sys.exit(1 if main() else 0)
