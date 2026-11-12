"""stage.py - lay out the 'python3' package (a file tree, see tools/mkpkg.py):
    usr/bin/python3            the program (python-build-standalone, aarch64 musl)
    usr/lib/python3.12/...     the standard library, without what MyOS can't use
CPython finds its library from where the program is (/usr/bin -> /usr/lib).
usage: python ports/python3/stage.py third_party/python3/python OUT_DIR
"""
import os
import shutil
import sys

# not useful on MyOS: GUI (Tk), the test suite, IDLE, building extensions,
# bootstrapping pip from the internet (pip itself stays: it installs wheels from files)
SKIP_DIRS = {"test", "tests", "idle_test", "idlelib", "tkinter", "turtledemo", "ensurepip", "lib2to3",
             "__pycache__", "config-3.12-aarch64-linux-musl"}
SKIP_FILES = ("turtle.py",)


def main():
    src, out = sys.argv[1], sys.argv[2]
    if os.path.exists(out):
        shutil.rmtree(out)
    os.makedirs(os.path.join(out, "usr", "bin"))
    shutil.copyfile(os.path.join(src, "bin", "python3.12"), os.path.join(out, "usr", "bin", "python3"))
    lib = os.path.join(src, "lib", "python3.12")
    n = size = 0
    for top, dirs, files in os.walk(lib):
        dirs[:] = sorted(d for d in dirs if d not in SKIP_DIRS)
        rel = os.path.relpath(top, lib)
        dest = os.path.join(out, "usr", "lib", "python3.12", "" if rel == "." else rel)
        os.makedirs(dest, exist_ok=True)
        for f in sorted(files):
            if f in SKIP_FILES or f.endswith((".pyc", ".a")) or f.startswith("_tkinter"):
                continue
            shutil.copyfile(os.path.join(top, f), os.path.join(dest, f))
            n += 1
            size += os.path.getsize(os.path.join(top, f))
    print(f"  python3: {n} library files, {size // 1024} KB + the program")


if __name__ == "__main__":
    main()
