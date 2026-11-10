#!/usr/bin/env python3
"""mksdimg - build a small SD card image laid out like a Raspberry Pi card,
for testing MyOS-ARM in QEMU:

  sector 0       MBR with one FAT32 partition ("bootfs")
  sector 2048..  FAT32 file system holding MYFS.IMG (one unbroken run of clusters)

usage: python tools/mksdimg.py <out.img> [file ...]
The files are put inside MYFS.IMG. Prints the sector range of MYFS.IMG.
"""
import os
import struct
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import myfs  # noqa: E402

SECTOR = 512
CARD_MB = 64            # QEMU wants a power of two
PART_START = 2048
RESERVED = 32
MYFS_MB = 8


def build(out_path, files=()):
    total = CARD_MB * 1024 * 1024 // SECTOR
    part_sectors = total - PART_START
    # FAT32 with one sector per cluster
    clusters = part_sectors - RESERVED
    fat_size = (clusters * 4 + SECTOR - 1) // SECTOR + 1
    clusters = part_sectors - RESERVED - 2 * fat_size
    data_start = PART_START + RESERVED + 2 * fat_size

    # MYFS.IMG contents: a MyFS image made with tools/myfs.py
    tmp = os.path.join(tempfile.mkdtemp(), "myfs.img")
    myfs.create(tmp, MYFS_MB)
    disk = myfs.Disk(tmp)
    for f in files:
        myfs.cmd_put(disk, f)
    disk.f.close()
    content = open(tmp, "rb").read()
    file_clusters = len(content) // SECTOR

    img = bytearray(total * SECTOR)

    def put(lba, data):
        img[lba * SECTOR:lba * SECTOR + len(data)] = data

    # MBR: one partition, type 0x0C (FAT32 LBA)
    mbr = bytearray(SECTOR)
    mbr[446:462] = struct.pack("<B3sB3sII", 0x00, b"\0\0\0", 0x0C, b"\0\0\0", PART_START, part_sectors)
    mbr[510:512] = b"\x55\xAA"
    put(0, mbr)

    # FAT32 boot sector
    bs = bytearray(SECTOR)
    bs[0:3] = b"\xEB\x58\x90"
    bs[3:11] = b"MYOS    "
    struct.pack_into("<HBHBHHBHHHII", bs, 11, SECTOR, 1, RESERVED, 2, 0, 0, 0xF8, 0, 63, 255,
                     PART_START, part_sectors)
    struct.pack_into("<IHHIHH", bs, 36, fat_size, 0, 0, 2, 1, 6)
    bs[64] = 0x80
    bs[66] = 0x29
    bs[67:71] = b"MYOS"
    bs[71:82] = b"bootfs     "
    bs[82:90] = b"FAT32   "
    bs[510:512] = b"\x55\xAA"
    put(PART_START, bs)
    put(PART_START + 6, bs)                                 # backup copy

    fsinfo = bytearray(SECTOR)
    struct.pack_into("<I", fsinfo, 0, 0x41615252)
    struct.pack_into("<III", fsinfo, 484, 0x61417272, 0xFFFFFFFF, 0xFFFFFFFF)
    struct.pack_into("<I", fsinfo, 508, 0xAA550000)
    put(PART_START + 1, fsinfo)

    # FAT: cluster 2 = root directory, clusters 3.. = MYFS.IMG
    fat = bytearray(fat_size * SECTOR)
    struct.pack_into("<III", fat, 0, 0x0FFFFFF8, 0x0FFFFFFF, 0x0FFFFFFF)
    for i in range(file_clusters):
        c = 3 + i
        struct.pack_into("<I", fat, c * 4, c + 1 if i < file_clusters - 1 else 0x0FFFFFFF)
    for copy in range(2):
        put(PART_START + RESERVED + copy * fat_size, fat)

    # root directory: volume label + MYFS.IMG
    root = bytearray(SECTOR)
    root[0:11] = b"bootfs     "
    root[11] = 0x08
    entry = bytearray(32)
    entry[0:11] = b"MYFS    IMG"
    entry[11] = 0x20
    struct.pack_into("<H", entry, 20, 0)                    # cluster high
    struct.pack_into("<H", entry, 26, 3)                    # cluster low
    struct.pack_into("<I", entry, 28, len(content))
    root[32:64] = entry
    put(data_start, root)                                   # cluster 2

    myfs_lba = data_start + 1                               # cluster 3
    put(myfs_lba, content)

    with open(out_path, "wb") as f:
        f.write(img)
    return myfs_lba, file_clusters


def read_myfs(card_path, myfs_lba, sectors, out_path):
    """copy MYFS.IMG back out of a card image (to check it with tools/myfs.py)"""
    with open(card_path, "rb") as f:
        f.seek(myfs_lba * SECTOR)
        data = f.read(sectors * SECTOR)
    with open(out_path, "wb") as f:
        f.write(data)


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    lba, n = build(sys.argv[1], sys.argv[2:])
    print(f"{sys.argv[1]}: MYFS.IMG at sectors {lba}-{lba + n - 1}")
