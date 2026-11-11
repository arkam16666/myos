# pip - install Python libraries from micropython-lib over the internet
#   pip install NAME [NAME==VERSION ...]   download into /lib (with what it needs)
#   pip uninstall NAME ...                 remove
#   pip list                               what is installed
#   pip show NAME                          version and files
#   pip search [WORD]                      what micropython-lib has
# Packages come from the index MicroPython's own 'mip' uses (source .py files,
# each checked against the SHA-256 in the package list). Afterwards
# 'import NAME' works, because MyOS's python looks in /lib too (python 1.7+).
# @version 1.0
# @description Install Python libraries (micropython-lib)
import sys
import os
try:                        # u-names: the built-in modules, even if /lib has a json.py
    import ujson as json
    import uhashlib as hashlib
    import ubinascii as binascii
except ImportError:
    try:
        import json
        import hashlib
        import binascii
    except ImportError:     # python before 1.7: no hashlib, and no /lib in sys.path
        print("pip needs python 1.7 or newer. Type: pkg upgrade")
        sys.exit(1)

INDEX = "https://micropython.org/pi/v2"
LIB = "/lib"
PROG = "mip" if sys.argv and "mip" in sys.argv[0] else "pip"

# PyPI names that are CPython-only (C code or a big OS underneath)
CPYTHON = ("numpy", "scipy", "pandas", "matplotlib", "flask", "django", "fastapi", "pillow", "pil",
           "opencv-python", "cv2", "tensorflow", "torch", "keras", "pygame", "beautifulsoup4", "bs4",
           "lxml", "scikit-learn", "sklearn", "selenium", "pytest", "cryptography", "psutil",
           "pyserial", "serial", "pyyaml", "yaml", "jupyter", "ipython", "sympy", "httpx",
           "urllib3", "aiofiles", "tkinter", "pyqt5", "pyside6", "kivy", "numba", "cython")
# micropython-lib packages that install fine but need things MyOS doesn't have (yet)
NEEDS_SOCKET = ("requests", "urequests", "aiohttp", "uaiohttpclient", "umqtt.simple", "umqtt.robust",
                "ntptime", "webrepl", "iperf3", "urllib.urequest", "bundle-networking", "aiorepl",
                "aioespnow", "espflash", "udnspkt")
HARDWARE = ("aioble", "neopixel", "dht", "ds18x20", "onewire", "ssd1306", "sdcard", "lora", "usb-device",
            "nrf24l01", "lcd160cr", "bmi270", "bmm150", "lsm6dsox", "lsm9ds1", "lps22h", "hts221",
            "hs3003", "mhz19", "wm8960")


class Fail(Exception):      # a message for the user (no traceback)
    pass


# ---------------------------------------------------------------- downloads

def net_hint(err):
    msg = str(err)
    if "no network" in msg or "DNS" in msg or "connect" in msg:
        return msg + ". Wi-Fi not connected? Try 'wifi' (or 'wifiman')."
    if "HTTPS" in msg:
        return msg + ". Try again; if it keeps failing, MyOS may be too old (micropython.org needs TLS 1.2)."
    return msg


def fetch(url, max_size=65536):
    """(status, bytes) of url; Fail with a readable reason when there's no answer"""
    import myos
    try:
        return myos.fetch(url, max_size)
    except OSError as e:
        if "too big" in str(e) and max_size < 256 * 1024:
            return fetch(url, max_size * 4)
        raise Fail("Can't download %s: %s" % (url, net_hint(e)))


def sha256_hex(data):
    return binascii.hexlify(hashlib.sha256(data).digest()).decode()


def split_spec(spec):
    """'datetime==4.0.0' / 'datetime@4.0.0' / 'datetime' -> (name, version or None)"""
    for sep in ("==", "@"):
        if sep in spec:
            name, version = spec.split(sep, 1)
            return name.strip(), version.strip() or None
    return spec.strip(), None


