#!/usr/bin/env python3
"""mkpkg - wrap a MyOS program into a .pkg package.

usage: python tools/mkpkg.py <program> <source.asm> <out.pkg>

The package name is the source file name; the rest comes from
comment lines in the source:
    ; @version 1.0
    ; @description What it does (43 characters max)

Package layout (must match kernel.asm):
  0   "MYPK"
  4   u32 format version (1)
  8   name[16]           zero-terminated
  24  version[16]
  40  description[64]
  104 u32 program size
  128 the program ("MYEX" ...)
"""
import os
import re
import struct
import sys

HEADER = struct.Struct("<4sI16s16s64sI")
HEADER_SIZE = 128


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    prog_path, src_path, out_path = sys.argv[1:]
    program = open(prog_path, "rb").read()
    if program[:4] != b"MYEX":
        sys.exit(f"{prog_path} is not a MyOS program (no MYEX header)")

    meta = {"version": "1.0", "description": ""}
    for line in open(src_path, encoding="utf-8"):
        m = re.match(r"\s*(?:;|//)\s*@(\w+)\s+(.*)", line)
        if m and m.group(1) in meta:
            meta[m.group(1)] = m.group(2).strip()

    name = os.path.splitext(os.path.basename(src_path))[0]
    if not re.fullmatch(r"[a-z0-9_-]{1,15}", name):
        sys.exit(f"Bad package name {name!r}: 1-15 of a-z 0-9 - _")
    for field, limit in (("version", 15), ("description", 43)):
        if len(meta[field]) > limit or not meta[field].isascii():
            sys.exit(f"{src_path}: @{field} must be ASCII, {limit} characters max")

    header = HEADER.pack(b"MYPK", 1, name.encode(), meta["version"].encode(),
                         meta["description"].encode(), len(program))
    with open(out_path, "wb") as f:
        f.write(header.ljust(HEADER_SIZE, b"\0") + program)


if __name__ == "__main__":
    main()
