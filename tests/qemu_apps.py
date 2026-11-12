"""Test every Python app in apps/py in QEMU (Raspberry Pi 3B, real MicroPython).

Each app is packaged, installed with 'pkg install' (the first one also
pulls in python as a dependency), driven with scripted key presses, and
must finish without a Python traceback.

usage: python tests/qemu_apps.py [APP ...]
"""
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import mkpkg  # noqa: E402
import mksdimg  # noqa: E402

QEMU = (shutil.which("qemu-system-aarch64")
        or r"C:\Program Files\qemu\qemu-system-aarch64.exe")
def _free_port():           # a port nobody uses, so several test runs can go at once
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


PORT = int(os.environ.get("MYOS_TEST_PORT", 0)) or _free_port()
UP, DOWN, LEFT, RIGHT = "\x1b[A", "\x1b[B", "\x1b[D", "\x1b[C"
CTRL_S, CTRL_X = "\x13", "\x18"


def line(s):
    return s + "\r"


# app -> (command line, [keys to send, a number = wait that many seconds], text expected)
TESTS = {
    "snake":     ("snake", [2, "q"], ["SNAKE", "Best score:"]),
    "blocks":    ("blocks", [2, " ", 1, LEFT, " ", 1, "q"], ["BLOCKS", "Game over. Score"]),
    "2048":      ("2048", [1, LEFT, UP, RIGHT, DOWN, "q"], ["2048   score:"]),
    "ttt":       ("ttt", [line("y")] + [line(str(i)) for i in (5, 1, 3, 7, 9, 2, 4, 6, 8)] + [2, line("n")],
                  ["Computer takes"]),
    "hangman":   ("hangman", [line(c) for c in "eaoitnsrlucmpdghbykvfwjxqz"] + [line("n")], ["Word:"]),
    "mines":     ("mines", [1, " ", 1, RIGHT, "f", "q"], ["MINES", "flagged"]),
    "four":      ("four", [line(str(c)) for c in [4, 3, 5, 2, 6, 1, 7] * 6] + [line("n")], ["Four in a row"]),
    "words":     ("words", [line(w) for w in ("abcde", "fghij", "klmno", "pqrst", "uvwxy", "zzzzz")] + [line("n")],
                  ["Try 1", "Try 6"]),
    "blackjack": ("blackjack", [line(""), 1, line("s"), 1, line("q")], ["Dealer", "You leave with"]),
    "maze":      ("maze", [1, RIGHT, DOWN, LEFT, UP, "q"], ["MAZE level 1"]),
    "life":      ("life", [3, "q"], ["LIFE  generation"]),
    "edit":      ("edit t.txt", [1, "hello", line(""), "world", CTRL_S, 1, CTRL_X], ["edit: t.txt", "Saved t.txt"]),
    "mandel":    ("mandel", [4, "+", 4, "q"], ["zoom in"]),
    "matrix":    ("matrix", [2, "x"], []),
    "stopwatch": ("stopwatch", [2, "l", 1, "q"], ["STOPWATCH"]),
    "todo":      ("todo add Buy milk", [], ["1 left, 0 done"]),
    "units":     ("units 10 km mi", [], ["10 km = 6.21371 mi"]),
    "primes":    ("primes 100", [], ["25 primes up to 100"]),
    "bf":        ("bf", [], ["Hello World!"]),
    "hanoi":     ("hanoi 3", [], ["Solved!", "moves: 7"]),
    "banner":    ("banner HI", [], ["#   # #####"]),
    "cowsay":    ("cowsay moo", [], ["< moo >", "(oo)"]),
    "dice":      ("dice 2d20", [], ["Total:"]),
    "typetest":  ("typetest", [line(""), line("abc"), line(""), line("abc"), line(""), line("abc")],
                  ["words per minute", "Average:"]),
    "mathquiz":  ("mathquiz", [line("1")] * 10, ["/10 right"]),
    "htop":      ("htop", [3, DOWN, "s", 1, "q"], ["MyOS htop", "CPU", "Mem", "PID TTY", "Wi-Fi:",
                                                  " history ", "sorted by MEM", "htop"]),
    "wifiman":   ("wifiman", [3, "q"], ["MyOS Wi-Fi manager", "no Wi-Fi chip"]),
    "rps":       ("rps", [line(c) for c in "rpsrpsrpsrpsrpsrpsrpsrpsrpsrps"] + [line("q")], ["score  you"]),
    "neofetch":  ("neofetch", [], ["MyOS Pi (ARM64)", "Memory", "MiB /", "MicroPython"]),
    "cal":       ("cal 11 2026", [], ["November 2026", "29 30", "B.E. 2569"]),
    "sl":        ("sl", [], ["(O)"]),
    "sudoku":    ("sudoku", [1, "h", "5", "c", "q"], ["SUDOKU", "Hint:"]),
    "sokoban":   ("sokoban", [1, "a", 1, "q"], ["SOKOBAN  level 1/9", "Level solved"]),
    "invaders":  ("invaders", [1, " ", LEFT, " ", 1, "q"], ["INVADERS", "score"]),
    "bc":        ("bc", [line("2^100"), line("scale=20; 1/7"), line("define f(n) {"), line("if (n < 2) return (n)"),
                         line("return (f(n-1) + f(n-2)) }"), line("f(20)"), line("obase=16; 255"), line("quit")],
                  ["1267650600228229401496703205376", ".14285714285714285714", "6765", "FF"]),
    # QEMU has no network: the download fails with a hint; fakelib is put in /lib by FILES
    "pip":       ("pip install datetime", [], ["Can't download", "Wi-Fi not connected"]),
    "mip":       ("mip help", [], ["mip install NAME", "micropython-lib"]),
}
# a second command run after the app, and what it must print
AFTER = {
    "edit":  ("cat t.txt", ["hello", "world"]),
    "todo":  ("todo done 1", ["[x] Buy milk"]),
    "units": ("units 100 c f", ["100 c = 212 f"]),
    "primes": ("primes is 91", ["91 = 7 x 13"]),
    "cal":    ("cal 2026", ["2026  (B.E. 2569)", "December"]),
    "bc":     ("bc -l \"scale=20; 4*a(1)\"", ["3.14159265358979323844"]),
    "pip":    [("pip list", ["fakelib", "0.1"]),
               ("python t_pip.py", ["fakelib works", "sha256 ba7816bf"]),
               ("pip show fakelib", ["Version: 0.1", "/lib/fakelib/__init__.py"]),
               ("pip uninstall fakelib", ["Removed fakelib"]),
               ("pip list", ["No libraries installed"]),
               ("python t_pip.py", ["fakelib gone"]),
               ("pip install numpy", ["Wi-Fi not connected"]),
               # pip.py itself, with myos.fetch answered from pipweb.json (a pretend micropython.org)
               ("python t_pipweb.py install demo", ["Installing demo 1.2", "Installing demolib 0.3",
                                                    "/lib/demo/util.py", "Done. Use it in Python: import demo"]),
               ("python t_pip.py", ["demo works: 42"]),
               ("python t_pipweb.py install broken", ["SHA-256 doesn't match"]),
               ("python t_pipweb.py install nosuch", ["not in micropython-lib", "pip search"]),
               ("python t_pipweb.py search demo", ["A demo", "versions: 1.2", "2 package(s)"]),
               ("pip uninstall demo demolib", ["Removed demo 1.2", "Removed demolib 0.3"]),
               ("ls /lib", ["pip.json"])],
    "mip":    ("pkg remove pip", []),
}


