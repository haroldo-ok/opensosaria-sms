#!/usr/bin/env python3
"""Rebuild open-sosaria's text and enemy tables from the user's disk files.

This is a Python mirror of the parts of sceneDiskLoader.c that fill:
    ultimaStrings[]      quoted strings from the BASIC programs and SET files
    statsNames, armorNames, vehicleNames, weaponNames, spellNames, placesNames,
    racesNames, typesNames   string arrays stored in BEVERY
    enemyDefinitions[]   name, group, rank, hp per monster (BEVERY)

The string indices match the engine, so the SMS code can use the same numbers
as open-sosaria (for example ultimaStrings[98]).

Used by tools/build_text.py. Can also be run alone to dump the tables:
    python3 tools/ultima_text.py RAWDIR
"""
import os
import sys

STRING_MAX = 40          # char ultimaStrings[1500][41]


def strip_bload(data):
    if len(data) >= 8:
        body = data[2] | (data[3] << 8)
        if body > 0 and body + 4 <= len(data):
            return data[4:4 + body]
    return data


def _emit_quoted(line, strings, state):
    """sceneDiskLoader_emitQuotedStringsFromAppleBasicLine. `state` holds
    ultimaStringCount, so appends across calls behave like the engine's."""
    n = len(line)
    at = lambda k: line[k] if k < n else 0
    i = 0
    should_append = False
    while i < n:
        if line[i] == 0x22:
            start = i + 1
            is_empty = True
            while at(start) == 0x07:
                i += 1
                start += 1
                is_empty = False
            end = start
            str_end = start
            while end < n and line[end] != 0x22:
                if at(str_end) != 0x07:
                    str_end += 1
                end += 1
            if end >= n:
                return
            length = str_end - start
            idx = state["count"]
            if 0 < length < 41:
                text = bytes(line[start:start + length])
                if should_append:
                    existing = strings[idx]
                    if len(existing) + length < 41:
                        strings[idx] = existing + text
                        should_append = False
                        state["count"] += 1
                        i = end + 1
                        continue
                else:
                    strings[idx] = text
                if at(end + 1) == 200:          # '+' token
                    should_append = True
                else:
                    state["count"] += 1
                    should_append = False
            elif not is_empty:
                strings[idx] = b""
                state["count"] += 1
            i = end + 1
            continue
        i += 1


def _extract_basic(data, strings, state):
    """sceneDiskLoader_extractBasicStrings."""
    pos = 0
    size = len(data)
    while pos + 4 <= size:
        next_addr = data[pos] | (data[pos + 1] << 8)
        pos += 4
        start = pos
        while pos < size and data[pos] != 0:
            pos += 1
        if pos > start:
            _emit_quoted(data[start:pos], strings, state)
        if pos < size and data[pos] == 0:
            pos += 1
        if next_addr == 0:
            break


def _extract_binary(data, strings, state):
    """sceneDiskLoader_extractBinaryStrings."""
    start = 0
    size = len(data)
    while start + 1 < size:
        if data[start] == 0x22:
            end = start + 1
            while end < size and data[end] != 0x22:
                end += 1
            if end >= size:
                break
            strings[state["count"]] = bytes(data[start + 1:end])[:STRING_MAX]
            state["count"] += 1
        start += 1


def find_variable_offset(data, name):
    letters = [c.upper() for c in name if c.isalpha()]
    b0 = ord(letters[0])
    b1 = 0x80 if len(letters) == 1 else (ord(letters[1]) | 0x80)
    for i in range(len(data) - 1):
        if (data[i] & 0x7F) == b0 and data[i + 1] == b1:
            return i
    return 0


def decode_string_array(data, index):
    """sceneDiskLoader_decodeUltimaStrings: count, then (len, addr lo, addr hi)."""
    size = (data[index] << 8) | data[index + 1]
    out = []
    off = index + 2
    for _ in range(size):
        length = data[off]
        addr = (data[off + 1] | (data[off + 2] << 8)) - 0x7800
        out.append(bytes(data[addr:addr + length]))
        off += 3
    return out


def applesoft_float(b):
    exp = b[0]
    if exp == 0:
        return 0.0
    sign = -1 if (b[1] & 0x80) else 1
    mant = ((b[1] & 0x7F) << 24) | (b[2] << 16) | (b[3] << 8) | b[4]
    return sign * (1.0 + mant / 2147483648.0) * (2.0 ** (exp - 129))


def decode_enemies(data):
    count = (data[0x496] << 8) | data[0x497]
    off, name_off, stride = 0x498, 0x405, 5
    enemies = []
    for i in range(count):
        group = applesoft_float(data[off + (0 * count + i) * stride:])
        rank = applesoft_float(data[off + (1 * count + i) * stride:])
        hp = applesoft_float(data[off + (2 * count + i) * stride:])
        length = data[name_off + i * 3]
        addr = (data[name_off + i * 3 + 1] | (data[name_off + i * 3 + 2] << 8)) - 0x7800
        name = bytes(data[addr:addr + length])[:15]
        enemies.append({"name": name, "group": group, "rank": rank, "hp": hp})
    return enemies


def load(raw):
    """Return a dict with every table, using the engine's load order."""
    rd = lambda disk, name: open(os.path.join(raw, disk, name + ".bin"), "rb").read()
    strings = [b""] * 1500
    state = {"count": 0}
    for disk, name, kind in [
        ("disk1", "INIT DISPLAY", "b"), ("disk1", "OUT MOVE", "b"),
        ("disk1", "TWN MOVE", "b"), ("disk1", "CAS MOVE", "b"),
        ("disk1", "DNG MOVE 1", "b"), ("disk1", "SET1", "x"),
        ("disk1", "SET2", "x"), ("disk1", "SET4", "x"), ("disk1", "SET5", "x"),
        ("disk1", "SPA MOVE", "b"), ("disk2", "TIME MACHINE", "b"),
    ]:
        (_extract_basic if kind == "b" else _extract_binary)(rd(disk, name), strings, state)
    count = state["count"]

    bev = strip_bload(rd("disk2", "BEVERY"))
    arr = lambda var: decode_string_array(bev, find_variable_offset(bev, var) + 5)
    tables = {
        "stats": arr("C$"), "races": arr("RA$"), "types": arr("TY$"),
        "armors": arr("AR$"), "vehicles": arr("VE$"), "weapons": arr("PW$"),
        "spells": arr("SP$"), "places": arr("TD$"),
    }
    tables["spells"][3] = strings[20]
    tables["places"][44] = strings[26]

    enemies = decode_enemies(bev)
    for idx, s in [(12, 14), (6, 15), (7, 16), (10, 17), (19, 18), (20, 19),
                   (30, 21), (33, 22), (39, 23), (41, 24), (32, 25)]:
        enemies[idx]["name"] = strings[s][:15]

    return {"strings": strings[:count], "tables": tables, "enemies": enemies}


if __name__ == "__main__":
    d = load(sys.argv[1])
    for i, s in enumerate(d["strings"]):
        print(f"[{i}] {s!r}")
    for k, v in d["tables"].items():
        print(k, v)
    for i, e in enumerate(d["enemies"]):
        print(i, e)
