#!/usr/bin/env python3
"""repo - the MyOS package repository (like an apt mirror).

usage: python tools/repo.py            build it and serve it to the Pi (USB cable)
       python tools/repo.py build      only build arm/build/repo/
       python tools/repo.py publish    build, sign and push it to the 'packages' branch
                                       on GitHub (the Pi gets it over Wi-Fi)
       python tools/repo.py keygen     make the repository signing key (once)

What goes in:
  arm/build/packages/*.pkg   programs for the Pi and python.pkg (arm/build.ps1)
  apps/py/*.py               Python apps; each becomes NAME.pkg

On the Pi:
  pkg update          pkg search          pkg install snake

Signing: index.txt is signed with an Ed25519 key (tools/ed25519.py) kept
OUTSIDE the repository in ~/.myos/repo-signing-key; its public half is built
into MyOS (arm/repo_key.h). MyOS refuses a list from the internet without a
valid signature, and every package's SHA-256 is in the signed list.

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
import ed25519  # noqa: E402
import mkpkg  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "arm", "build", "repo")
PORT = 8080
KEY_FILE = os.path.join(os.path.expanduser("~"), ".myos", "repo-signing-key")
KEY_HEADER = os.path.join(ROOT, "arm", "repo_key.h")
BRANCH = "packages"


def load_key():
    try:
        seed = bytes.fromhex(open(KEY_FILE).read().strip())
        return seed if len(seed) == 32 else None
    except (OSError, ValueError):
        return None


def build():
    """Build arm/build/repo; returns True if the list got signed."""
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
        kind = {mkpkg.KIND_PYTHON: "py", mkpkg.KIND_LINUX: "linux"}.get(kind_num, "app")
        version = version.rstrip(b"\0").decode()
        desc = desc.rstrip(b"\0").decode()
        with open(os.path.join(OUT, name + ".pkg"), "wb") as f:
            f.write(data)
        lines.append(f"{name} {version} {kind} {len(data)} {hashlib.sha256(data).hexdigest()} {desc}")
    index = ("\n".join(lines) + "\n").encode()
    with open(os.path.join(OUT, "index.txt"), "wb") as f:
        f.write(index)
    seed = load_key()
    if seed:
        with open(os.path.join(OUT, "index.sig"), "w", newline="\n") as f:
            f.write(ed25519.sign(seed, index).hex() + "\n")
    print(f"Repository: {len(lines) - 1} packages in {OUT}" + (", signed" if seed else ", NOT signed (no key)"))
    return bool(seed)


def write_key_header(pub):
    rows = ", ".join(f"0x{b:02X}" for b in pub)
    with open(KEY_HEADER, "w", newline="\n") as f:
        f.write("// repo_key.h - the public half of the MyOS package repository key.\n"
                "// tools/repo.py keygen made it; the secret half stays on the developer's\n"
                "// computer (~/.myos/repo-signing-key). pkg.c checks index.sig with it.\n"
                "#ifndef REPO_KEY_H\n#define REPO_KEY_H\n#include <stdint.h>\n\n"
                f"static const uint8_t repo_public_key[32] = {{\n    {rows}\n}};\n\n#endif\n")


def keygen():
    if load_key():
        sys.exit(f"A key already exists: {KEY_FILE}\n(delete it first if you really want a new one; "
                 "Pis with the old key built in would then refuse new lists)")
    os.makedirs(os.path.dirname(KEY_FILE), exist_ok=True)
    seed = os.urandom(32)
    with open(KEY_FILE, "w") as f:
        f.write(seed.hex() + "\n")
    try:
        os.chmod(KEY_FILE, 0o600)
    except OSError:
        pass
    pub = ed25519.public_key(seed)
    write_key_header(pub)
    print(f"New signing key: {KEY_FILE}  (keep it secret, back it up)")
    print(f"Public key {pub.hex()} written to {KEY_HEADER} - rebuild MyOS (arm/build.ps1).")


def publish():
    """Push arm/build/repo to the 'packages' branch on GitHub (its history is replaced each time)."""
    import shutil
    import subprocess
    import tempfile
    if not build():
        sys.exit("No signing key: run  python tools/repo.py keygen  first.")
    url = subprocess.run(["git", "-C", ROOT, "remote", "get-url", "origin"],
                         capture_output=True, text=True, check=True).stdout.strip()
    tmp = tempfile.mkdtemp(prefix="myos-repo-")
    try:
        for f in os.listdir(OUT):
            shutil.copy(os.path.join(OUT, f), tmp)
        with open(os.path.join(tmp, "README.md"), "w", newline="\n") as f:
            f.write("# MyOS packages\n\n"
                    "The package repository of [MyOS](https://github.com/arkam16666/myos): "
                    "`pkg update` on the Pi downloads `index.txt` (signed: `index.sig`) and the "
                    "`.pkg` files from here. Made by `python tools/repo.py publish`; this branch "
                    "is replaced each time.\n\n"
                    "Licences: see THIRD-PARTY-NOTICES.md on the main branch. `busybox.pkg` "
                    "(if present) is BusyBox 1.36.1 from Alpine Linux's busybox-static package, "
                    "GPL-2.0; its source: https://busybox.net/downloads/busybox-1.36.1.tar.bz2 "
                    "and https://gitlab.alpinelinux.org/alpine/aports/-/tree/3.20-stable/main/busybox\n")
        git = ["git", "-C", tmp, "-c", "user.name=arka", "-c", "user.email=arka@localhost"]
        subprocess.run(["git", "-C", tmp, "init", "-q", "-b", BRANCH], check=True)
        subprocess.run(git + ["add", "-A"], check=True)
        subprocess.run(git + ["commit", "-q", "-m", "MyOS packages"], check=True)
        subprocess.run(["git", "-C", tmp, "push", "-q", "--force", url, f"{BRANCH}:{BRANCH}"], check=True)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    print(f"Published to the '{BRANCH}' branch of {url}")
    print("(GitHub's raw file cache can take up to 5 minutes to show new files)")


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
    cmd = sys.argv[1] if len(sys.argv) > 1 else "serve"
    if cmd == "keygen":
        keygen()
    elif cmd == "publish":
        publish()
    else:
        build()
        if cmd != "build":
            serve()