def _pip_web():
    """the answers of a pretend micropython.org for t_pipweb.py: url -> text"""
    import hashlib
    import json
    index = "https://micropython.org/pi/v2"
    web = {}

    def package(name, version, files, deps=(), damage=False):
        hashes = []
        for path, text in files:
            h = hashlib.sha256(text.encode()).hexdigest()[:8]
            web[f"{index}/file/{h[:2]}/{h}"] = text + ("#" if damage else "")
            hashes.append([path, h])
        meta = {"v": 1, "version": version, "hashes": hashes}
        if deps:
            meta["deps"] = [[d, "latest"] for d in deps]
        web[f"{index}/package/py/{name}/latest.json"] = json.dumps(meta)
    package("demo", "1.2", [("demo/__init__.py", "from demo.util import answer\n"),
                            ("demo/util.py", "import demolib\ndef answer():\n    return demolib.N\n")],
            deps=["demolib"])
    package("demolib", "0.3", [("demolib.py", "N = 42\n")])
    package("broken", "1.0", [("broken.py", "X = 1\n")], damage=True)
    web[f"{index}/index.json"] = json.dumps({"v": 2, "packages": [
        {"name": n, "version": v, "description": d, "versions": {"py": [v]}}
        for n, v, d in (("demo", "1.2", "A demo"), ("demolib", "0.3", ""), ("other", "1.0", "Not it"))]})
    return json.dumps(web).encode()
