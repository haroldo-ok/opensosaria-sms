#!/usr/bin/env python3
"""Build the text and monster banks (6 and 7) from the user's disk files.

Reads the same sources as open-sosaria (see tools/ultima_text.py) and writes:
    data/banked/bank6.bin   header, string index, first strings
    data/banked/bank7.bin   remaining strings

String ids:
    0 .. U-1          ultimaStrings[], same numbers as the engine
    U ..              the BEVERY name tables, one after another
                      (stats, races, types, armors, vehicles, weapons, spells, places)
    then              enemyDefinitions[].name, one per monster

Bank 6 header (offsets from the start of the bank; all words little-endian):
    0    u16  total string count
    2    u16  U, the number of ultimaStrings
    4    u16  first id of each of the 8 name tables (16 bytes)
    20   u16  first id of the monster names
    22   u8   monster count
    23   u8   (padding)
    24   u8   group[48]   overworld group size per monster
    72   u8   rank[48]    combat strength per monster
    120  u16  index[total]: bit 15 set = string is in bank 7; bits 0-14 = offset
              of the NUL-terminated string from the start of its bank
Strings are capped at 40 bytes, like the engine's char[41].

Usage: python3 tools/build_text.py RAWDIR OUTDIR
"""
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ultima_text import load  # noqa: E402

BANK_LIMIT = 16320              # stay below 16 KiB, as the skill advises
HEADER = 120
MAX_ENEMIES = 48
TABLE_ORDER = ["stats", "races", "types", "armors", "vehicles", "weapons", "spells", "places"]


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    raw, out = argv[1], argv[2]
    d = load(raw)

    strings = list(d["strings"])
    n_ultima = len(strings)
    bases = []
    for name in TABLE_ORDER:
        bases.append(len(strings))
        strings += d["tables"][name]
    enemy_base = len(strings)
    enemies = d["enemies"]
    if len(enemies) > MAX_ENEMIES:
        raise SystemExit("too many monsters: %d" % len(enemies))
    strings += [e["name"] for e in enemies]
    strings = [s[:40] for s in strings]
    total = len(strings)

    bank6 = bytearray(HEADER + 2 * total)
    bank7 = bytearray()
    index = []
    for s in strings:
        blob = s + b"\0"
        if len(bank6) + len(blob) <= BANK_LIMIT and not bank7:
            index.append(len(bank6))
            bank6 += blob
        else:
            if len(bank7) + len(blob) > BANK_LIMIT:
                raise SystemExit("text does not fit in banks 6 and 7")
            index.append(0x8000 | len(bank7))
            bank7 += blob

    struct.pack_into("<HH", bank6, 0, total, n_ultima)
    struct.pack_into("<8H", bank6, 4, *bases)
    struct.pack_into("<HBB", bank6, 20, enemy_base, len(enemies), 0)
    for i, e in enumerate(enemies):
        bank6[24 + i] = int(e["group"]) & 0xFF
        bank6[72 + i] = int(e["rank"]) & 0xFF
    struct.pack_into("<%dH" % total, bank6, HEADER, *index)

    os.makedirs(os.path.join(out, "banked"), exist_ok=True)
    for n, blob in ((6, bank6), (7, bank7 or b"\0")):
        path = os.path.join(out, "banked", "bank%d.bin" % n)
        with open(path, "wb") as fh:
            fh.write(blob)
        print("banked/bank%d.bin: %d bytes" % (n, len(blob)))
    print("strings: %d (%d ultimaStrings), monsters: %d" % (total, n_ultima, len(enemies)))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
