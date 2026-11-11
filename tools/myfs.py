#!/usr/bin/env python3
"""MyFS disk tool - look inside a MyOS disk image (myfs.img / disk.img) on the PC.

usage:
  python tools/myfs.py myfs.img create [MB] [--v1]   new empty image (default 16 MB, MyFS v2)
  python tools/myfs.py IMG ls [path]                 list a folder (default: /)
  python tools/myfs.py IMG tree [path]               every folder and file below path
  python tools/myfs.py IMG put <file> [path]         copy a PC file onto the disk
                                                     (path may be a folder: /apps/)
  python tools/myfs.py IMG putnew <files...>         copy only files not on the disk yet
  python tools/myfs.py IMG get <path> [out]          copy a file off the disk
  python tools/myfs.py IMG rm <path>                 delete a file
  python tools/myfs.py IMG mkdir <path>              new folder (and its parents)   [v2]
  python tools/myfs.py IMG rmdir <path>              delete an empty folder         [v2]
  python tools/myfs.py IMG mv <from> <to>            rename / move
  python tools/myfs.py IMG info                      format, size, free space
  python tools/myfs.py IMG fsck                      check v2, give back lost space
  python tools/myfs.py IMG upgrade                   v1 -> v2 in place, keeping every file
  python tools/myfs.py IMG import <other.img>        copy everything from another MyFS image
                                                     (e.g. into a new, bigger image)
  python tools/myfs.py IMG format [--v1|--v2]        erase everything (default: keep the
                                                     image's version; a blank image gets v1,
                                                     which is what MyOS PC reads)

Two formats (must match arm/myfs.c; MyOS PC's kernel.asm reads v1 only):

v1 "MYFS0001"
  sector 0      superblock: "MYFS0001", u32 total sectors, u32 next free sector
  sector 1-8    directory: 64 entries x 64 bytes
                  name[48] (zero-terminated), u32 start, u32 size, u32 alloc, u32 flags
  sector 16..   file data, one contiguous run of sectors per file

v2 "MYFS0002" (blocks are 512-byte sectors)
  sector 0      "MYFS0002", u32 total, u32 bitmap start, u32 bitmap sectors, u32 flags;
                bytes 128-255: the entry of the top folder "/"
  bitmap        1 bit per sector, 1 = used (bit s%8 of byte s/8)
  entry         128 bytes: name[64], u32 type (0 free, 1 file, 2 folder), u32 size,
                u32 first extent block, u32 reserved, 6 x (u32 start, u32 count)
  extent block  "MYXT", u32 next block, 63 x (u32 start, u32 count)
  folder        its sectors hold its entries, 4 per sector
"""
import os
import struct
import sys

SECTOR = 512

# ---------------------------------------------------------------- v1
MAGIC = b"MYFS0001"
DIR_START, DIR_SECTORS = 1, 8
DATA_START = 16
ENTRY = struct.Struct("<48sIIII")
MAX_FILES = 64
NAME_MAX = 47

# ---------------------------------------------------------------- v2
MAGIC2 = b"MYFS0002"
ENT = 128
NX_INLINE, NX_BLOCK = 6, 63
T_FREE, T_FILE, T_DIR = 0, 1, 2
NAME_MAX2 = 63
DIR_FIRST, DIR_GROW = 4, 8
MAX_SECTORS = 8 << 20


class FSError(Exception):
    pass


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

    def close(self):
        self.f.close()


def version_of(disk):
    head = disk.read(0)[:8]
    return 2 if head == MAGIC2 else 1 if head == MAGIC else 0


def split_path(path):
    """'/a/./b/../c' -> ['a', 'c']"""
    parts = []
    for p in path.replace("\\", "/").split("/"):
        if p in ("", "."):
            continue
        if p == "..":
            if parts:
                parts.pop()
            continue
        parts.append(p)
    return parts


def check_name(name, limit):
    if not 0 < len(name) <= limit or not all(0x21 <= ord(c) <= 0x7E for c in name) or "/" in name:
        raise FSError(f"Bad name {name!r}: use 1-{limit} ASCII characters, no spaces or '/'")


# ================================================================= v1
def load(disk):
    sb = disk.read(0)
    if sb[:8] != MAGIC:
        sys.exit("This disk has no MyFS v1 file system.")
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


