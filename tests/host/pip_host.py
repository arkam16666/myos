"""Run apps/py/pip.py on the PC (CPython) against the real micropython.org index.

myos.fetch is replaced by urllib, /lib by a temporary folder. Checks install
(file and folder packages, a pinned version, files shared by two packages),
SHA-256 checking, list/show/search, uninstall and the error messages.

usage: python tests/host/pip_host.py      (needs the internet)
"""
import io
import os
import shutil
import sys
import tempfile
import types
import urllib.error
import urllib.request
from contextlib import redirect_stdout

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
corrupt = []                                    # URLs whose body gets damaged


def fake_fetch(url, max_size=262144):
    try:
        with urllib.request.urlopen(url, timeout=30) as r:
            status, data = r.status, r.read()
    except urllib.error.HTTPError as e:
        status, data = e.code, b""
    except urllib.error.URLError:
        raise OSError("can't connect to the server")
    if len(data) > max_size:
        raise OSError("too big")
    if url in corrupt:
        data = data[:-1] + b"#"
    return status, data


def offline_fetch(url, max_size=262144):
    raise OSError("no network")


myos = types.ModuleType("myos")
myos.fetch = fake_fetch
sys.modules["myos"] = myos
sys.path.insert(0, os.path.join(ROOT, "apps", "py"))
import pip as P  # noqa: E402  (apps/py/pip.py, not CPython's pip: path[0])
assert P.__file__.endswith(os.path.join("apps", "py", "pip.py")), P.__file__

failed = 0


def run(*args):
    out = io.StringIO()
    sys.argv = ["pip.py"] + list(args)
    code = 0
    with redirect_stdout(out):
        try:
            P.run()
        except SystemExit as e:
            code = e.code
    return code, out.getvalue()


def check(what, ok, out=""):
    global failed
    print(("PASS  " if ok else "FAIL  ") + what)
    if not ok:
        failed += 1
        print("      " + out.replace("\n", "\n      "))


def main():
    tmp = tempfile.mkdtemp(prefix="myos-pip-")
    os.chdir(tmp)
    P.LIB = "lib"
    try:
        code, out = run("list")
        check("empty list", code == 0 and "No libraries installed" in out, out)

        code, out = run("install", "datetime")
        check("install datetime", code == 0 and os.path.isfile("lib/datetime.py")
              and "import datetime" in out, out)
        code, out = run("install", "datetime")
        check("reinstall keeps the file (hash matches, no download)", code == 0 and "bytes)" not in out, out)

        code, out = run("install", "aioble")
        check("folder package aioble", code == 0 and os.path.isfile("lib/aioble/__init__.py")
              and "Note: aioble" in out, out)

        code, out = run("install", "datetime==4.0.0")
        check("pinned version datetime==4.0.0", code == 0 and "datetime 4.0.0" in out, out)
        code, out = run("list")
        check("list shows versions", "datetime                 4.0.0" in out and "aioble" in out, out)

        code, out = run("install", "collections-defaultdict", "collections-deque")
        check("two packages sharing collections/__init__.py", code == 0
              and os.path.isfile("lib/collections/defaultdict.py")
              and os.path.isfile("lib/collections/deque.py"), out)
        code, out = run("uninstall", "collections-deque")
        check("uninstall keeps the shared file", code == 0 and not os.path.exists("lib/collections/deque.py")
              and os.path.isfile("lib/collections/__init__.py"), out)
        code, out = run("uninstall", "collections-defaultdict")
        check("last user gone: folder removed", code == 0 and not os.path.exists("lib/collections"), out)

        code, out = run("show", "aioble")
        check("show", code == 0 and "Version:" in out and "lib/aioble/core.py" in out, out)
        code, out = run("uninstall", "aioble")
        check("uninstall folder package", code == 0 and not os.path.exists("lib/aioble"), out)
        code, out = run("uninstall", "aioble")
        check("uninstall something not installed", code == 1 and "not installed" in out, out)

        code, out = run("install", "numpy")
        check("numpy: explains CPython vs MicroPython", code == 1 and "CPython" in out
              and "micropython-lib" in out, out)
        code, out = run("install", "datetime==0.0.0")
        check("unknown version", code == 1 and "no version 0.0.0" in out, out)
        code, out = run("install", "../evil")
        check("bad name refused", code == 1 and "Bad package name" in out, out)

        code, out = run("search", "date")
        check("search", code == 0 and "datetime" in out, out)

        P.LIB = "lib2"
        meta = fake_fetch(P.INDEX + "/package/py/logging/latest.json")[1]
        h = __import__("json").loads(meta)["hashes"][0][1]
        corrupt.append("%s/file/%s/%s" % (P.INDEX, h[:2], h))
        code, out = run("install", "logging")
        check("damaged download refused", code == 1 and "SHA-256" in out
              and not os.path.exists("lib2/logging.py"), out)
        corrupt.clear()

        myos.fetch = offline_fetch
        code, out = run("install", "logging")
        check("no network: Wi-Fi hint", code == 1 and "Wi-Fi not connected" in out, out)
        code, out = run("help")
        check("help", code == 0 and "pip install NAME" in out, out)
    finally:
        os.chdir(ROOT)
        shutil.rmtree(tmp, ignore_errors=True)
    print("ALL PASSED" if not failed else "%d FAILED" % failed)
    return failed


if __name__ == "__main__":
    sys.exit(1 if main() else 0)