def valid_name(name):
    if not name or len(name) > 60 or name[0] in ".-":
        return False
    for c in name:
        if not (c.isalpha() or c.isdigit() or c in "._-"):
            return False
    return True


def safe_path(path):
    """a file path from a package list: relative, no '..', short enough for MyFS"""
    if not path or path[0] == "/" or "\\" in path or len(path) > 180:
        return False
    for part in path.split("/"):
        if part in ("", ".", "..") or len(part) > 63:
            return False
    return True


# ---------------------------------------------------------------- the disk

def exists(path):
    try:
        os.stat(path)
        return True
    except OSError:
        return False


def is_dir(path):
    try:
        return os.stat(path)[0] & 0x4000 != 0
    except OSError:
        return False


def makedirs(path):
    """make every folder of path (a folder path, absolute)"""
    cur = ""
    for part in path.split("/"):
        if not part:
            continue
        cur = (cur + "/" + part) if cur or path.startswith("/") else part
        if is_dir(cur):
            continue
        try:
            os.mkdir(cur)
        except OSError as e:
            if exists(cur):
                raise Fail("%s is a file, but pip needs a folder there." % cur)
            if e.args and e.args[0] == 1:           # EPERM: MyFS v1, no folders
                raise Fail("Libraries need folders, and this disk is the old MyFS v1.\n"
                           "Type 'fsupgrade' (keeps every file), then try again.")
            raise Fail("Can't make the folder %s (disk full?)" % cur)


def read_bytes(path):
    with open(path, "rb") as f:
        return f.read()


def write_bytes(path, data):
    try:
        with open(path, "wb") as f:
            f.write(data)
    except OSError:
        raise Fail("Can't write %s (disk full?)" % path)


def db_path():
    return LIB + "/.pip.json"


def load_db():
    try:
        db = json.loads(read_bytes(db_path()))
        return db if isinstance(db, dict) else {}
    except (OSError, ValueError):
        return {}


def save_db(db):
    makedirs(LIB)
    write_bytes(db_path(), json.dumps(db).encode())


# ---------------------------------------------------------------- install

def explain_missing(name, version):
    if version:
        print("%s has no version %s in micropython-lib. Versions: pip search %s" % (name, version, name))
        return
    low = name.lower()
    print("%s is not in micropython-lib." % name)
    if low in CPYTHON:
        print("It is a CPython package from PyPI (often with parts written in C).")
    print("MyOS runs MicroPython: PyPI packages made for CPython (numpy, flask ...)")
    print("don't work here. pip installs pure-Python libraries from micropython-lib.")
    print("See what there is: %s search   (or %s search WORD)" % (PROG, PROG))


def note_for(name):
    if name in NEEDS_SOCKET:
        return ("needs the 'socket' module, which MyOS's MicroPython doesn't have yet.\n"
                "      To download from Python use myos.fetch(url) instead.")
    for h in HARDWARE:
        if name == h or name.startswith(h + "-"):
            return "is for microcontroller hardware (machine/bluetooth modules): not on MyOS."
    if name in ("mip", "mip-cmdline"):
        return "is MicroPython's own installer; on MyOS use pip (or mip) instead."
    return None


def get_file(short_hash, dest):
    """download one file of a package and check it against its hash"""
    url = "%s/file/%s/%s" % (INDEX, short_hash[:2], short_hash)
    status, data = fetch(url)
    if status != 200:
        raise Fail("The server doesn't have %s (HTTP %d)." % (dest, status))
    if sha256_hex(data)[:len(short_hash)] != short_hash:
        raise Fail("%s: the download is damaged (SHA-256 doesn't match). Try again." % dest)
    return data