def format_v1(disk):
    empty = [{"name": "", "start": 0, "size": 0, "alloc": 0, "flags": 0}] * MAX_FILES
    save(disk, {"total": disk.total, "next_free": DATA_START}, empty)


class V1:
    """the flat v1 file system with the same methods as V2"""
    version = 1

    def __init__(self, disk):
        self.disk = disk
        self.sb, self.entries = load(disk)

    def _name(self, path):
        parts = split_path(path)
        if not parts:
            raise FSError("that is the top folder")
        if len(parts) > 1:
            raise FSError("MyFS v1 has no folders (run: upgrade)")
        return parts[0]

    def listdir(self, path="/"):
        if split_path(path):
            raise FSError(f"No such folder: {path}")
        return [(e["name"], T_FILE, e["size"]) for e in self.entries if e["flags"] & 1]

    def stat(self, path):
        if not split_path(path):
            return T_DIR, 0
        e = find(self.entries, self._name(path))
        return (T_FILE, e["size"]) if e else None

    def read(self, path):
        e = find(self.entries, self._name(path))
        if e is None:
            raise FSError(f"No such file: {path}")
        return self.disk.read(e["start"], e["alloc"])[:e["size"]]

    def write(self, path, data):
        name = self._name(path)
        check_name(name, NAME_MAX)
        sectors = (len(data) + SECTOR - 1) // SECTOR
        e = find(self.entries, name)
        if e is None:
            free = [i for i, x in enumerate(self.entries) if not x["flags"] & 1]
            if not free:
                raise FSError("Directory is full (64 files max)")
            e = self.entries[free[0]] = {"name": name, "start": 0, "size": 0, "alloc": 0, "flags": 0}
        if e["flags"] & 1 == 0 or sectors > e["alloc"]:
            if self.sb["next_free"] + sectors > self.sb["total"]:
                raise FSError("Disk is full")
            e["start"], e["alloc"] = self.sb["next_free"], sectors
            self.sb["next_free"] += sectors
        e["size"], e["flags"] = len(data), 1
        self.disk.write(e["start"], data.ljust(sectors * SECTOR, b"\0"))
        save(self.disk, self.sb, self.entries)

    def remove(self, path):
        e = find(self.entries, self._name(path))
        if e is None:
            raise FSError(f"No such file: {path}")
        e["flags"] = 0
        save(self.disk, self.sb, self.entries)

    def rename(self, src, dst):
        e = find(self.entries, self._name(src))
        if e is None:
            raise FSError(f"No such file: {src}")
        name = self._name(dst)
        check_name(name, NAME_MAX)
        old = find(self.entries, name)
        if old is not None and old is not e:
            old["flags"] = 0
        e["name"] = name
        save(self.disk, self.sb, self.entries)

    def mkdir(self, path):
        raise FSError("MyFS v1 has no folders (run: upgrade)")

    rmdir = mkdir

    def free_kb(self):
        return (self.sb["total"] - self.sb["next_free"]) // 2

    def total_kb(self):
        return self.sb["total"] // 2


# ================================================================= v2
def _u32(b, off):
    return struct.unpack_from("<I", b, off)[0]


def new_entry(name, kind, size=0):
    e = bytearray(ENT)
    raw = name.encode("ascii")
    e[0:len(raw)] = raw
    struct.pack_into("<II", e, 64, kind, size)
    return e


def ent_name(e):
    return bytes(e[0:64]).split(b"\0")[0].decode("ascii", "replace")


