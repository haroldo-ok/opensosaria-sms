#!/usr/bin/env python3
"""Extract every file from Apple II DOS 3.3 .dsk images (140 KB, DOS order).

Usage:  python3 tools/extract_disks.py OUTDIR DISK1.dsk [DISK2.dsk ...]

Writes each catalog entry to OUTDIR/<diskstem>/<filename>.bin and a
manifest.txt listing name, type, track/sector, and raw byte size. The
original disk images are never modified and never copied into the project.
The required-file sizes are the ones checked by sceneDiskLoader.c in
open-sosaria; this tool reports whether each one matches.
"""
import os
import re
import struct
import sys

SECTOR = 256
SPT = 16
TYPES = {0: "T", 1: "I", 2: "A", 4: "B", 8: "S", 16: "R"}

# Sizes the Open Sosaria engine requires (from sceneDiskLoader.c).
REQUIRED = {
    "PIC.ULTIMATUM": 8192, "OUT.SHAPES": 768, "TWN.CAS.SHAPES": 512,
    "SPA.SHAPES": 1024, "ULTSHAPES": 768, "DRAW 64.OBJ": 768,
    "OUT MOVE": 11008, "TWN MOVE": 14336, "CAS MOVE": 12032,
    "DNG MOVE 1": 11520, "SPA MOVE": 8448, "SET1": 2560, "SET2": 2560,
    "SET3": 2560, "SET4": 2560, "SET5": 2304,
    "TWN.PIC": 8192, "CAS.PIC": 8192, "BTERRA0": 4352, "BTERRA1": 4352,
    "BTERRA2": 4352, "BTERRA3": 4352, "BTWN": 1024, "BCAS": 1024,
    "TIME MACHINE": 8960, "MONDAIN": 512,
}


def sector(disk, track, sec):
    off = (track * SPT + sec) * SECTOR
    return disk[off:off + SECTOR]


def catalog(disk):
    entries = []
    track, sec = 17, 15
    seen = set()
    while track and (track, sec) not in seen:
        seen.add((track, sec))
        s = sector(disk, track, sec)
        for i in range(7):
            e = 11 + i * 35
            ft, fs = s[e], s[e + 1]
            if ft == 0 or ft == 0xFF:
                continue  # empty or deleted slot
            raw_type = s[e + 2]
            name = bytes(b & 0x7F for b in s[e + 3:e + 33]).decode("latin1").rstrip()
            if not name.strip():
                continue
            entries.append({
                "name": name,
                "type": raw_type & 0x7F,
                "locked": bool(raw_type & 0x80),
                "track": ft,
                "sector": fs,
                "sectors": s[e + 33] | (s[e + 34] << 8),
            })
        track, sec = s[1], s[2]
    return entries


def read_chain(disk, track, sec):
    data = bytearray()
    seen = set()
    while track and (track, sec) not in seen:
        seen.add((track, sec))
        s = sector(disk, track, sec)
        for i in range(122):
            t, sc = s[12 + i * 2], s[13 + i * 2]
            if t == 0 and sc == 0:
                continue
            data += sector(disk, t, sc)
        track, sec = s[1], s[2]
    return bytes(data)


def safe_filename(name):
    return re.sub(r"[^A-Za-z0-9._ -]", "_", name).strip() or "unnamed"


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    outdir, disks = argv[1], argv[2:]
    found = {}
    for path in disks:
        with open(path, "rb") as fh:
            disk = fh.read()
        if len(disk) != 35 * SPT * SECTOR:
            print(f"!! {path}: unexpected size {len(disk)} (want 143360)")
        stem = os.path.splitext(os.path.basename(path))[0]
        target = os.path.join(outdir, stem)
        os.makedirs(target, exist_ok=True)
        manifest = []
        for ent in catalog(disk):
            data = read_chain(disk, ent["track"], ent["sector"])
            fname = safe_filename(ent["name"]) + ".bin"
            with open(os.path.join(target, fname), "wb") as out:
                out.write(data)
            kind = TYPES.get(ent["type"], str(ent["type"]))
            manifest.append(
                f"{ent['name']!r}\ttype={kind}\tts={ent['track']}/{ent['sector']}"
                f"\tsectors={ent['sectors']}\traw={len(data)}"
            )
            found.setdefault(ent["name"], (stem, len(data)))
        with open(os.path.join(target, "manifest.txt"), "w") as mf:
            mf.write("\n".join(manifest) + "\n")
        print(f"{path}: {len(manifest)} files -> {target}")

    print("\nRequired-file check (engine's expected sizes):")
    missing = 0
    for name, want in REQUIRED.items():
        if name not in found:
            print(f"  MISSING   {name!r}")
            missing += 1
        else:
            stem, got = found[name]
            status = "OK" if got == want else f"SIZE {got} != {want}"
            print(f"  {status:9} {name!r} ({stem})")
    return 0 if missing == 0 else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
