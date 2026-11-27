#!/usr/bin/env python3
"""Inspect a dumped /persist (mmcblk0p74) image without mounting it.

Input: one or more 16 MiB chunk files dumped by the violet `dump persist`
verb (PERS0001..0004.BIN, concatenated in order = the raw 64 MiB partition),
or a single concatenated image.

What it does:
  1. ext4 superblock sanity check (magic, volume name, block size, feature
     flags) so we know the dump is a real /persist filesystem.
  2. Recursive directory walk using the ext4 inode tables and extent trees,
     listing every file with size and inode (enough to locate provisioning
     payloads such as wlan_mac.bin, wpadata, persist/wifi/...).
  3. Raw signature scan fallback for names that the walk might miss
     (corrupted extents, inline xattrs).

Usage:
  python scripts/persist_inspect.py --in PERS0001.BIN PERS0002.BIN ...
  python scripts/persist_inspect.py --in PERSIST.BIN --extract wlan_mac.bin out.bin
"""
import argparse
import os
import struct
import sys

EXT4_SUPERBLOCK_OFFSET = 1024
EXT4_MAGIC = 0xEF53

# feature flags we care about
INCOMPAT_64BIT = 0x80


class Ext4Image:
    def __init__(self, data):
        self.data = data
        self._parse_superblock()

    def _parse_superblock(self):
        if len(self.data) < EXT4_SUPERBLOCK_OFFSET + 264:
            raise ValueError("image too small for an ext4 superblock")
        sb = self.data[EXT4_SUPERBLOCK_OFFSET:]
        (self.inodes_count, self.blocks_count_lo, _, _, self.free_blocks_lo,
         _) = struct.unpack_from("<8I", sb, 0)
        (magic,) = struct.unpack_from("<H", sb, 0x38)
        if magic != EXT4_MAGIC:
            raise ValueError("not an ext4 image (magic 0x%04x)" % magic)
        self.log_block_size = struct.unpack_from("<I", sb, 0x18)[0]
        self.block_size = 1024 << self.log_block_size
        self.blocks_per_group = struct.unpack_from("<I", sb, 0x20)[0]
        self.inodes_per_group = struct.unpack_from("<I", sb, 0x28)[0]
        self.inode_size = struct.unpack_from("<H", sb, 0x58)[0]
        (self.feature_compat,) = struct.unpack_from("<I", sb, 0x4C)
        (self.feature_incompat,) = struct.unpack_from("<I", sb, 0x50)
        (self.feature_ro_compat,) = struct.unpack_from("<I", sb, 0x54)
        self.volume_name = sb[0x78:0x78 + 16].split(b"\0")[0].decode(
            "utf-8", errors="replace")
        self.desc_size = 32 if self.feature_incompat & INCOMPAT_64BIT else 32
        # 64-bit feature moves desc_size to 64
        if self.feature_incompat & INCOMPAT_64BIT:
            self.desc_size = struct.unpack_from("<H", sb, 0xFE)[0] or 64
        first_data_block = struct.unpack_from("<I", sb, 0x14)[0]
        group_count = (self.blocks_count_lo - first_data_block +
                       self.blocks_per_group - 1) // self.blocks_per_group
        desc_table_start = (EXT4_SUPERBLOCK_OFFSET + self.block_size *
                            first_data_block)
        self.group_descriptors = []
        for g in range(group_count):
            off = desc_table_start + g * self.desc_size
            if off + 32 > len(self.data):
                break
            lo = struct.unpack_from("<8I", self.data, off)
            inode_table_lo = lo[3]
            hi = 0
            if self.desc_size >= 64:
                hi = struct.unpack_from("<I", self.data, off + 0x20 + 0x8)[0] \
                    if off + 0x28 < len(self.data) else 0
                # bg_inode_table_hi is at offset 40 in a 64-byte descriptor
                hi = struct.unpack_from("<I", self.data, off + 40)[0] \
                    if self.desc_size >= 64 else 0
            self.group_descriptors.append((hi << 32) | inode_table_lo)
        self.first_data_block = first_data_block

    def read_block(self, block):
        start = block * self.block_size
        end = start + self.block_size
        if end > len(self.data):
            raise ValueError("block %u beyond end of image" % block)
        return self.data[start:end]

    def inode(self, num):
        """Load the ext4 inode at 1-based index `num`."""
        idx = num - 1
        group = idx // self.inodes_per_group
        if group >= len(self.group_descriptors):
            return None
        table = self.group_descriptors[group]
        index = idx % self.inodes_per_group
        off = table + index * self.inode_size
        if off + self.inode_size > len(self.data):
            return None
        raw = self.data[off:off + self.inode_size]
        (mode, uid, size_lo, atime, ctime, mtime, dtime, gid, links, blocks_lo,
         flags) = struct.unpack_from("<11I", raw, 0)
        if mode == 0 and links == 0:
            return None
        i = {
            "num": num, "mode": mode & 0xF000, "size": size_lo, "links": links,
            "flags": flags, "extents": [],
        }
        if size_lo >= 0 and (mode & 0xF000) == 0x4000:
            i["dir"] = True
        elif (mode & 0xF000) in (0x8000, 0xA000):
            i["dir"] = False
        else:
            return i if i["dir"] is None else i
        i["dir"] = (mode & 0xF000) == 0x4000
        if i["size"] > 0 and not i["dir"]:
            pass
        # extent tree: 15 * 60 bytes = i_block at offset 0x28
        i_block = raw[0x28:0x28 + 60]
        eh_magic, eh_entries, eh_max, eh_depth = struct.unpack_from(
            "<HHHH", i_block, 0)
        if eh_magic != 0xF30A:
            # legacy block map or inline data: report what we know
            i["extents"] = None
            return i
        self._walk_extent_header(i_block, eh_depth, i["extents"])
        return i

    def _walk_extent_header(self, block, depth, out):
        eh_entries, = struct.unpack_from("<H", block, 2)
        pos = 12
        for e in range(eh_entries):
            if depth == 0:
                ee_block, ee_len, ee_start_hi, ee_start_lo = struct.unpack_from(
                    "<IHHI", block, pos)
                start = (ee_start_hi << 32) | ee_start_lo
                out.append((ee_block, ee_len, start))
            else:
                ei_block, ei_leaf_hi, ei_leaf_lo = struct.unpack_from(
                    "<IHI", block, pos)
                leaf = (ei_leaf_hi << 32) | ei_leaf_lo
                try:
                    sub = self.read_block(leaf)
                except ValueError:
                    continue
                sub_entries, sub_depth = struct.unpack_from("<HH", sub, 2)
                if sub_depth + 1 != depth:
                    continue
                self._walk_extent_header(sub, sub_depth, out)
            pos += depth == 0 and 12 or 12
            # index entries are also 12 bytes
        return

    def read_file(self, inode):
        out = bytearray()
        for (logical, length, start) in inode["extents"]:
            for b in range(length):
                out += self.read_block(start + b)
        return bytes(out[:inode["size"]])

    def read_dir(self, inode):
        """Return [(name, inode_num, ftype)] from a directory inode."""
        entries = []
        data = bytearray()
        for (logical, length, start) in inode["extents"]:
            for b in range(length):
                data += self.read_block(start + b)
        data = bytes(data[:inode["size"]])
        pos = 0
        while pos + 8 <= len(data):
            inode_num, rec_len, name_len, ftype = struct.unpack_from(
                "<IHBB", data, pos)
            if rec_len < 8 or inode_num == 0:
                pos += 8 if rec_len < 8 else rec_len
                continue
            name = data[pos + 8:pos + 8 + name_len]
            if inode_num:
                entries.append((name.decode("utf-8", errors="replace"),
                                inode_num, ftype))
            pos += rec_len
        return entries


