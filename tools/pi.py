#!/usr/bin/env python3
"""pi - talk to MyOS on the Raspberry Pi over its USB serial port (macOS / Linux).

usage: python3 tools/pi.py update [--no-reboot] [FILE]   send a new kernel (default arm/build/kernel8.img)
       python3 tools/pi.py push FILE...                  copy files into MyFS (like push-pi.ps1)
       python3 tools/pi.py run "COMMAND" [SECONDS]       run a shell command, print what it says
options: --port /dev/cu.usbmodemXXXX   (found by itself when only one Pi is plugged in)

The same protocol as arm/update-pi.ps1 and arm/push-pi.ps1 on Windows:
'update' / 'recv NAME' answer "... READY", then get "MYUP" + u32 size + u32 CRC-32 + data.
Only one program can have the port open: close screen/minicom first.
"""
import glob
import os
import select
import sys
import termios
import time
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def find_port():
    ports = sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/ttyACM*"))
    if not ports:
        sys.exit("MyOS is not connected (no USB serial port). Is the Pi plugged in?")
    if len(ports) > 1:
        sys.exit("several serial ports: pick one with --port " + " ".join(ports))
    return ports[0]


class Port:
    def __init__(self, path):
        self.fd = os.open(path, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
        a = termios.tcgetattr(self.fd)
        a[0] = 0                                            # iflag: raw
        a[1] = 0                                            # oflag
        a[2] = termios.CS8 | termios.CREAD | termios.CLOCAL  # cflag
        a[3] = 0                                            # lflag: no echo, no line editing
        a[4] = a[5] = termios.B115200                       # (USB serial ignores the speed)
        a[6][termios.VMIN] = 0
        a[6][termios.VTIME] = 0
        termios.tcsetattr(self.fd, termios.TCSANOW, a)
        self.text = ""

    def write(self, data):
        if isinstance(data, str):
            data = data.encode()
        while data:
            select.select([], [self.fd], [], 5)
            try:
                n = os.write(self.fd, data[:4096])
            except BlockingIOError:
                continue
            data = data[n:]

    def read(self, seconds):
        """everything that arrives within SECONDS"""
        end = time.time() + seconds
        out = b""
        while time.time() < end:
            r, _, _ = select.select([self.fd], [], [], max(0, end - time.time()))
            if r:
                try:
                    out += os.read(self.fd, 4096)
                except (BlockingIOError, OSError):
                    break
        return out.decode(errors="replace")

    def wait_for(self, words, seconds):
        """read until one of WORDS and the rest of its line has arrived"""
        text, end = "", time.time() + seconds
        while time.time() < end:
            text += self.read(0.05)
            for w in words:
                i = text.find(w)
                if i >= 0 and text.find("\n", i) >= 0:
                    return text
        return text

    def close(self):
        try:
            os.close(self.fd)
        except OSError:
            pass


def send_blob(port, command, word, data, seconds):
    port.read(0.3)                                          # drop anything old
    port.write(command + "\r")
    answer = port.wait_for([f"{word} READY", f"{word} FAILED", "Unknown command"], 10)
    if f"{word} READY" not in answer:
        sys.exit(f"The Pi didn't accept it: {answer.strip()}")
    port.write(b"MYUP" + len(data).to_bytes(4, "little") + (zlib.crc32(data) & 0xFFFFFFFF).to_bytes(4, "little"))
    for i in range(0, len(data), 4096):
        port.write(data[i:i + 4096])
    answer = port.wait_for([f"{word} OK", f"{word} FAILED"], seconds)
    line = next((l for l in answer.splitlines() if f"{word} OK" in l or f"{word} FAILED" in l), answer.strip())
    if f"{word} OK" not in answer:
        sys.exit(f"Failed: {line}")
    print(line.strip())


def main(argv):
    port_path = None
    if "--port" in argv:
        i = argv.index("--port")
        port_path = argv[i + 1]
        del argv[i:i + 2]
    no_reboot = "--no-reboot" in argv
    argv = [a for a in argv if a != "--no-reboot"]
    if not argv or argv[0] not in ("update", "push", "run"):
        sys.exit(__doc__)
    port = Port(port_path or find_port())
    try:
        if argv[0] == "update":
            path = argv[1] if len(argv) > 1 else os.path.join(ROOT, "arm", "build", "kernel8.img")
            data = open(path, "rb").read()
            print(f"Sending MyOS ({len(data)} bytes) to the Pi ...")
            send_blob(port, "update", "UPDATE", data, 120)
            if not no_reboot:
                port.write("reboot\r")
                time.sleep(0.5)
                print("The Pi is restarting with the new MyOS.")
        elif argv[0] == "push":
            for path in argv[1:]:
                name = os.path.basename(path)
                data = open(path, "rb").read()
                print(f"{name} ({len(data)} bytes) ...")
                send_blob(port, "recv " + name, "RECV", data, 60)
        else:
            seconds = float(argv[2]) if len(argv) > 2 else 3
            port.read(0.3)
            port.write(argv[1] + "\r")
            text = port.read(seconds)
            sys.stdout.write(text.replace("\r\n", "\n"))
    finally:
        port.close()


if __name__ == "__main__":
    main(sys.argv[1:])