def install(spec, db, seen, depth=0):
    """install one package and its dependencies; True if it worked"""
    name, version = split_spec(spec)
    if not valid_name(name) or (version and not valid_name(version)):
        raise Fail("Bad package name: %s" % spec)
    if name in seen:
        return True
    seen.append(name)
    if depth > 8:
        raise Fail("Dependencies go too deep (a loop?) at %s." % name)
    status, body = fetch("%s/package/py/%s/%s.json" % (INDEX, name, version or "latest"))
    if status == 404:
        explain_missing(name, version)
        return False
    if status != 200:
        raise Fail("micropython.org answered HTTP %d for %s. Try again later." % (status, name))
    try:
        meta = json.loads(body)
    except ValueError:
        raise Fail("The package list for %s is damaged. Try again." % name)
    del body
    new_version = str(meta.get("version", version or "?"))
    old = db.get(name)
    print("%s %s %s ..." % ("Updating" if old else "Installing", name, new_version))
    files = []
    for item in meta.get("hashes", ()):
        path, short = item[0], item[1]
        if not safe_path(path) or not valid_name(short):
            raise Fail("%s lists a strange file name: %s" % (name, path))
        dest = LIB + "/" + path
        if "/" in path:
            makedirs(LIB + "/" + path.rsplit("/", 1)[0])
        else:
            makedirs(LIB)
        try:
            same = sha256_hex(read_bytes(dest))[:len(short)] == short
        except OSError:
            same = False
        if not same:
            data = get_file(short, path)
            write_bytes(dest, data)
            print("  %s  (%d bytes)" % (dest, len(data)))
            del data
        files.append(path)
    for item in meta.get("urls", ()):
        print("  skipped %s: not from the index (no checksum)" % item[0])
    deps = []
    for item in meta.get("deps", ()):
        dep = item[0]
        dep_version = item[1] if len(item) > 1 else None
        if ":" in dep or "/" in dep:
            print("  skipped dependency %s: not in micropython-lib" % dep)
            continue
        deps.append(dep)
        if not install(dep + ("==" + dep_version if dep_version and dep_version != "latest" else ""),
                       db, seen, depth + 1):
            raise Fail("%s needs %s, which couldn't be installed." % (name, dep))
    if old:                                         # files the old version had but the new one hasn't
        remove_files([f for f in old.get("files", ()) if f not in files], db, name)
    db[name] = {"version": new_version, "files": files, "deps": deps}
    save_db(db)
    note = note_for(name)
    if note:
        print("Note: %s %s" % (name, note))
    return True


# ---------------------------------------------------------------- uninstall

def remove_files(files, db, skip):
    """delete files no other installed package uses, then empty folders"""
    used = []
    for other, info in db.items():
        if other != skip:
            used.extend(info.get("files", ()))
    folders = []
    for f in files:
        if f in used or not safe_path(f):
            continue
        try:
            os.remove(LIB + "/" + f)
        except OSError:
            pass
        parts = f.split("/")[:-1]
        while parts:
            d = LIB + "/" + "/".join(parts)
            if d not in folders:
                folders.append(d)
            parts.pop()
    folders.sort(key=len, reverse=True)             # deepest first
    for d in folders:
        try:
            if not os.listdir(d):
                os.rmdir(d)
        except OSError:
            pass


def uninstall(name, db):
    info = db.get(name)
    if info is None:
        print("%s is not installed (see: %s list)" % (name, PROG))
        return False
    remove_files(info.get("files", ()), db, name)
    del db[name]
    save_db(db)
    print("Removed %s %s" % (name, info.get("version", "")))
    needed = [o for o, i in db.items() if name in i.get("deps", ())]
    if needed:
        print("Note: %s needed it." % ", ".join(needed))
    return True


# ---------------------------------------------------------------- list, show, search

def cmd_list(db):
    if not db:
        print("No libraries installed yet. Try: %s install datetime" % PROG)
        return
    print("%-24s %s" % ("Package", "Version"))
    print("%-24s %s" % ("-" * 22, "-" * 10))
    for name in sorted(db):
        print("%-24s %s" % (name, db[name].get("version", "?")))
    print("(in %s)" % LIB)


