#!/usr/bin/env python3
"""mkpkg - wrap a MyOS program or Python app into a .pkg package.

usage: python tools/mkpkg.py <program> <source> <out.pkg>
       python tools/mkpkg.py <linux-elf> <info file> <out.pkg>  (Linux program)
       python tools/mkpkg.py <app.py> <app.py> <out.pkg>      (Python app)
       python tools/mkpkg.py <folder> <info file> <out.pkg>     (file tree: unpacked from '/')

The package name is the source file name; the rest comes from
comment lines in the source (';', '//' or '#'):
    // @version 1.0
    // @description What it does (43 characters max)

Package layout (must match kernel.asm and arm/pkg.c):
  0   "MYPK"
  4   u32 kind: 1 = program ("MYEX" ...), 2 = Python app (.py source),
              3 = Linux program (static AArch64 ELF file, run by arm/linux.c)
              4 = file tree (gzip'd ustar tar of the folder's contents, e.g.
                  usr/bin/python3 ...; arm/pkg.c unpacks it from '/')
  8   name[16]           zero-terminated
  24  version[16]
  40  description[64]
  104 u32 content size
  128 the content
"""
import os
import re
import gzip
import io
import struct
import sys
import tarfile

HEADER = struct.Struct("<4sI16s16s64sI")
HEADER_SIZE = 128
KIND_PROGRAM, KIND_PYTHON, KIND_LINUX, KIND_TREE = 1, 2, 3, 4
NAME_MAX, PATH_MAX = 63, 250            # MyFS v2: one name, a whole path ("/" + it < 256)


def make_tree(folder):
    """the folder's files as a gzip'd ustar tar (reproducible: no dates, owners)"""
    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w", format=tarfile.USTAR_FORMAT) as tar:
        for top, dirs, files in os.walk(folder):
            dirs.sort()
            for name in sorted(dirs) + sorted(files):
                full = os.path.join(top, name)
                rel = os.path.relpath(full, folder).replace(os.sep, "/")
                if len(name) > NAME_MAX or len(rel) > PATH_MAX or not rel.isascii():
                    sys.exit(f"{rel}: too long (or not ASCII) for MyFS")
                info = tarfile.TarInfo(rel)
                info.mtime, info.uid, info.gid, info.uname, info.gname = 0, 0, 0, "", ""
                if os.path.isdir(full):
                    info.type, info.mode = tarfile.DIRTYPE, 0o755
                    tar.addfile(info)
                else:
                    data = open(full, "rb").read()
                    info.size, info.mode = len(data), 0o755 if os.access(full, os.X_OK) else 0o644
                    tar.addfile(info, io.BytesIO(data))
    out = io.BytesIO()
    with gzip.GzipFile(fileobj=out, mode="wb", compresslevel=9, mtime=0) as gz:
        gz.write(buf.getvalue())
    return out.getvalue()


def read_meta(src_path):
    meta = {"version": "1.0", "description": ""}
    for line in open(src_path, encoding="utf-8"):
        m = re.match(r"\s*(?:;|//|#)\s*@(\w+)\s+(.*)", line)
        if m and m.group(1) in meta:
            meta[m.group(1)] = m.group(2).strip()
    for field, limit in (("version", 15), ("description", 43)):
        if len(meta[field]) > limit or not meta[field].isascii():
            sys.exit(f"{src_path}: @{field} must be ASCII, {limit} characters max")
    return meta


def make_package(prog_path, src_path):
    """Return the bytes of the package."""
    content, kind = (make_tree(prog_path), KIND_TREE) if os.path.isdir(prog_path) else (open(prog_path, "rb").read(), 0)
    if kind == KIND_TREE:
        pass
    elif prog_path.endswith(".py"):
        kind = KIND_PYTHON
        compile(content, prog_path, "exec")         # catch syntax errors now
    elif content[:4] == b"MYEX":
        kind = KIND_PROGRAM
    elif content[:4] == b"\x7fELF":
        if content[4] != 2 or content[5] != 1 or content[18] != 183:
            sys.exit(f"{prog_path} is an ELF file, but not for 64-bit ARM (AArch64) Linux")
        kind = KIND_LINUX
    else:
        sys.exit(f"{prog_path} is not a MyOS program (no MYEX header), a Linux ELF or a .py file")
    meta = read_meta(src_path)
    name = os.path.splitext(os.path.basename(src_path))[0]
    if not re.fullmatch(r"[a-z0-9_-]{1,15}", name):
        sys.exit(f"Bad package name {name!r}: 1-15 of a-z 0-9 - _")
    header = HEADER.pack(b"MYPK", kind, name.encode(), meta["version"].encode(),
                         meta["description"].encode(), len(content))
    return header.ljust(HEADER_SIZE, b"\0") + content


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    prog_path, src_path, out_path = sys.argv[1:]
    data = make_package(prog_path, src_path)
    with open(out_path, "wb") as f:
        f.write(data)


if __name__ == "__main__":
    main()
