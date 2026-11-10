#!/usr/bin/env python3
"""MyFS disk tool - look inside the MyOS hard disk from Windows.

usage:
  python tools/myfs.py disk.img ls
  python tools/myfs.py disk.img put <file> [name]   copy a Windows file onto the disk
  python tools/myfs.py disk.img putnew <files...>   copy only files not on the disk yet
  python tools/myfs.py disk.img get <name> [out]    copy a file off the disk
  python tools/myfs.py disk.img rm <name>
  python tools/myfs.py disk.img format              erase everything

Layout (must match kernel.asm):
  sector 0      superblock: "MYFS0001", u32 total sectors, u32 next free sector
  sector 1-8    directory: 64 entries x 64 bytes
                  name[48] (zero-terminated), u32 start, u32 size, u32 alloc, u32 flags
  sector 16..   file data, one contiguous run of sectors per file
"""
import os
import struct
import sys

SECTOR = 512
MAGIC = b"MYFS0001"
DIR_START, DIR_SECTORS = 1, 8
DATA_START = 16
ENTRY = struct.Struct("<48sIIII")
MAX_FILES = 64
NAME_MAX = 47


class Disk:
    def __init__(self, path):
        self.f = open(path, "r+b")
        self.f.seek(0, 2)
        self.total = self.f.tell() // SECTOR

    def read(self, lba, count=1):
        self.f.seek(lba * SECTOR)
        return self.f.read(count * SECTOR)

    def write(self, lba, data):
        self.f.seek(lba * SECTOR)
        self.f.write(data)


def load(disk):
    sb = disk.read(0)
    if sb[:8] != MAGIC:
        sys.exit("This disk has no MyFS file system. Run: format")
    total, next_free = struct.unpack_from("<II", sb, 8)
    raw = disk.read(DIR_START, DIR_SECTORS)
    entries = []
    for i in range(MAX_FILES):
        name, start, size, alloc, flags = ENTRY.unpack_from(raw, i * ENTRY.size)
        entries.append({"name": name.split(b"\0")[0].decode("ascii", "replace"),
                        "start": start, "size": size, "alloc": alloc, "flags": flags})
    return {"total": total, "next_free": next_free}, entries


def save(disk, sb, entries):
    head = MAGIC + struct.pack("<II", sb["total"], sb["next_free"])
    disk.write(0, head.ljust(SECTOR, b"\0"))
    raw = b"".join(ENTRY.pack(e["name"].encode("ascii"), e["start"], e["size"],
                              e["alloc"], e["flags"]) for e in entries)
    disk.write(DIR_START, raw)


def find(entries, name):
    for e in entries:
        if e["flags"] & 1 and e["name"] == name:
            return e
    return None


def cmd_format(disk):
    empty = [{"name": "", "start": 0, "size": 0, "alloc": 0, "flags": 0}] * MAX_FILES
    save(disk, {"total": disk.total, "next_free": DATA_START}, empty)
    print(f"Formatted: {(disk.total - DATA_START) // 2} KB free")


def cmd_ls(disk):
    sb, entries = load(disk)
    used = [e for e in entries if e["flags"] & 1]
    for e in used:
        print(f"  {e['name']:<34}{e['size']} bytes")
    if not used:
        print("  (no files)")
    print(f"{len(used)} file(s), {(sb['total'] - sb['next_free']) // 2} KB free")


def cmd_put(disk, path, name=None):
    name = name or os.path.basename(path)
    if not name.isascii() or " " in name or not 0 < len(name) <= NAME_MAX:
        sys.exit(f"Bad name {name!r}: use 1-{NAME_MAX} ASCII characters, no spaces")
    data = open(path, "rb").read()
    sectors = (len(data) + SECTOR - 1) // SECTOR
    sb, entries = load(disk)
    e = find(entries, name)
    if e is None:
        free = [i for i, x in enumerate(entries) if not x["flags"] & 1]
        if not free:
            sys.exit("Directory is full (64 files max)")
        e = entries[free[0]] = {"name": name, "start": 0, "size": 0, "alloc": 0, "flags": 0}
    if e["flags"] & 1 == 0 or sectors > e["alloc"]:
        if sb["next_free"] + sectors > sb["total"]:
            sys.exit("Disk is full")
        e["start"], e["alloc"] = sb["next_free"], sectors
        sb["next_free"] += sectors
    e["size"], e["flags"] = len(data), 1
    disk.write(e["start"], data.ljust(sectors * SECTOR, b"\0"))
    save(disk, sb, entries)
    print(f"put {path} -> {name} ({len(data)} bytes)")


def cmd_putnew(disk, *paths):
    """put each file whose name is not on the disk yet (never overwrites)"""
    _, entries = load(disk)
    existing = {e["name"] for e in entries if e["flags"] & 1}
    for path in paths:
        if os.path.basename(path) not in existing:
            cmd_put(disk, path)


def cmd_get(disk, name, out=None):
    _, entries = load(disk)
    e = find(entries, name)
    if e is None:
        sys.exit(f"No such file: {name}")
    data = disk.read(e["start"], e["alloc"])[:e["size"]]
    open(out or name, "wb").write(data)
    print(f"get {name} -> {out or name} ({len(data)} bytes)")


def cmd_rm(disk, name):
    sb, entries = load(disk)
    e = find(entries, name)
    if e is None:
        sys.exit(f"No such file: {name}")
    e["flags"] = 0
    save(disk, sb, entries)
    print(f"removed {name}")


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    disk = Disk(sys.argv[1])
    cmd, args = sys.argv[2], sys.argv[3:]
    commands = {"format": cmd_format, "ls": cmd_ls, "put": cmd_put, "putnew": cmd_putnew,
                "get": cmd_get, "rm": cmd_rm}
    if cmd not in commands:
        sys.exit(__doc__)
    commands[cmd](disk, *args)


if __name__ == "__main__":
    main()