# commands run before the app, and other apps/py packages it needs on the disk
BEFORE = {"mip": ["pkg install pip"]}
NEEDS = {"mip": ["pip"]}
# files put on the disk for a test: a library that looks installed by pip, and a script using it
FILES = {
    "pip": {
        "/lib/fakelib/__init__.py": b"X = 'fakelib works'\n",
        "/lib/.pip.json": b'{"fakelib": {"version": "0.1", "files": ["fakelib/__init__.py"], "deps": []}}',
        "/t_pip.py": b"import hashlib, binascii\n"
                     b"print('sha256', binascii.hexlify(hashlib.sha256(b'abc').digest())[:8].decode())\n"
                     b"try:\n    import fakelib\n    print(fakelib.X)\n"
                     b"except ImportError:\n    print('fakelib gone')\n"
                     b"try:\n    import demo\n    print('demo works:', demo.answer())\n"
                     b"except ImportError:\n    pass\n",
        "/t_pipweb.py": b"import json\n"
                        b"WEB = json.loads(open('pipweb.json').read())\n"   # (moved to /home at first boot)
                        b"def fetch(url, max_size=65536):\n"
                        b"    return (200, WEB[url].encode()) if url in WEB else (404, b'')\n"
                        b"g = {'__name__': 'pip'}\n"
                        b"exec(open('/bin/pip.py').read(), g)\n"           # (pkg puts apps in /bin)
                        b"g['fetch'] = fetch\n"
                        b"g['run']()\n",
        "/pipweb.json": _pip_web(),
    },
}


def main():
    only = sys.argv[1:]
    tmp = tempfile.mkdtemp(prefix="myos-apptest-")
    names = [n for n in TESTS if not only or n in only]
    files = [os.path.join(ROOT, "arm", "build", "packages", "python.pkg")]
    for n in names + [x for n in names for x in NEEDS.get(n, ()) if x not in names]:
        src = os.path.join(ROOT, "apps", "py", n + ".py")
        out = os.path.join(tmp, n + ".pkg")
        with open(out, "wb") as f:
            f.write(mkpkg.make_package(src, src))
        files.append(out)
    card = os.path.join(tmp, "sd.img")
    image = "arm/build/kernel8-qemu.img"

    def setup(fs):
        for n in names:
            for path, data in FILES.get(n, {}).items():
                parts = path.split("/")[1:-1]
                for i in range(len(parts)):     # /lib, then /lib/fakelib
                    try:
                        fs.mkdir("/" + "/".join(parts[:i + 1]))
                    except Exception:
                        pass                    # (already there)
                fs.write(path, data)
    mksdimg.build(card, files, kernel=os.path.join(ROOT, image), setup=setup)

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

    def read_until_prompt(seconds):
        data = b""
        end = time.time() + seconds
        while time.time() < end:
            try:
                data += s.recv(65536)
            except socket.timeout:
                if data.rstrip().endswith(b"myos>"):
                    break
        return data.decode("utf-8", "replace")

    def send(text):
        for ch in text:
            s.sendall(ch.encode())
            time.sleep(0.02)

    failed = 0
    try:
        read_until_prompt(8)
        for n in names:
            cmd, keys, expect = TESTS[n]
            send(line("pkg install " + n))
            out = read_until_prompt(60)
            if "Done." not in out:
                print(f"FAIL  {n}  install   got: {out[-300:]!r}")
                failed += 1
                continue
            for bcmd in BEFORE.get(n, ()):
                send(line(bcmd))
                read_until_prompt(60)
            send(line(cmd))
            out = ""
            start = time.time()
            for k in keys:
                if isinstance(k, (int, float)):
                    time.sleep(k)
                else:
                    send(k)
                    time.sleep(0.3)
            out += read_until_prompt(180)
            after_ok = True
            steps = AFTER.get(n, [])
            for acmd, aexp in steps if isinstance(steps, list) else [steps]:
                send(line(acmd))
                aout = read_until_prompt(30)
                if not all(e in aout for e in aexp):
                    after_ok = False
                    print(f"      {n}: '{acmd}' missing {[e for e in aexp if e not in aout]}")
                out += aout
            missing = [e for e in expect if e not in out]
            bad = "Traceback" in out or "Error" in out
            back = out.rstrip().endswith("myos>")
            if missing or bad or not back or not after_ok:
                failed += 1
                tail = out[-700:]
                print(f"FAIL  {n}  ({time.time() - start:.0f}s) missing={missing} traceback={bad} "
                      f"back_at_prompt={back} after_ok={after_ok}\n      ...{tail!r}")
            else:
                print(f"PASS  {n}  ({time.time() - start:.0f}s)")
            send(line("pkg remove " + n))
            read_until_prompt(10)
    finally:
        s.close()
        qemu.kill()
        qemu.wait()
    print("ALL APPS PASSED" if not failed else f"{failed} app(s) failed")
    return failed


if __name__ == "__main__":
    sys.exit(1 if main() else 0)