def walk(image, inode_num, path, files, depth=0, max_depth=8):
    if depth > max_depth:
        return
    node = image.inode(inode_num)
    if node is None or not node.get("dir"):
        return
    try:
        entries = image.read_dir(node)
    except (ValueError, IndexError):
        files.append(("  %s  <unreadable dir>" % path, None, None))
        return
    for (name, num, ftype) in entries:
        if name in (".", ".."):
            continue
        child_path = path.rstrip("/") + "/" + name
        child = image.inode(num) if num else None
        if child is None:
            files.append(("%s  <inode %u not present>" % (child_path, num),
                          num, None))
            continue
        is_dir = child.get("dir")
        size = child.get("size", 0)
        if is_dir:
            files.append(("d %s/" % child_path, num, None))
            walk(image, num, child_path, files, depth + 1, max_depth)
        else:
            files.append(("%-s %s (%u bytes)" % ("f", child_path, size),
                          num, size))


INTERESTING = (b"wlan_mac", b"wpa_supplicant", b"persist", b"wifi", b"wcnss",
               b"Wifi", b"WIFI", b"nvdata", b"mac")


def raw_scan(data):
    hits = {}
    for sig in INTERESTING:
        start = 0
        while True:
            i = data.find(sig, start)
            if i < 0:
                break
            context = data[max(0, i - 24):i + 40]
            context = bytes(c if 32 <= c < 127 else 46 for c in context)
            hits.setdefault(sig.decode(), []).append((i, context.decode()))
            start = i + 1
            if len(hits[sig.decode()]) > 8:
                break
    return hits


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--in", dest="inputs", nargs="+", required=True,
                    help="chunk files, in partition order")
    ap.add_argument("--extract", metavar="PATH", default=None,
                    help="extract one file (by path shown in the walk)")
    ap.add_argument("--out", default=None, help="output file for --extract")
    args = ap.parse_args()

    data = bytearray()
    for path in args.inputs:
        with open(path, "rb") as f:
            data += f.read()
    data = bytes(data)
    print("image: %u bytes" % len(data))

    try:
        img = Ext4Image(data)
    except ValueError as e:
        print("superblock: %s" % e)
        print("raw signature scan only:")
        for sig, h in raw_scan(data).items():
            for (off, ctx) in h[:4]:
                print("  %s at 0x%08x: %s" % (sig, off, ctx))
        return 1

    print("superblock: ok magic=0x%04x" % EXT4_MAGIC)
    print("  volume name: '%s'" % img.volume_name)
    print("  block size: %u, blocks: %u, inodes: %u" %
          (img.block_size, img.blocks_count_lo, img.inodes_count))
    print("  free blocks: %u" % img.free_blocks_lo)
    print("  inode size: %u, inodes/group: %u, groups: %u" %
          (img.inode_size, img.inodes_per_group, len(img.group_descriptors)))

    files = []
    root = img.inode(2)
    if root is None or not root.get("dir"):
        print("root inode (2): missing or not a directory")
        return 1
    walk(img, 2, "", files)
    print("directory walk:")
    for line in files:
        print("  " + line[0])

    if args.extract:
        wanted = args.extract.strip("/")
        for line in files:
            text = line[0]
            if line[1] is None:
                continue
            # path portion after the leading type marker
            path = text.split(" ", 1)[-1].split(" (")[0].rstrip("/")
            if path != wanted:
                continue
            node = img.inode(line[1])
            if node is None or node.get("dir"):
                print("extract: %s is not a regular file with extents" % wanted)
                return 1
            payload = img.read_file(node)
            out = args.out or os.path.basename(wanted)
            with open(out, "wb") as f:
                f.write(payload)
            print("extracted %s -> %s (%u bytes)" % (wanted, out, len(payload)))
            return 0
        print("extract: %s not found in the walk" % wanted)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
