"""Tests for MyFS v2 (folders) on MyOS Pi in QEMU (Raspberry Pi 3B).

Test SD cards are made with tools/mksdimg.py; afterwards tools/myfs.py
reads back what MyOS wrote.
  A  folders: cd/pwd/mkdir/rmdir/cp/mv/rm -r/tree, 500 files made on the PC,
     500 files made by a Python program, space given back on delete
  B  an old v1 volume: still works, 'fsupgrade' keeps every file
  C  a v1 image upgraded on the PC with tools/myfs.py
  D  fragmented free space: files in many pieces (extent blocks)
  E  space left behind by an "interrupted write" is given back at mount

usage: python tests/qemu_fs.py [A B C D E]
"""
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
import myfs  # noqa: E402

QEMU = (shutil.which("qemu-system-aarch64")
        or r"C:\Program Files\qemu\qemu-system-aarch64.exe")
IMAGE = "arm/build/kernel8-qemu.img"
PACKAGES = os.path.join(ROOT, "arm", "build", "packages")
PROMPT = re.compile(r"(myos(:[^\s>]*)?|>>)> $")


def _free_port():
    with socket.socket() as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class Pi:
    """one QEMU run with the test card"""

    def __init__(self, card):
        self.port = int(os.environ.get("MYOS_TEST_PORT", 0)) or _free_port()
        self.qemu = subprocess.Popen(
            [QEMU, "-M", "raspi3b", "-kernel", IMAGE, "-display", "none", "-monitor", "none",
             "-drive", f"file={card},if=sd,format=raw",
             "-serial", f"tcp:127.0.0.1:{self.port},server=on,wait=on"],
            cwd=ROOT, stderr=subprocess.DEVNULL)
        for _ in range(50):
            try:
                self.s = socket.create_connection(("127.0.0.1", self.port))
                break
            except OSError:
                time.sleep(0.2)
        self.s.settimeout(0.1)

    def read_prompt(self, timeout):
        data = ""
        end = time.time() + timeout
        while time.time() < end:
            try:
                data += self.s.recv(65536).decode("ascii", "replace")
            except socket.timeout:
                if PROMPT.search(data.replace("\r", "")):
                    break
        return data.replace("\r", "")

    def cmd(self, line, timeout=10):
        for ch in line:
            self.s.sendall(ch.encode())
            time.sleep(0.003)
        self.s.sendall(b"\r")
        out = self.read_prompt(timeout)
        return out[out.find("\n") + 1:] if "\n" in out else out     # without the echo

    def close(self):
        self.s.close()
        self.qemu.kill()
        self.qemu.wait()


failed = 0


def check(part, name, ok, info=""):
    global failed
    print(f"{'PASS' if ok else 'FAIL'}  {part}  {name}" + ("" if ok else f"   got: {info!r}"))
    failed += not ok


def expect(part, pi, name, line, words, timeout=10, absent=()):
    out = pi.cmd(line, timeout)
    ok = all(w in out for w in words) and not any(w in out for w in absent)
    check(part, name, ok, out[-1500:])
    return out


def free_kb(out):
    m = re.search(r"(\d+) KB free", out)
    return int(m.group(1)) if m else -1


def card_image(card, layout, name):
    out = os.path.join(os.path.dirname(card), name)
    mksdimg.read_myfs(card, layout["myfs"][0], layout["myfs"][1], out)
    return out


SCRIPT_MK = b"""import os
os.mkdir("py")
for i in range(500):
    f = open("py/f%d.txt" % i, "w")
    f.write("x" * (i % 700))
    f.close()
print("made", len(os.listdir("py")))
os.chdir("py")
print("cwd", os.getcwd())
print("size", os.stat("f7.txt")[6])
os.rename("f1.txt", "/renamed.txt")
os.chdir("..")
print("isdir", os.stat("py")[0] & 0o170000 == 0o040000)
ok = all(open("py/f%d.txt" % i).read() == "x" * (i % 700) for i in range(2, 500, 37))
print("content", ok)
try:
    os.rmdir("py")
except OSError:
    print("rmdir refused")
"""

SCRIPT_CLEAN = b"""import os
for n in os.listdir("py"):
    os.remove("py/" + n)
os.rmdir("py")
os.remove("renamed.txt")
print("cleaned", "py" in os.listdir())
"""

