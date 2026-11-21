#!/usr/bin/env python3
"""Minimal FAT16 reader: extract /image/* from Xiaomi NON-HLOS.bin."""
import os, struct, sys

IMG = sys.argv[1]
DEST = sys.argv[2]
d = open(IMG, "rb").read()
bps = struct.unpack_from("<H", d, 11)[0]
spc = d[13]
reserved = struct.unpack_from("<H", d, 14)[0]
nfats = d[16]
root_entries = struct.unpack_from("<H", d, 17)[0]
fat_sectors = struct.unpack_from("<H", d, 22)[0]
fat_off = reserved * bps
root_off = fat_off + nfats * fat_sectors * bps
data_off = root_off + root_entries * 32

def entry_name(e):
    name = e[:8].decode("ascii", "replace").rstrip()
    ext = e[8:11].decode("ascii", "replace").rstrip()
    return name + ("." + ext if ext else "")

def fat_next(cluster):
    return struct.unpack_from("<H", d, fat_off + cluster * 2)[0]

def cluster_data(cluster):
    return d[data_off + (cluster - 2) * spc * bps:
             data_off + (cluster - 2) * spc * bps + spc * bps]

def dir_entries(cluster):
    if cluster is None:
        buf = d[root_off:root_off + root_entries * 32]
        return [buf[i:i+32] for i in range(0, len(buf), 32)]
    out = []
    seen = set()
    while cluster not in seen and cluster >= 2 and cluster < 0xfff0:
        seen.add(cluster)
        buf = cluster_data(cluster)
        for i in range(0, len(buf), 32):
            out.append(buf[i:i+32])
        cluster = fat_next(cluster)
    return out

root = dir_entries(None)
imgdir = None
for e in root:
    if e[0] == 0 or e[0] == 0xe5:
        continue
    if e[11] & 0x10 and entry_name(e).upper() == "IMAGE":
        imgdir = struct.unpack_from("<H", e, 26)[0]
        break
if imgdir is None:
    raise SystemExit("no /image directory")

os.makedirs(DEST, exist_ok=True)
count = 0
for e in dir_entries(imgdir):
    if e[0] == 0 or e[0] == 0xe5 or e[11] & 0x10:
        continue
    name = entry_name(e)
    size = struct.unpack_from("<I", e, 28)[0]
    cluster = struct.unpack_from("<H", e, 26)[0]
    out = b""
    seen = set()
    while cluster >= 2 and cluster < 0xfff0 and cluster not in seen and len(out) < size:
        seen.add(cluster)
        out += cluster_data(cluster)
        cluster = fat_next(cluster)
    with open(os.path.join(DEST, name), "wb") as fh:
        fh.write(out[:size])
    count += 1
print("extracted", count, "files to", DEST)
