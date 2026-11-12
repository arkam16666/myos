"""Test CPython (package 'python3') on MyOS Pi, in QEMU (Raspberry Pi 3B):
a dynamically linked musl program (arm/linux.c loads /lib/ld-musl-aarch64.so.1),
a file-tree package (arm/pkg.c kind 4: gzip'd tar unpacked from '/', bigger
than the 8 MB package buffer), SIGILL caught by the program (OpenSSL's CPU probe).
usage: python tests/qemu_python3.py
needs: tools/get-deps.ps1 (third_party/python3, third_party/musl), then arm/build.ps1
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
sys.path.insert(0, os.path.join(ROOT, "tools"))
import mksdimg  # noqa: E402

QEMU = (shutil.which("qemu-system-aarch64")
        or r"C:\Program Files\qemu\qemu-system-aarch64.exe")
IMAGE = "arm/build/kernel8-qemu.img"
PACKAGES = os.path.join(ROOT, "arm", "build", "packages")
PROMPT = re.compile(r"myos(:[^\s>]*)?> $")

T1 = b'''import sys, json, sqlite3, dataclasses, hashlib, re, decimal, zlib, os
print("version", sys.version.split()[0], sys.platform)
print("where", sys.executable, sys.prefix)
print("json", json.dumps({"a": [1, 2, 3]}))
db = sqlite3.connect(":memory:")
db.execute("create table t(x)")
db.executemany("insert into t values (?)", [(i,) for i in range(10)])
print("sqlite", db.execute("select sum(x) from t").fetchone()[0])
print("sha256", hashlib.sha256(b"abc").hexdigest()[:16])
@dataclasses.dataclass
class P:
    x: int
    y: int = 2
print("dataclass", P(1))
print("re", re.findall(r"[0-9]+", "a1b22c333"))
print("decimal", decimal.Decimal(1) / decimal.Decimal(8))
print("zlib", len(zlib.compress(b"x" * 10000)))
with open("/t1.out", "w") as f:
    f.write("written by CPython")
print("readback", open("/t1.out").read())
print("args", sys.argv[1:])
'''


def _free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


failed = 0


def check(name, ok, info=""):
    global failed
    print(f"{'PASS' if ok else 'FAIL'}  {name}" + ("" if ok else f"   got: {info[-1500:]!r}"), flush=True)
    failed += not ok


def main():
    pkgs = [os.path.join(PACKAGES, n) for n in ("python3.pkg", "musl.pkg")]
    for p in pkgs:
        if not os.path.exists(p):
            sys.exit(f"missing {p}: run tools/get-deps.ps1, then arm/build.ps1")
    tmp = tempfile.mkdtemp(prefix="myos-py3test-")
    card = os.path.join(tmp, "sd.img")

    def setup(fs):                          # MyFS v2 in partition 2 (big enough for Python)
        for p in pkgs:
            fs.write("/" + os.path.basename(p), open(p, "rb").read())
        fs.write("/t1.py", T1)

    mksdimg.build(card, [], kernel=os.path.join(ROOT, IMAGE), card_mb=256, boot_mb=64, second_setup=setup)
    port = int(os.environ.get("MYOS_TEST_PORT", 0)) or _free_port()
    qemu = subprocess.Popen([QEMU, "-M", "raspi3b", "-kernel", IMAGE, "-display", "none", "-monitor", "none",
                             "-drive", f"file={card},if=sd,format=raw",
                             "-serial", f"tcp:127.0.0.1:{port},server=on,wait=on"],
                            cwd=ROOT, stderr=subprocess.DEVNULL)
    for _ in range(50):
        try:
            s = socket.create_connection(("127.0.0.1", port))
            break
        except OSError:
            time.sleep(0.2)
    s.settimeout(0.2)

    def read_prompt(seconds):
        data, end = "", time.time() + seconds
        while time.time() < end:
            try:
                data += s.recv(65536).decode("utf-8", "replace")
            except socket.timeout:
                if PROMPT.search(data.replace("\r", "")):
                    break
        return data.replace("\r", "")

    def run(line, seconds=60):
        for ch in line:
            s.sendall(ch.encode())
            time.sleep(0.002)
        s.sendall(b"\r")
        out = read_prompt(seconds)
        return out[out.find("\n") + 1:] if "\n" in out else out

    try:
        out = read_prompt(30)
        check("boot from partition 2", "MyFS mounted from partition 2" in out, out)
        out = run("pkg install python3", 600)
        check("pkg install python3 (tree package, pulls in musl)",
              "Unpacking python3 3.12.16" in out and "files installed" in out and
              "needs the musl C library" in out and "Done." in out and "rror" not in out, out)
        out = run("ls /lib")
        check("musl's loader in /lib", "ld-musl-aarch64.so.1" in out, out)
        out = run('python3 -c "print(6*7)"', 180)
        check("python3 -c", "42" in out.split("\n"), out)
        out = run("python3 /t1.py one two", 240)
        for want in ("version 3.12.16 linux", "where /usr/bin/python3 /usr", 'json {"a": [1, 2, 3]}',
                     "sqlite 45", "sha256 ba7816bf8f01cfea", "dataclass P(x=1, y=2)", "re ['1', '22', '333']",
                     "decimal 0.125", "zlib 33", "readback written by CPython", "args ['one', 'two']"):
            check("t1.py: " + want, want in out, out)
        out = run('python3 -c "import nosuchmodule"', 180)
        check("tracebacks", "ModuleNotFoundError: No module named 'nosuchmodule'" in out, out)
        out = run("pkg list")
        check("pkg list", "python3" in out and "musl" in out, out)
        out = run("pkg remove python3", 120)
        check("pkg remove python3", "Removed python3" in out, out)
        out = run("ls /usr/lib")
        check("its files are gone", "python3.12" not in out, out)
        out = run("python3")
        check("and the command", "Unknown command" in out or "not found" in out.lower(), out)
    finally:
        s.close()
        qemu.kill()
        qemu.wait()
    print(f"{failed} failed" if failed else "ALL PYTHON3 TESTS PASSED")
    return failed


if __name__ == "__main__":
    sys.exit(1 if main() else 0)
