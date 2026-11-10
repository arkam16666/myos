"""Automatic tests for MyOS Pi (ARM64) in QEMU (Raspberry Pi 3B and 4B).

Boots arm/build/kernel8-qemu.img, talks to the Pi's serial port over
TCP and checks the answers.

usage: python tests/qemu_arm.py
"""
import os
import socket
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
QEMU = r"C:\Program Files\qemu\qemu-system-aarch64.exe"
PORT = 45481

# (name, command, text that must appear in the answer)
TESTS = [
    ("boot banner", None,                 ["for Raspberry Pi (ARM64)", "myos> "]),
    ("help",        "help",               ["uptime"]),
    ("info",        "info",               ["Cortex-A53", "exception level EL1"]),
    ("echo",        "echo hello pi",      ["hello pi"]),
    ("uptime",      "uptime",             ["Uptime:"]),
    ("unknown",     "nope",               ["Unknown command"]),
]


def run(machine, image, tests):
    qemu = subprocess.Popen([QEMU, "-M", machine, "-kernel", image, "-display", "none",
                             "-monitor", "none",
                             "-serial", f"tcp:127.0.0.1:{PORT},server=on,wait=on"],
                            cwd=ROOT, stderr=subprocess.DEVNULL)
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
                out = read_for(2)
            else:
                for ch in command:
                    s.sendall(ch.encode())
                    time.sleep(0.01)
                s.sendall(b"\r")
                out = read_for(0.8)
            missing = [e for e in expect if e not in out]
            if missing:
                failed += 1
                print(f"FAIL  {machine}  {name}   (missing: {missing})\n      got: {out!r}")
            else:
                print(f"PASS  {machine}  {name}")
    finally:
        s.close()
        qemu.kill()
        qemu.wait()
    return failed


def main():
    failed = run("raspi3b", "arm/build/kernel8-qemu.img", TESTS)
    # Pi 4 build: same tests, but the CPU is a Cortex-A72
    pi4 = [(n, c, ["Cortex-A72" if e == "Cortex-A53" else e for e in x]) for n, c, x in TESTS]
    failed += run("raspi4b", "arm/build/kernel8-qemu-pi4.img", pi4)
    return failed


if __name__ == "__main__":
    sys.exit(1 if main() else 0)
