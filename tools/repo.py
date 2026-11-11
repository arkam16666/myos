#!/usr/bin/env python3
"""repo - the MyOS package repository (like an apt mirror) on your PC.

usage: python tools/repo.py            build it and serve it to the Pi
       python tools/repo.py build      only build arm/build/repo/

What goes in:
  arm/build/packages/*.pkg   programs for the Pi and python.pkg (arm/build.ps1)
  apps/py/*.py               Python apps; each becomes NAME.pkg

On the Pi (plugged in by USB):
  pkg update          pkg search          pkg install snake

The server listens only on the USB network address (192.168.7.x) when it
can, so other computers on your LAN can't see it. Ctrl+C stops it.
"""
import hashlib
import http.server
import os
import socket
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import mkpkg  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "arm", "build", "repo")
PORT = 8080


def build():
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        os.remove(os.path.join(OUT, f))
    packages = {}

    pkg_dir = os.path.join(ROOT, "arm", "build", "packages")
    if os.path.isdir(pkg_dir):
        for f in sorted(os.listdir(pkg_dir)):
            if f.endswith(".pkg"):
                packages[f[:-4]] = open(os.path.join(pkg_dir, f), "rb").read()
    else:
        print("note: arm/build/packages is missing - run arm\\build.ps1 for the programs")

    app_dir = os.path.join(ROOT, "apps", "py")
    for f in sorted(os.listdir(app_dir)):
        if f.endswith(".py"):
            path = os.path.join(app_dir, f)
            packages[f[:-3]] = mkpkg.make_package(path, path)

    lines = ["# MyOS package list: name version kind size sha256 description"]
    for name in sorted(packages):
        data = packages[name]
        if data[:4] != b"MYPK":
            continue
        fields = mkpkg.HEADER.unpack(data[:mkpkg.HEADER.size])
        kind_num, version, desc = fields[1], fields[3], fields[4]
        kind = "py" if kind_num == mkpkg.KIND_PYTHON else "app"
        version = version.rstrip(b"\0").decode()
        desc = desc.rstrip(b"\0").decode()
        with open(os.path.join(OUT, name + ".pkg"), "wb") as f:
            f.write(data)
        lines.append(f"{name} {version} {kind} {len(data)} {hashlib.sha256(data).hexdigest()} {desc}")
    with open(os.path.join(OUT, "index.txt"), "w", newline="\n") as f:
        f.write("\n".join(lines) + "\n")
    print(f"Repository: {len(lines) - 1} packages in {OUT}")


def usb_address():
    """The PC's address on the MyOS USB network, if the Pi is plugged in."""
    for last in range(2, 5):
        addr = f"192.168.7.{last}"
        s = socket.socket()
        try:
            s.bind((addr, 0))
            return addr
        except OSError:
            pass
        finally:
            s.close()
    return None


class Handler(http.server.SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=OUT, **kwargs)

    def log_message(self, fmt, *args):
        print(f"  Pi: {self.requestline}  -> {args[1] if len(args) > 1 else ''}")


def serve():
    host = usb_address()
    if not host:
        print("Waiting for the Pi's USB network (plug the Pi in)...")
        for _ in range(60):
            time.sleep(1)
            host = usb_address()
            if host:
                break
    if not host:
        print("Still no MyOS USB network. Listening on all addresses instead.")
        host = "0.0.0.0"
    server = http.server.ThreadingHTTPServer((host, PORT), Handler)
    print(f"Serving packages on http://{host}:{PORT}/  (Ctrl+C to stop)")
    print("On the Pi:  pkg update   then   pkg search   and   pkg install NAME")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped.")


if __name__ == "__main__":
    build()
    if sys.argv[1:] != ["build"]:
        serve()
