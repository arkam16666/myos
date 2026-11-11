#!/usr/bin/env python3
"""mkpkg - wrap a MyOS program or Python app into a .pkg package.

usage: python tools/mkpkg.py <program> <source> <out.pkg>
       python tools/mkpkg.py <app.py> <app.py> <out.pkg>      (Python app)

The package name is the source file name; the rest comes from
comment lines in the source (';', '//' or '#'):
    // @version 1.0
    // @description What it does (43 characters max)

Package layout (must match kernel.asm and arm/pkg.c):
  0   "MYPK"
  4   u32 kind: 1 = program ("MYEX" ...), 2 = Python app (.py source)
  8   name[16]           zero-terminated
  24  version[16]
  40  description[64]
  104 u32 content size
  128 the content
"""
import os
import re
import struct
import sys

HEADER = struct.Struct("<4sI16s16s64sI")
HEADER_SIZE = 128
KIND_PROGRAM, KIND_PYTHON = 1, 2


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
    content = open(prog_path, "rb").read()
    if prog_path.endswith(".py"):
        kind = KIND_PYTHON
        compile(content, prog_path, "exec")         # catch syntax errors now
    elif content[:4] == b"MYEX":
        kind = KIND_PROGRAM
    else:
        sys.exit(f"{prog_path} is not a MyOS program (no MYEX header) or a .py file")
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
