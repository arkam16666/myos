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
    "htop":      ("htop", [3, DOWN, 1, "q"], ["MyOS htop", "CPU [", "Mem [", "PID TTY", "htop"]),
    "rps":       ("rps", [line(c) for c in "rpsrpsrpsrpsrpsrpsrpsrpsrpsrps"] + [line("q")], ["score  you"]),
}
# a second command run after the app, and what it must print
AFTER = {
    "edit":  ("cat t.txt", ["hello", "world"]),
    "todo":  ("todo done 1", ["[x] Buy milk"]),
    "units": ("units 100 c f", ["100 c = 212 f"]),
    "primes": ("primes is 91", ["91 = 7 x 13"]),
}


def main():
    only = sys.argv[1:]
    tmp = tempfile.mkdtemp(prefix="myos-apptest-")
    names = [n for n in TESTS if not only or n in only]
    files = [os.path.join(ROOT, "arm", "build", "packages", "python.pkg")]
    for n in names:
        src = os.path.join(ROOT, "apps", "py", n + ".py")
        out = os.path.join(tmp, n + ".pkg")
        with open(out, "wb") as f:
            f.write(mkpkg.make_package(src, src))
        files.append(out)
    card = os.path.join(tmp, "sd.img")
    image = "arm/build/kernel8-qemu.img"
    mksdimg.build(card, files, kernel=os.path.join(ROOT, image))

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
            if n in AFTER:
                acmd, aexp = AFTER[n]
                send(line(acmd))
                aout = read_until_prompt(30)
                after_ok = all(e in aout for e in aexp)
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