SCRIPT_FRAG = b"""data = b"0123456789abcdef" * 6400
f = open("frag.bin", "wb")
f.write(data)
f.close()
back = open("frag.bin", "rb").read()
print("frag", back == data, len(back))
"""


# ------------------------------------------------------------------ F, G
def read_part(card, second, out):
    """partition 2 of a card image -> a file tools/myfs.py can open"""
    with open(card, "rb") as f, open(out, "wb") as o:
        f.seek(second[0] * 512)
        left = second[1] * 512
        while left:
            chunk = f.read(min(left, 8 << 20))
            if not chunk:
                break
            o.write(chunk)
            left -= len(chunk)
    return out


def expand_test(P, tmp, card_mb, fill_mb, check_on_pc):
    """fsexpand: myfs.img (v1, with files) -> MyFS v2 in partition 2 (old 'Linux' data there)"""
    card = os.path.join(tmp, f"{P.lower()}.img")
    want = {}
    layout = mksdimg.build(card, [], version=1, card_mb=card_mb, boot_mb=64, second_fill=fill_mb,
                           setup=lambda fs: want.update(v1_files(fs, tmp)))
    n = len(want)
    part_mb = layout["second"][1] // 2048
    pi = Pi(card)
    try:
        out = pi.read_prompt(15)
        check(P, "boot: myfs.img", f"MyFS mounted from myfs.img, {n} file(s)" in out, out)
        expect(P, pi, "fsexpand asks first", "fsexpand",
               [f"rest of the SD card: {part_mb} MB", "ERASES partition 2 (type 0x83 Linux", "fsexpand yes"])
        expect(P, pi, "nothing changed yet", "sd", ["myfs.img: sectors"])
        expect(P, pi, "fsexpand yes", "fsexpand yes",
               [f"{n} file(s), 0 folder(s)", "Done: MyFS now uses partition 2"], 120)
        expect(P, pi, "files there", "ls", [f"{n} file(s)", "data01.bin", "readme.txt"])
        expect(P, pi, "cat", "cat readme.txt", ["Welcome to MyOS!"])
        expect(P, pi, "df", "df", ["sd-part2"])
        expect(P, pi, "folders", "mkdir big", [], absent=["need", "Bad", "rror"])
        expect(P, pi, "write in it", "write big/new.txt on the partition", ["Wrote 17 bytes"])
        expect(P, pi, "again: already", "fsexpand", ["already has a partition"])
        expect(P, pi, "fsck", "fsck", ["No problems found."], 60)
    finally:
        pi.close()
    pi = Pi(card)
    try:
        out = pi.read_prompt(30)
        check(P, "after reboot: partition 2", f"MyFS mounted from partition 2, {n + 1} file(s), 1 folder(s)" in out, out)
        expect(P, pi, "sd", "sd", ["MyFS on:  partition 2", f"({part_mb} MB)", "v2"])
        expect(P, pi, "still there", "cat big/new.txt", ["on the partition"])
        out = expect(P, pi, "free space", "df", ["sd-part2"])
        expect(P, pi, "fsck after reboot", "fsck", ["No problems found."], 60)
    finally:
        pi.close()
    with open(card, "rb") as f:
        mbr = f.read(512)
    check(P, "MBR: partition 2 is type 0xDA", mbr[446 + 16 + 4] == 0xDA, mbr[446 + 16 + 4])
    if check_on_pc:
        check_files(P, read_part(card, layout["second"], os.path.join(tmp, f"{P.lower()}-p2.img")), want,
                    {"/big/new.txt": b"on the partition\n"})
    return card


def part_f(tmp):
    expand_test("F", tmp, 256, 8, True)


def part_g(tmp):
    """bigger than the built-in 4 GB bitmap: an 8 GB card (written sparsely)"""
    card = expand_test("G", tmp, 8192, 4, False)
    os.remove(card)


