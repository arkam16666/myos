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

# small changes to the standard library so it works on MyOS (each is checked:
# a newer Python whose code moved fails here instead of quietly not working)
PATCHES = {
    "venv/__init__.py": [
        # MyFS has no symbolic links: 'python3 -m venv' always makes lib64 -> lib
        # on 64-bit Linux, and stopped there with EPERM
        ("""            if not os.path.exists(link_path):   # Issue #21643
                os.symlink('lib', link_path)""",
         """            if not os.path.exists(link_path):   # Issue #21643
                try:
                    os.symlink('lib', link_path)
                except OSError:                 # MyOS: no symbolic links (lib64 isn't needed)
                    pass"""),
        # ... and copying the program is the way there (no 'Unable to symlink' warnings)
        ("""    if os.name == 'nt':
        use_symlinks = False
    else:
        use_symlinks = True""",
         """    use_symlinks = False                # MyOS: no symbolic links, copy the program"""),
        # ensurepip isn't in MyOS's python3 (it bootstraps pip from bundled wheels):
        # make the environment without a pip of its own instead of failing
        ("""        \"\"\"Installs or upgrades pip in a virtual environment\"\"\"
        self._call_new_python(""",
         """        \"\"\"Installs or upgrades pip in a virtual environment\"\"\"
        import importlib.util
        if importlib.util.find_spec('ensurepip') is None:      # MyOS
            print('Note: no ensurepip here, so this environment has no pip of its own '
                  '(make it with --system-site-packages to use python3\\'s pip).', file=sys.stderr)
            return
        self._call_new_python("""),
    ],
}


def patch(path, rel):
    text = open(path, encoding="utf-8").read()
    for old, new in PATCHES[rel]:
        if text.count(old) != 1:
            sys.exit(f"stage.py: can't patch {rel} (the Python version changed?): {old[:60]!r}")
        text = text.replace(old, new)
    open(path, "w", encoding="utf-8", newline="\n").write(text)


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
            rel_file = os.path.relpath(os.path.join(top, f), lib).replace(os.sep, "/")
            if rel_file in PATCHES:
                patch(os.path.join(dest, f), rel_file)
            n += 1
            size += os.path.getsize(os.path.join(top, f))
    print(f"  python3: {n} library files, {size // 1024} KB + the program")


if __name__ == "__main__":
    main()