class V2:
    version = 2

    def __init__(self, disk):
        self.disk = disk
        sb = disk.read(0)
        if sb[:8] != MAGIC2:
            raise FSError("not MyFS v2")
        self.total, self.bm_start, self.bm_sectors, self.flags = struct.unpack_from("<IIII", sb, 8)
        if self.bm_sectors != (self.total + 4095) // 4096 or self.total > disk.total:
            raise FSError("damaged v2 superblock")
        self.bm = bytearray(disk.read(self.bm_start, self.bm_sectors))
        self.hint = self.bm_start + self.bm_sectors

    # ---- bitmap
    def bit(self, s):
        return self.bm[s >> 3] >> (s & 7) & 1

    def mark(self, s, n, used):
        for k in range(s, min(s + n, self.total)):
            if used:
                self.bm[k >> 3] |= 1 << (k & 7)
            else:
                self.bm[k >> 3] &= ~(1 << (k & 7)) & 0xFF

    def flush_bitmap(self):
        self.disk.write(self.bm_start, bytes(self.bm))

    def free_sectors(self):
        return sum(1 for k in range(self.total) if not self.bit(k))

    def free_kb(self):
        return self.free_sectors() // 2

    def total_kb(self):
        return self.total // 2

    def find_run(self, start, end, need):
        run = 0
        first = 0
        k = start
        while k < end:
            if k & 7 == 0 and k + 8 <= end and self.bm[k >> 3] == 0xFF:
                run = 0
                k += 8
                continue
            if self.bit(k):
                run = 0
            else:
                if run == 0:
                    first = k
                run += 1
                if run == need:
                    return first
            k += 1
        return 0

    # ---- entries
    def entry_at(self, lba, slot):
        return bytearray(self.disk.read(lba)[slot * ENT:(slot + 1) * ENT])

    def put_entry(self, lba, slot, e):
        sec = bytearray(self.disk.read(lba))
        sec[slot * ENT:(slot + 1) * ENT] = e
        self.disk.write(lba, bytes(sec))

    def extents(self, e):
        """-> ([(start, count)...], [extent block sectors])"""
        xs, chains = [], []
        for i in range(NX_INLINE):
            s, n = struct.unpack_from("<II", e, 80 + i * 8)
            if n == 0:
                return xs, chains
            xs.append((s, n))
        c = _u32(e, 72)
        while c:
            blk = self.disk.read(c)
            if blk[:4] != b"MYXT" or c in chains:
                raise FSError("damaged extent block")
            chains.append(c)
            for i in range(NX_BLOCK):
                s, n = struct.unpack_from("<II", blk, 8 + i * 8)
                if n == 0:
                    return xs, chains
                xs.append((s, n))
            c = _u32(blk, 4)
        return xs, chains

    def sectors_of(self, e):
        for s, n in self.extents(e)[0]:
            yield from range(s, s + n)

    def slots(self, d):
        """every place in folder entry d: (lba, slot, entry)"""
        for lba in self.sectors_of(d):
            sec = self.disk.read(lba)
            for slot in range(4):
                yield lba, slot, bytearray(sec[slot * ENT:(slot + 1) * ENT])

    def root(self):
        return 0, 1, self.entry_at(0, 1)

    def find_in(self, d, name):
        free = None
        for lba, slot, e in self.slots(d):
            t = _u32(e, 64)
            if t == T_FREE:
                free = free or (lba, slot)
            elif ent_name(e) == name:
                return (lba, slot, e), free
        return None, free

    def lookup(self, path):
        loc = self.root()
        for part in split_path(path):
            if _u32(loc[2], 64) != T_DIR:
                raise FSError(f"Not a folder in {path}")
            loc, _ = self.find_in(loc[2], part)
            if loc is None:
                return None
        return loc

    def parent_of(self, path):
        parts = split_path(path)
        if not parts:
            raise FSError("that is the top folder")
        check_name(parts[-1], NAME_MAX2)
        d = self.lookup("/" + "/".join(parts[:-1]))
        if d is None or _u32(d[2], 64) != T_DIR:
            raise FSError(f"No such folder: /{'/'.join(parts[:-1])}")
        return d, parts[-1]

    # ---- allocation
    def alloc(self, need):
        if need == 0:
            return [], []
        s = self.find_run(self.hint, self.total, need) or self.find_run(1, self.total, need)
        if s:
            xs = [(s, need)]
        else:
            xs, left, k = [], need, 1
            while k < self.total and left:
                if self.bit(k):
                    k += 1
                    continue
                n = 0
                while k + n < self.total and n < left and not self.bit(k + n):
                    n += 1
                xs.append((k, n))
                left -= n
                k += n
            if left:
                raise FSError("Disk is full")
        for s, n in xs:
            self.mark(s, n, 1)
        chains = []
        for _ in range(max(0, (len(xs) - NX_INLINE + NX_BLOCK - 1) // NX_BLOCK)):
            c = self.find_run(1, self.total, 1)
            if not c:
                for s, n in xs:
                    self.mark(s, n, 0)
                for c in chains:
                    self.mark(c, 1, 0)
                raise FSError("Disk is full")
            self.mark(c, 1, 1)
            chains.append(c)
        last = xs[-1]
        self.hint = last[0] + last[1] if last[0] + last[1] < self.total else 1
        return xs, chains

    def store_extents(self, e, xs, chains):
        e[72:76] = struct.pack("<I", chains[0] if chains else 0)
        e[80:128] = bytes(48)
        for i, (s, n) in enumerate(xs[:NX_INLINE]):
            struct.pack_into("<II", e, 80 + i * 8, s, n)
        rest = xs[NX_INLINE:]
        for i, c in enumerate(chains):
            blk = bytearray(SECTOR)
            blk[0:4] = b"MYXT"
            struct.pack_into("<I", blk, 4, chains[i + 1] if i + 1 < len(chains) else 0)
            for k, (s, n) in enumerate(rest[i * NX_BLOCK:(i + 1) * NX_BLOCK]):
                struct.pack_into("<II", blk, 8 + k * 8, s, n)
            self.disk.write(c, bytes(blk))

    def free_entry(self, e):
        xs, chains = self.extents(e)
        for s, n in xs:
            self.mark(s, n, 0)
        for c in chains:
            self.mark(c, 1, 0)

    def grow(self, dloc):
        """a full folder gets DIR_GROW more sectors -> (lba, 0) of the first new place"""
        lba, slot, d = dloc
        xs, chains = self.extents(d)
        start = self.find_run(self.hint, self.total, DIR_GROW) or self.find_run(1, self.total, DIR_GROW)
        if not start:
            raise FSError("Disk is full")
        self.mark(start, DIR_GROW, 1)
        self.disk.write(start, bytes(DIR_GROW * SECTOR))
        if xs and xs[-1][0] + xs[-1][1] == start:
            xs[-1] = (xs[-1][0], xs[-1][1] + DIR_GROW)
        else:
            xs.append((start, DIR_GROW))
        for c in chains:
            self.mark(c, 1, 0)
        need = max(0, (len(xs) - NX_INLINE + NX_BLOCK - 1) // NX_BLOCK)
        chains = []
        for _ in range(need):
            c = self.find_run(1, self.total, 1)
            if not c:
                raise FSError("Disk is full")
            self.mark(c, 1, 1)
            chains.append(c)
        self.store_extents(d, xs, chains)
        struct.pack_into("<I", d, 68, sum(n for _, n in xs) * SECTOR)
        self.flush_bitmap()
        self.put_entry(lba, slot, d)
        return start, 0

    # ---- operations
    def stat(self, path):
        loc = self.lookup(path)
        return None if loc is None else (_u32(loc[2], 64), _u32(loc[2], 68))

    def listdir(self, path="/"):
        loc = self.lookup(path)
        if loc is None or _u32(loc[2], 64) != T_DIR:
            raise FSError(f"No such folder: {path}")
        out = []
        for _, _, e in self.slots(loc[2]):
            t = _u32(e, 64)
            if t in (T_FILE, T_DIR):
                out.append((ent_name(e), t, _u32(e, 68)))
        return out

    def read(self, path):
        loc = self.lookup(path)
        if loc is None or _u32(loc[2], 64) != T_FILE:
            raise FSError(f"No such file: {path}")
        e = loc[2]
        data = b"".join(self.disk.read(s, n) for s, n in self.extents(e)[0])
        return data[:_u32(e, 68)]

    def write(self, path, data):
        dloc, name = self.parent_of(path)
        old, free = self.find_in(dloc[2], name)
        if old is not None and _u32(old[2], 64) != T_FILE:
            raise FSError(f"{path} is a folder")
        need = (len(data) + SECTOR - 1) // SECTOR
        reused = False
        try:
            xs, chains = self.alloc(need)
        except FSError:
            if old is None:
                raise
            self.free_entry(old[2])                 # reuse the old file's space
            reused = True
            xs, chains = self.alloc(need)
        pos = 0
        for s, n in xs:
            self.disk.write(s, data[pos:pos + n * SECTOR].ljust(n * SECTOR, b"\0"))
            pos += n * SECTOR
        e = new_entry(name, T_FILE, len(data))
        self.store_extents(e, xs, chains)
        self.flush_bitmap()
        if old is not None:
            self.put_entry(old[0], old[1], e)
            if not reused:
                self.free_entry(old[2])
                self.flush_bitmap()
        else:
            if free is None:
                free = self.grow(dloc)
            self.put_entry(free[0], free[1], e)

    def mkdir(self, path, parents=False):
        parts = split_path(path)
        if parents:
            for i in range(1, len(parts)):
                st = self.stat("/" + "/".join(parts[:i]))
                if st is None:
                    self.mkdir("/" + "/".join(parts[:i]))
                elif st[0] != T_DIR:
                    raise FSError(f"/{'/'.join(parts[:i])} is a file")
            st = self.stat(path) if parts else (T_DIR, 0)
            if st is not None and st[0] == T_DIR:
                return
        dloc, name = self.parent_of(path)
        old, free = self.find_in(dloc[2], name)
        if old is not None:
            raise FSError(f"{path} already exists")
        if len(parts) > 32:
            raise FSError("folders nested too deep")
        xs, chains = self.alloc(DIR_FIRST)
        for s, n in xs:
            self.disk.write(s, bytes(n * SECTOR))
        e = new_entry(name, T_DIR, DIR_FIRST * SECTOR)
        self.store_extents(e, xs, chains)
        self.flush_bitmap()
        if free is None:
            free = self.grow(dloc)
        self.put_entry(free[0], free[1], e)

    def remove(self, path):
        loc = self.lookup(path)
        if loc is None or _u32(loc[2], 64) != T_FILE:
            raise FSError(f"No such file: {path}")
        self.put_entry(loc[0], loc[1], bytes(ENT))
        self.free_entry(loc[2])
        self.flush_bitmap()

    def rmdir(self, path):
        loc = self.lookup(path)
        if loc is None or _u32(loc[2], 64) != T_DIR or loc[0] == 0:
            raise FSError(f"No such folder: {path}")
        if self.listdir(path):
            raise FSError(f"{path} is not empty")
        self.put_entry(loc[0], loc[1], bytes(ENT))
        self.free_entry(loc[2])
        self.flush_bitmap()

    def rename(self, src, dst):
        loc = self.lookup(src)
        if loc is None or loc[0] == 0:
            raise FSError(f"No such file or folder: {src}")
        a, b = "/" + "/".join(split_path(src)), "/" + "/".join(split_path(dst))
        if a == b:
            return
        if _u32(loc[2], 64) == T_DIR and b.startswith(a + "/"):
            raise FSError("a folder can't go inside itself")
        dloc, name = self.parent_of(dst)
        e = bytearray(loc[2])
        e[0:64] = new_entry(name, 0)[0:64]
        old, free = self.find_in(dloc[2], name)
        if old is not None:
            if _u32(old[2], 64) != T_FILE or _u32(loc[2], 64) != T_FILE:
                raise FSError(f"{dst} already exists")
            self.put_entry(old[0], old[1], e)
            self.put_entry(loc[0], loc[1], bytes(ENT))
            self.free_entry(old[2])
            self.flush_bitmap()
            return
        if a.rsplit("/", 1)[0] == b.rsplit("/", 1)[0]:
            self.put_entry(loc[0], loc[1], e)
            return
        if free is None:
            free = self.grow(dloc)
        self.put_entry(free[0], free[1], e)
        self.put_entry(loc[0], loc[1], bytes(ENT))

    def check(self, fix=True):
        """walk every folder, rebuild the bitmap -> report dict"""
        bm = bytearray(len(self.bm))
        for k in range(self.total, len(bm) * 8):
            bm[k >> 3] |= 1 << (k & 7)
        rep = {"files": 0, "dirs": 0, "damaged": []}

        def claim(e, path):
            try:
                xs, chains = self.extents(e)
            except FSError:
                rep["damaged"].append(path)
                return False
            clash = False
            for s, n in xs + [(c, 1) for c in chains]:
                if s == 0 or s + n > self.total:
                    rep["damaged"].append(path)
                    return False
                for k in range(s, s + n):
                    if bm[k >> 3] >> (k & 7) & 1:
                        clash = True
                    bm[k >> 3] |= 1 << (k & 7)
            if clash:
                rep["damaged"].append(path)
            return not clash

        def walk(d, path, depth):
            for _, _, e in self.slots(d):
                t = _u32(e, 64)
                if t == T_FREE:
                    continue
                p = path + "/" + ent_name(e)
                if t not in (T_FILE, T_DIR):
                    rep["damaged"].append(p)
                    continue
                rep["files" if t == T_FILE else "dirs"] += 1
                if claim(e, p) and t == T_DIR and depth < 40:
                    walk(e, p, depth + 1)

        for k in [0] + list(range(self.bm_start, self.bm_start + self.bm_sectors)):
            bm[k >> 3] |= 1 << (k & 7)
        root = self.entry_at(0, 1)
        claim(root, "")
        walk(root, "", 0)
        if rep["damaged"]:
            bm = bytearray(a | b for a, b in zip(bm, self.bm))
        leaked = sum(bin(a & ~b & 0xFF).count("1") for a, b in zip(self.bm, bm))
        missing = sum(bin(b & ~a & 0xFF).count("1") for a, b in zip(self.bm, bm))
        rep["leaked_kb"], rep["missing_kb"] = (leaked + 1) // 2, (missing + 1) // 2
        if fix and bm != self.bm:
            self.bm = bm
            self.flush_bitmap()
        return rep


def format_v2(disk, sectors=None):
    total = min(sectors or disk.total, MAX_SECTORS)
    if total < 64:
        raise FSError("too small for MyFS v2")
    bm_sectors = (total + 4095) // 4096
    root = 1 + bm_sectors
    bm = bytearray(bm_sectors * SECTOR)
    for k in list(range(0, root + DIR_GROW)) + list(range(total, bm_sectors * 4096)):
        bm[k >> 3] |= 1 << (k & 7)
    disk.write(root, bytes(DIR_GROW * SECTOR))
    disk.write(1, bytes(bm))
    sb = bytearray(SECTOR)
    sb[0:8] = MAGIC2
    struct.pack_into("<IIII", sb, 8, total, 1, bm_sectors, 0)
    e = new_entry("", T_DIR, DIR_GROW * SECTOR)
    struct.pack_into("<II", e, 80, root, DIR_GROW)
    sb[ENT:2 * ENT] = e
    disk.write(0, bytes(sb))


def upgrade(disk):
    """v1 -> v2 in place, the same way as arm/myfs.c myfs_upgrade: no data moves,
    the bitmap and "/" go into free space, sector 0 is written last"""
    sb, entries = load(disk)
    total = min(disk.total, MAX_SECTORS)          # the whole image (v1 knew only its old size)
    if total < sb["total"]:
        raise FSError("the image is smaller than its v1 file system says; nothing changed")
    files = [e for e in entries if e["flags"] & 1]
    for e in files:
        need = (e["size"] + SECTOR - 1) // SECTOR
        if need and (need > e["alloc"] or e["start"] < DATA_START or e["start"] + need > total):
            raise FSError(f"v1 entry {e['name']!r} is damaged; nothing changed")
    names = []
    for e in files:
        nm = "".join("_" if c == "/" or not 0x21 <= ord(c) <= 0x7E else c for c in e["name"])
        if nm in ("", ".", ".."):
            nm = "_" + nm
        tries = 0
        while nm in names and tries < 100:
            nm = nm[:NAME_MAX2 - 2] + "~" + str(tries % 10)
            tries += 1
        names.append(nm)
    bm_sectors = (total + 4095) // 4096
    fs = V2.__new__(V2)
    fs.disk, fs.total, fs.bm_sectors = disk, total, bm_sectors
    fs.bm = bytearray(bm_sectors * SECTOR)
    for k in range(total, bm_sectors * 4096):
        fs.bm[k >> 3] |= 1 << (k & 7)
    fs.mark(0, DATA_START, 1)
    for e in files:
        need = (e["size"] + SECTOR - 1) // SECTOR
        for k in range(e["start"], e["start"] + need):
            if fs.bit(k):
                raise FSError("two v1 files share sectors; nothing changed")
        fs.mark(e["start"], need, 1)
    root_sectors = (len(files) + 32 + 3) // 4
    b = fs.find_run(DATA_START, total, bm_sectors)
    if not b:
        raise FSError("no room for the bitmap; delete a file first")
    fs.mark(b, bm_sectors, 1)
    root = fs.find_run(DATA_START, total, root_sectors)
    if not root:
        raise FSError("no room for the folder table; delete a file first")
    fs.mark(root, root_sectors, 1)
    table = bytearray(root_sectors * SECTOR)
    for i, (e, nm) in enumerate(zip(files, names)):
        ent = new_entry(nm, T_FILE, e["size"])
        if e["size"]:
            struct.pack_into("<II", ent, 80, e["start"], (e["size"] + SECTOR - 1) // SECTOR)
        table[i * ENT:(i + 1) * ENT] = ent
    disk.write(root, bytes(table))
    fs.mark(1, DATA_START - 1, 0)
    disk.write(b, bytes(fs.bm))
    head = bytearray(SECTOR)
    head[0:8] = MAGIC2
    struct.pack_into("<IIII", head, 8, total, b, bm_sectors, 1)
    r = new_entry("", T_DIR, root_sectors * SECTOR)
    struct.pack_into("<II", r, 80, root, root_sectors)
    head[ENT:2 * ENT] = r
    disk.write(0, bytes(head))
    return len(files)


def open_fs(disk):
    v = version_of(disk)
    if v == 2:
        return V2(disk)
    if v == 1:
        return V1(disk)
    sys.exit("This disk has no MyFS file system. Run: format")


def read_file(image, path):
    """convenience for tests: the bytes of one file in an image"""
    disk = Disk(image)
    try:
        return open_fs(disk).read(path)
    finally:
        disk.close()


# ================================================================= commands
def create(path, megabytes, version=2):
    """make a new, empty MyFS image file (for the Pi: copy it to the SD card's bootfs)"""
    with open(path, "wb") as f:
        f.truncate(int(megabytes) * 1024 * 1024)
    disk = Disk(path)
    if version == 1:
        format_v1(disk)
    else:
        format_v2(disk)
    disk.close()


def cmd_format(disk, *opts):
    v = 1 if "--v1" in opts else 2 if "--v2" in opts else (version_of(disk) or 1)
    if v == 1:
        format_v1(disk)
        print(f"Formatted (MyFS v1): {(disk.total - DATA_START) // 2} KB free")
    else:
        format_v2(disk)
        print(f"Formatted (MyFS v2): {V2(disk).free_kb()} KB free")


def cmd_info(disk):
    fs = open_fs(disk)
    print(f"MyFS v{fs.version}: {fs.total_kb()} KB, {fs.free_kb()} KB free")


def cmd_ls(disk, path="/"):
    fs = open_fs(disk)
    items = fs.listdir(path)
    for name, t, size in items:
        if t == T_DIR:
            print(f"  {name + '/':<34}<folder>")
        else:
            print(f"  {name:<34}{size} bytes")
    if not items:
        print("  (empty)")
    nf = sum(1 for i in items if i[1] == T_FILE)
    nd = len(items) - nf
    print(f"{nf} file(s), {nd} folder(s), {fs.free_kb()} KB free")


def cmd_tree(disk, path="/"):
    fs = open_fs(disk)

    def show(p, indent):
        for name, t, size in sorted(fs.listdir(p)):
            print("  " * indent + (name + "/" if t == T_DIR else f"{name}  ({size} bytes)"))
            if t == T_DIR:
                show(p.rstrip("/") + "/" + name, indent + 1)
    print(path)
    show(path, 1)


def target_path(fs, src, dest):
    """put/mv/cp: a destination that is a folder means 'inside it'"""
    if dest is None:
        return "/" + os.path.basename(src)
    st = fs.stat(dest) if split_path(dest) else (T_DIR, 0)
    if dest.endswith("/") or (st and st[0] == T_DIR):
        return dest.rstrip("/") + "/" + os.path.basename(src.rstrip("/"))
    return dest


def cmd_put(disk, path, name=None):
    fs = open_fs(disk)
    dest = target_path(fs, path, name)
    try:
        data = open(path, "rb").read()
        if fs.version == 2:
            parts = split_path(dest)
            if len(parts) > 1:
                fs.mkdir("/" + "/".join(parts[:-1]), parents=True)
        fs.write(dest, data)
    except FSError as e:
        sys.exit(str(e))
    print(f"put {path} -> {dest} ({len(data)} bytes)")


def cmd_putnew(disk, *paths):
    """put each file whose name is not on the disk yet (never overwrites)"""
    fs = open_fs(disk)
    existing = {n for n, _, _ in fs.listdir("/")}
    for path in paths:
        if os.path.basename(path) not in existing:
            cmd_put(disk, path)


def cmd_get(disk, name, out=None):
    try:
        data = open_fs(disk).read(name)
    except FSError as e:
        sys.exit(str(e))
    out = out or os.path.basename(name)
    open(out, "wb").write(data)
    print(f"get {name} -> {out} ({len(data)} bytes)")


def _run(fn, msg):
    try:
        fn()
    except FSError as e:
        sys.exit(str(e))
    print(msg)


def cmd_rm(disk, name):
    _run(lambda: open_fs(disk).remove(name), f"removed {name}")


def cmd_mkdir(disk, path):
    def go():
        fs = open_fs(disk)
        if fs.version == 1:
            fs.mkdir(path)
        fs.mkdir(path, parents=True)
    _run(go, f"made folder {path}")


def cmd_rmdir(disk, path):
    _run(lambda: open_fs(disk).rmdir(path), f"removed folder {path}")


def cmd_mv(disk, src, dst):
    fs = open_fs(disk)
    dst = target_path(fs, src, dst)
    _run(lambda: fs.rename(src, dst), f"moved {src} -> {dst}")


def cmd_fsck(disk):
    fs = open_fs(disk)
    if fs.version != 2:
        sys.exit("fsck is for MyFS v2 (this is v1; run: upgrade)")
    rep = fs.check()
    print(f"{rep['files']} file(s), {rep['dirs']} folder(s), {fs.free_kb()} KB free")
    if rep["leaked_kb"] or rep["missing_kb"]:
        print(f"bitmap fixed: {rep['leaked_kb']} KB given back, {rep['missing_kb']} KB marked used")
    for p in rep["damaged"]:
        print(f"damaged: {p}")
    if not rep["damaged"] and not rep["leaked_kb"] and not rep["missing_kb"]:
        print("no problems found")


def cmd_upgrade(disk):
    v = version_of(disk)
    if v == 2:
        sys.exit("already MyFS v2")
    if v != 1:
        sys.exit("no MyFS v1 file system here")
    try:
        n = upgrade(disk)
    except FSError as e:
        sys.exit(str(e))
    print(f"upgraded to MyFS v2: {n} file(s) kept, {V2(disk).free_kb()} KB free")


def cmd_import(disk, other):
    """copy every file and folder of another image into this one"""
    src_disk = Disk(other)
    src, dst = open_fs(src_disk), open_fs(disk)
    count = [0, 0]

    def copy(path):
        for name, t, _ in src.listdir(path):
            p = path.rstrip("/") + "/" + name
            if t == T_DIR:
                if dst.stat(p) is None:
                    dst.mkdir(p)
                count[1] += 1
                copy(p)
            else:
                dst.write(p, src.read(p))
                count[0] += 1
    try:
        copy("/")
    except FSError as e:
        sys.exit(str(e))
    src_disk.close()
    print(f"imported {count[0]} file(s), {count[1]} folder(s) from {other}; {dst.free_kb()} KB free")


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    if sys.argv[2] == "create":
        args = [a for a in sys.argv[3:] if not a.startswith("--")]
        create(sys.argv[1], args[0] if args else 16, 1 if "--v1" in sys.argv else 2)
        disk = Disk(sys.argv[1])
        cmd_info(disk)
        return
    disk = Disk(sys.argv[1])
    cmd, args = sys.argv[2], sys.argv[3:]
    commands = {"format": cmd_format, "ls": cmd_ls, "tree": cmd_tree, "put": cmd_put,
                "putnew": cmd_putnew, "get": cmd_get, "rm": cmd_rm, "mkdir": cmd_mkdir,
                "rmdir": cmd_rmdir, "mv": cmd_mv, "info": cmd_info, "fsck": cmd_fsck,
                "upgrade": cmd_upgrade, "import": cmd_import}
    if cmd not in commands:
        sys.exit(__doc__)
    commands[cmd](disk, *args)


if __name__ == "__main__":
    main()