def cmd_show(name, db):
    info = db.get(name)
    if info is None:
        print("%s is not installed. Install it: %s install %s" % (name, PROG, name))
        return False
    print("Name: %s" % name)
    print("Version: %s" % info.get("version", "?"))
    print("Location: %s" % LIB)
    print("Requires: %s" % (", ".join(info.get("deps", ())) or "-"))
    print("Required-by: %s" % (", ".join(o for o, i in sorted(db.items()) if name in i.get("deps", ())) or "-"))
    print("Files:")
    for f in info.get("files", ()):
        print("  %s/%s" % (LIB, f))
    return True


def cmd_search(word):
    status, body = fetch(INDEX + "/index.json", 256 * 1024)
    if status != 200:
        raise Fail("micropython.org answered HTTP %d. Try again later." % status)
    packages = json.loads(body).get("packages", ())
    del body
    word = word.lower()
    found = 0
    for p in packages:
        name = p.get("name", "")
        text = name + " " + p.get("description", "")
        if word in text.lower() and "py" in p.get("versions", {"py": 1}):
            found += 1
            line = "%-24s %-8s %s" % (name, p.get("version", ""), p.get("description", ""))
            print(line[:79])
            if word and word == name.lower():
                print("  versions: %s" % " ".join(p.get("versions", {}).get("py", ())))
    print("%d package(s). Install: %s install NAME" % (found, PROG))


USAGE = """%s - install Python libraries from micropython-lib (MicroPython)
  %s install NAME ...         download NAME (and what it needs) into /lib
  %s install NAME==VERSION    a certain version
  %s uninstall NAME ...       remove
  %s list                     installed libraries
  %s show NAME                version, files
  %s search [WORD]            what micropython-lib has
Then in Python: import NAME. Needs Wi-Fi (type 'wifi')."""


def import_name(name, files):
    """the module a package gives: 'umqtt.simple' -> umqtt.simple, 'os-path' -> os.path"""
    want = name.replace("-", ".")
    for f in files:
        if f.endswith("/__init__.py"):
            f = f[:-12]
        elif f.endswith(".py"):
            f = f[:-3]
        if f.replace("/", ".") == want:
            return want
    return None


def main(args):
    cmd = args[0] if args else "help"
    rest = args[1:]
    db = load_db()
    if cmd in ("help", "-h", "--help"):
        print(USAGE.replace("%s", PROG))
    elif cmd == "install":
        names = [a for a in rest if not a.startswith("-")]
        if not names:
            print("Usage: %s install NAME [NAME ...]" % PROG)
            return 1
        ok = True
        for spec in names:
            seen = []
            if install(spec, db, seen):
                name = split_spec(spec)[0]
                module = import_name(name, db[name].get("files", ()))
                print("Done." + (" Use it in Python: import " + module if module else ""))
            else:
                ok = False
        return 0 if ok else 1
    elif cmd in ("uninstall", "remove"):
        names = [a for a in rest if not a.startswith("-")]
        if not names:
            print("Usage: %s uninstall NAME [NAME ...]" % PROG)
            return 1
        ok = True
        for name in names:
            ok = uninstall(name, db) and ok
        return 0 if ok else 1
    elif cmd in ("list", "freeze"):
        cmd_list(db)
    elif cmd in ("show", "info"):
        if not rest:
            print("Usage: %s show NAME" % PROG)
            return 1
        ok = True
        for name in rest:
            ok = cmd_show(name, db) and ok
        return 0 if ok else 1
    elif cmd == "search":
        cmd_search(" ".join(rest))
    else:
        print("Unknown command '%s'. Type: %s help" % (cmd, PROG))
        return 1
    return 0


def run():
    try:
        code = main(sys.argv[1:])
    except Fail as e:
        print(e.args[0] if e.args else "failed")
        code = 1
    except MemoryError:
        print("Out of memory. Install fewer packages at a time.")
        code = 1
    if code:
        sys.exit(code)


if __name__ == "__main__":
    run()