# ------------------------------------------------------------------ A
def part_a(tmp):
    card = os.path.join(tmp, "a.img")

    def setup(fs):
        fs.write("/readme.txt", open(os.path.join(ROOT, "files", "readme.txt"), "rb").read())
        fs.write("/python.pkg", open(os.path.join(PACKAGES, "python.pkg"), "rb").read())
        fs.write("/mk.py", SCRIPT_MK)
        fs.write("/clean.py", SCRIPT_CLEAN)
        fs.mkdir("/docs")
        fs.write("/docs/pc.txt", b"made on the PC\n")
        fs.mkdir("/many")
        for i in range(500):
            fs.write(f"/many/n{i:03d}.txt", f"file {i}\n".encode())

    layout = mksdimg.build(card, [], setup=setup)
    files = 5 + 500
    P = "A"
    pi = Pi(card)
    try:
        out = pi.read_prompt(15)
        check(P, "boot: v2 mounted", f"MyFS mounted from myfs.img, {files} file(s), 2 folder(s)" in out, out)
        expect(P, pi, "ls shows folders", "ls", ["docs/", "many/", "<folder>", "4 file(s), 2 folder(s)"])
        expect(P, pi, "500 files from the PC", "ls many", ["n000.txt", "n499.txt", "500 file(s)"], 20)
        expect(P, pi, "cat with a path", "cat many/n499.txt", ["file 499"])
        expect(P, pi, "cat in a folder", "cat /docs/pc.txt", ["made on the PC"])
        expect(P, pi, "sd says v2", "sd", ["MyFS:     v2, 505 file(s), 2 folder(s)"])
        expect(P, pi, "mkdir", "mkdir games", [], absent=["Bad", "No such", "exists"])
        expect(P, pi, "cd", "cd games", ["myos:/games> "])
        expect(P, pi, "pwd", "pwd", ["/games\n"])
        expect(P, pi, "write in a folder", "write note.txt hello folder", ["Wrote 13 bytes to note.txt"])
        expect(P, pi, "ls here", "ls", ["note.txt", "1 file(s)"])
        expect(P, pi, "cd ..", "cd ..", ["myos> "])
        expect(P, pi, "cat relative path", "cat games/note.txt", ["hello folder"])
        expect(P, pi, "mkdir -p", "mkdir -p a/b/c", [], absent=["Bad", "No such"])
        expect(P, pi, "tree", "tree a", ["/a\n", "  b/", "    c/", "0 file(s), 2 folder(s)"])
        expect(P, pi, "cp into a folder", "cp games/note.txt a/b/c", [], absent=["No such", "Bad"])
        expect(P, pi, "copy is there", "cat /a/b/c/note.txt", ["hello folder"])
        expect(P, pi, "mv a file", "mv a/b/c/note.txt moved.txt", [], absent=["No such", "Bad"])
        expect(P, pi, "moved", "cat moved.txt", ["hello folder"])
        expect(P, pi, "old place empty", "ls a/b/c", ["(no files)"])
        expect(P, pi, "mv a folder", "mv games stuff", [], absent=["No such", "Bad"])
        expect(P, pi, "folder moved", "ls stuff", ["note.txt", "1 file(s)"])
        expect(P, pi, "old folder gone", "cd games", ["No such file or folder: games"])
        expect(P, pi, "rmdir not empty", "rmdir stuff", ["not empty"])
        expect(P, pi, "rm on a folder", "rm stuff", ["That's a folder"])
        expect(P, pi, "rm -r", "rm -r stuff", ["Removed stuff"])
        expect(P, pi, "rm -r done", "ls stuff", ["No such file or folder"])
        expect(P, pi, "rmdir", "rmdir a/b/c", [], absent=["No such", "not empty"])
        expect(P, pi, "mkdir exists", "mkdir docs", ["Already exists"])
        expect(P, pi, "no such folder", "write nothere/x.txt hi", ["No such file or folder"])
        long63 = "n" * 59 + ".txt"
        expect(P, pi, "63-char name", f"write {long63} long", [f"Wrote 5 bytes to {long63}"])
        expect(P, pi, "64-char name refused", f"write {long63}x long", ["Bad name"])
        expect(P, pi, "cd into folder via ..", "cd /a/b", ["myos:/a/b> "])
        expect(P, pi, "ls ..", "ls ..", ["b/", "0 file(s), 1 folder(s)"])
        expect(P, pi, "rmdir where you are", "rmdir /a/b", ["inside that folder"])
        expect(P, pi, "cd alone = /", "cd", ["myos> "])
        expect(P, pi, "install python", "pkg install python", ["Done."], 20)
        free0 = free_kb(pi.cmd("ls"))
        out = expect(P, pi, "python makes 500 files", "python mk.py",
                     ["made 500", "cwd /py", "size 7", "isdir True", "content True", "rmdir refused"], 180)
        expect(P, pi, "500 files in py", "ls py", ["499 file(s)", "f499.txt"], 30)
        expect(P, pi, "fsck with a big folder", "fsck", ["No problems found.", "1009 file(s)"], 30)
        expect(P, pi, "ls one file", "ls docs/pc.txt", ["docs/pc.txt", "15 bytes"])
        free1 = free_kb(pi.cmd("ls"))
        check(P, "files take space", 0 < free1 < free0 - 100, (free0, free1))
        expect(P, pi, "python removes them", "python clean.py", ["cleaned False"], 180)
        free2 = free_kb(pi.cmd("ls"))
        check(P, "space given back (500 files)", free2 == free0, (free0, free2))
        expect(P, pi, "cp a big file", "cp python big1", [], 20, absent=["No such", "full"])
        expect(P, pi, "cp again", "cp big1 docs/big2", [], 20, absent=["No such", "full"])
        free3 = free_kb(pi.cmd("ls"))
        size = os.path.getsize(os.path.join(PACKAGES, "python.pkg")) - 128
        used = 2 * ((size + 1023) // 1024)
        check(P, "big files take space", abs((free0 - free3) - used) <= 2, (free0, free3, used))
        pi.cmd("rm big1")
        pi.cmd("rm docs/big2")
        free4 = free_kb(pi.cmd("ls"))
        check(P, "space given back (big files)", free4 == free0, (free0, free4))
        expect(P, pi, "overwrite a file", "write moved.txt shorter", ["Wrote 8 bytes"])
        free5 = free_kb(pi.cmd("ls"))
        check(P, "rewrite keeps space", free5 == free4, (free4, free5))
        expect(P, pi, "fsck", "fsck", ["No problems found."], 30)
        expect(P, pi, "df", "df", ["myfs.img", "Use%"])
        expect(P, pi, "python os.listdir(path)", "python", [">>> "], 5)
        expect(P, pi, "listdir", "import os; sorted(os.listdir('/docs'))", ["['pc.txt']"])
        expect(P, pi, "getcwd", "os.chdir('docs'); os.getcwd()", ["'/docs'"])
        expect(P, pi, "open relative", "open('pc.txt').read()", ["made on the PC"])
        expect(P, pi, "stat dir", "os.stat('/many')[0] == 0o040755", ["True"])
        expect(P, pi, "mkdir error", "os.mkdir('/docs')", ["OSError", "17"])
        expect(P, pi, "leave python", "exit()", ["myos> "], 5)
    finally:
        pi.close()

    pi = Pi(card)                                   # reboot: everything still there?
    try:
        out = pi.read_prompt(15)
        check(P, "after reboot", "MyFS mounted from myfs.img, 509 file(s), 4 folder(s)" in out, out)
        expect(P, pi, "file survives", "cat moved.txt", ["shorter"])
        expect(P, pi, "folder survives", "ls many", ["500 file(s)"], 20)
        expect(P, pi, "fsck after reboot", "fsck", ["No problems found."], 30)
    finally:
        pi.close()

    img = card_image(card, layout, "a-after.img")   # and on the PC
    disk = myfs.Disk(img)
    fs = myfs.open_fs(disk)
    rep = fs.check(fix=False)
    ok = (fs.read("/moved.txt") == b"shorter\n" and fs.listdir("/a/b") == []
          and len(fs.listdir("/many")) == 500 and fs.read("/docs/pc.txt") == b"made on the PC\n"
          and ("n" * 59 + ".txt") in [n for n, _, _ in fs.listdir("/")]
          and not rep["damaged"] and rep["leaked_kb"] == 0 and rep["missing_kb"] == 0)
    check(P, "myfs.py reads what MyOS wrote", ok, (rep, fs.listdir("/")))
    disk.close()


# ------------------------------------------------------------------ B
def v1_files(fs, tmp):
    """a used v1 volume: some files deleted (holes), one rewritten bigger"""
    want = {}
    want["readme.txt"] = open(os.path.join(ROOT, "files", "readme.txt"), "rb").read()
    want["hello.py"] = open(os.path.join(ROOT, "files", "hello.py"), "rb").read()
    for i in range(30):
        want[f"data{i:02d}.bin"] = bytes((i * 7 + k) % 251 for k in range(i * 997 % 20000))
    want["empty.txt"] = b""
    for name, data in want.items():
        fs.write(name, data)
    for i in range(0, 30, 4):
        fs.remove(f"data{i:02d}.bin")
        del want[f"data{i:02d}.bin"]
    want["data01.bin"] = b"bigger now " * 3000
    fs.write("data01.bin", want["data01.bin"])
    return want


def check_files(part, img, want, extra):
    disk = myfs.Disk(img)
    fs = myfs.open_fs(disk)
    bad = [n for n, d in want.items() if fs.read("/" + n) != d]
    for p, d in extra.items():
        if fs.read(p) != d:
            bad.append(p)
    rep = fs.check(fix=False)
    check(part, "myfs.py: every old file identical", fs.version == 2 and not bad, bad)
    check(part, "myfs.py: fsck clean", not rep["damaged"] and not rep["leaked_kb"] and not rep["missing_kb"], rep)
    disk.close()


def part_b(tmp):
    card = os.path.join(tmp, "b.img")
    want = {}
    layout = mksdimg.build(card, [], version=1, setup=lambda fs: want.update(v1_files(fs, tmp)))
    n = len(want)
    P = "B"
    pi = Pi(card)
    try:
        out = pi.read_prompt(15)
        check(P, "boot: v1 mounted", f"MyFS mounted from myfs.img, {n} file(s)" in out and "old MyFS v1" in out, out)
        expect(P, pi, "ls (v1)", "ls", [f"{n} file(s)", "readme.txt"])
        expect(P, pi, "no folders in v1", "mkdir x", ["Folders need MyFS v2"])
        expect(P, pi, "df (v1)", "df", [f"{n}/64"])
        expect(P, pi, "fsck (v1)", "fsck", ["old MyFS v1"])
        expect(P, pi, "v1 still writable", "write v1new.txt still v1", ["Wrote 9 bytes"])
        expect(P, pi, "cat /path on v1", "cat /readme.txt", ["Welcome to MyOS!"])
        expect(P, pi, "fsupgrade", "fsupgrade", [f"Done: {n + 1} file(s) kept"], 30)
        expect(P, pi, "already v2", "fsupgrade", ["Already MyFS v2."])
        expect(P, pi, "files kept", "ls", [f"{n + 1} file(s)", "data01.bin", "v1new.txt"])
        expect(P, pi, "cat after upgrade", "cat readme.txt", ["Welcome to MyOS!"])
        expect(P, pi, "folders now", "mkdir x", [], absent=["need", "Bad"])
        expect(P, pi, "write in it", "write x/y.txt in a folder", ["Wrote 12 bytes"])
        expect(P, pi, "fsck after upgrade", "fsck", ["No problems found."], 30)
    finally:
        pi.close()
    pi = Pi(card)
    try:
        out = pi.read_prompt(15)
        check(P, "after reboot: v2", f"MyFS mounted from myfs.img, {n + 2} file(s), 1 folder(s)" in out, out)
        expect(P, pi, "still there", "cat x/y.txt", ["in a folder"])
    finally:
        pi.close()
    check_files(P, card_image(card, layout, "b-after.img"), want,
                {"/v1new.txt": b"still v1\n", "/x/y.txt": b"in a folder\n"})


# ------------------------------------------------------------------ C
def part_c(tmp):
    card = os.path.join(tmp, "c.img")
    want = {}

    def setup(fs):
        want.update(v1_files(fs, tmp))
        myfs.upgrade(fs.disk)                       # upgraded on the PC

    layout = mksdimg.build(card, [], version=1, setup=setup)
    P = "C"
    pi = Pi(card)
    try:
        out = pi.read_prompt(15)
        check(P, "boot: PC-upgraded v2", f"MyFS mounted from myfs.img, {len(want)} file(s)" in out
              and "old MyFS" not in out, out)
        expect(P, pi, "cat", "cat readme.txt", ["Welcome to MyOS!"])
        expect(P, pi, "fsck", "fsck", ["No problems found."], 30)
        expect(P, pi, "mkdir", "mkdir new", [], absent=["need"])
    finally:
        pi.close()
    check_files(P, card_image(card, layout, "c-after.img"), want, {})


# ------------------------------------------------------------------ D
def comb(fs):
    """one file owning every other pair of free sectors: the free space left is
    nothing but 2-sector holes"""
    xs = []
    k = fs.find_run(1, fs.total, 1)
    while k + 4 <= fs.total:
        if not fs.bit(k) and not fs.bit(k + 1):
            xs.append((k, 2))
            fs.mark(k, 2, 1)
        k += 4
    chains = []
    for _ in range((len(xs) - 6 + 62) // 63):
        c = fs.find_run(1, fs.total, 1)
        fs.mark(c, 1, 1)
        chains.append(c)
    e = myfs.new_entry("comb.bin", myfs.T_FILE, len(xs) * 2 * 512)
    fs.store_extents(e, xs, chains)
    fs.flush_bitmap()
    _, free = fs.find_in(fs.root()[2], "comb.bin")
    fs.put_entry(free[0], free[1], e)
    return len(xs)


def part_d(tmp):
    card = os.path.join(tmp, "d.img")
    pieces = []

    def setup(fs):
        fs.write("/python.pkg", open(os.path.join(PACKAGES, "python.pkg"), "rb").read())
        fs.write("/frag.py", SCRIPT_FRAG)
        pieces.append(comb(fs))

    layout = mksdimg.build(card, [], setup=setup)
    P = "D"
    pi = Pi(card)
    try:
        out = pi.read_prompt(20)
        check(P, f"boot with a {pieces[0]}-piece file", "MyFS mounted from myfs.img, 3 file(s)" in out, out)
        expect(P, pi, "install python into holes", "pkg install python", ["Done."], 30)
        expect(P, pi, "fragmented program runs", "python frag.py", ["frag True 102400"], 60)
        expect(P, pi, "fsck", "fsck", ["No problems found."], 30)
        free0 = free_kb(pi.cmd("ls"))
        expect(P, pi, "delete the comb", "rm comb.bin", ["Removed comb.bin"])
        free1 = free_kb(pi.cmd("ls"))
        check(P, "comb space given back", free1 - free0 >= pieces[0], (free0, free1))
    finally:
        pi.close()
    img = card_image(card, layout, "d-after.img")
    disk = myfs.Disk(img)
    fs = myfs.open_fs(disk)
    data = fs.read("/frag.bin")
    xs, chains = fs.extents(fs.lookup("/frag.bin")[2])
    rep = fs.check(fix=False)
    check(P, "myfs.py reads the pieces", data == b"0123456789abcdef" * 6400 and len(xs) > 69
          and len(chains) >= 1 and not rep["damaged"] and not rep["leaked_kb"], (len(xs), len(chains), rep))
    disk.close()


# ------------------------------------------------------------------ E
def part_e(tmp):
    card = os.path.join(tmp, "e.img")
    lost = 1000

    def setup(fs):
        fs.write("/keep.txt", b"keep me\n")
        s = fs.find_run(fs.hint, fs.total, lost)      # marked used, but no file has them
        fs.mark(s, lost, 1)
        fs.flush_bitmap()

    layout = mksdimg.build(card, [], setup=setup)
    P = "E"
    pi = Pi(card)
    try:
        out = pi.read_prompt(15)
        check(P, "lost space given back at mount", f"Gave back {lost // 2} KB of lost space" in out, out)
        expect(P, pi, "file fine", "cat keep.txt", ["keep me"])
        expect(P, pi, "fsck clean now", "fsck", ["No problems found."], 30)
    finally:
        pi.close()
    disk = myfs.Disk(card_image(card, layout, "e-after.img"))
    rep = myfs.open_fs(disk).check(fix=False)
    check(P, "bitmap on disk fixed", rep["leaked_kb"] == 0, rep)
    disk.close()


def main():
    parts = [p.upper() for p in sys.argv[1:]] or ["A", "B", "C", "D", "E", "F", "G"]
    tmp = tempfile.mkdtemp(prefix="myos-fstest-")
    for p in parts:
        {"A": part_a, "B": part_b, "C": part_c, "D": part_d, "E": part_e, "F": part_f,
         "G": part_g}[p](tmp)
    print(f"{failed} failed" if failed else "all passed")
    return failed


if __name__ == "__main__":
    sys.exit(1 if main() else 0)
