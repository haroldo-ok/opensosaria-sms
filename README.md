# Open Sosaria: Sega Master System port

A port of [delcodigo/open-sosaria](https://github.com/delcodigo/open-sosaria) v0.6.0 (MIT), a C reimplementation of Ultima I, to the Sega Master System. The target is a `.sms` ROM, built with SDCC and devkitSMS.

**This repository contains no game data.** Maps, towns, castles, dungeon tables, and pictures are read from your own legally obtained Ultima I disk images, then converted by the scripts in `tools/`. Generated data and screenshots from the game are not part of the source archive.

Reference for the original game: https://archive.org/details/wozaday_Ultima_Trilogy_Ultima_I_The_Beginning (WOZ format; see "Disk images" below).

## Status

| Area | Status |
|---|---|
| Title screen, text, input, state machine | Working |
| Overworld: terrain, scrolling, movement, collision | Working. Checked in the emulator against a forced full redraw. Not yet compared with the original's look. |
| Towns and castles: picture, scrolling, collision | Working in the emulator. Town and castle pictures scroll horizontally. Shops, dialogue, and guards are not ported. |
| Dungeons: floors, movement, turning, trapdoors, ladders | Working. Floors come from this port's own deterministic generator, so they are not identical to the original. |
| Dungeons: first-person wireframe view | Working. The 280 px view is scaled to 256 px. Monsters and console messages are not drawn. |
| Dungeon input | Fixed in the emulator. Taps during the redraw are latched. |
| Dungeon turn speed | About 0.8 s per turn in the emulator (43 to 49 frames). Slower than the original; see Known issues. |
| Combat, encounters, monsters | Not started |
| Space (pseudo-3D) section | Not started |
| Mondain final battle and the Time Machine | Not started |
| Save and load (SRAM) | Not started |
| Sound and music | Not started |

## Known issues

- **Dungeon turn speed.** One turn takes about 0.8 seconds in the emulator. Most of the time goes to rendering the lines (15 to 25 frames) and the clear (7 frames). The cell scan is hand-written Z80 in `src/scan_asm.s`. Full prerendering of every view is not feasible: `tools/count_views.c` counts 873,470 distinct views.
- **Device-only band.** A stale band of overworld-style tiles appeared on a real console, in rows 6 to 9 of the dungeon view. It was not reproduced in the emulator. A fix is in the code, but it is not yet confirmed on hardware.
- **Input during the redraw.** Two different keys pressed during one redraw are merged into one action, so the second is lost.
- **Build warning.** `src/dungeon_tables.c` gives "overflow in implicit constant conversion" for one value, 53445, in an `int` table. It is open in `PORTING_PLAN.md` until the table's use is checked.
- **Temporary debug code.** `dgn_dbg_stage` markers in `src/dungeon_view.c` are there for `tools/dungeon_redraw_stages.txt`. Remove them before a release.

## Requirements

- **devkitSMS** toolchain, including `smstest` (the headless emulator used by the tests). Set it up with the sega-master-system-devkitsms skill: https://github.com/haroldo-ok/sega-master-system-skill-for-claude. The Makefile expects it in `~/.devkitsms` (override with `DEVKIT=...`).
- **SDCC 4.5** (`sdcc` and `sdasz80`). On Debian and Ubuntu, `sudo apt-get install sdcc`.
- **Python 3 with Pillow** (`python3-pil`) for the data scripts.

Note: after a fresh environment or a reset, check that `sdcc` is on the path and that `folder2c`, `ihx2sms`, and `smstest` in `~/.devkitsms/bin` are executable (`chmod +x`).

## Disk images

The scripts read **Apple II DOS 3.3 `.dsk` images** (140 KB, DOS order). The archive.org item is in WOZ format, so convert it to `.dsk` first. The engine needs these files. The extractor checks their sizes against the engine's values:

- Disk 1: `PIC.ULTIMATUM`, `OUT.SHAPES`, `TWN.CAS.SHAPES`, `SPA.SHAPES`, `ULTSHAPES`, `DRAW 64.OBJ`, the `* MOVE` files, `SET1`–`SET5`
- Disk 2: `TWN.PIC`, `CAS.PIC`, `BTERRA0`–`BTERRA3`, `BTWN`, `BCAS`, `TIME MACHINE`, `MONDAIN`

The dungeon-table script also reads `BEVERY` from disk 2.

Only use disk images you own. Do not commit them to the repository.

## Building the game data

Run these from the project root. They write into `data/` and `src/`, which are not in the source archive:

```
python3 tools/extract_disks.py ~/ultima-raw disk1.dsk disk2.dsk
python3 tools/build_overworld.py ~/ultima-raw data
python3 tools/build_town.py ~/ultima-raw data
python3 tools/extract_dungeon_table.py ~/ultima-raw data/dungeon_pe.bin
python3 tools/extract_dungeon_tables.py ~/ultima-raw src/dungeon_tables.c
```

`build_overworld.py` writes `data/*.bin` and `data/banked/bank2.bin`, `bank3.bin`. `build_town.py` writes `data/banked/bank4.bin` and `bank5.bin`. `make` turns these into C arrays (`data.c`, `gen/bankN.c`).

## Build and test

```
make            # builds opensosaria.sms
make playtest   # runs six headless emulator suites
```

`make playtest` runs `tests/playtest.txt`, `banks.txt`, `town.txt`, `enter.txt`, `dungeon.txt`, and `dungeon_input.txt`. The screenshots go to `shots/`.

Two more probes, which are not in `make playtest`:

- `tools/dungeon_turn_timing.txt`: six chained turns, with a check that the view returns to the start facing.
- `tools/dungeon_redraw_stages.txt`: measures each phase of a dungeon redraw, using the temporary stage markers.

Run one with `smstest opensosaria.sms tools/<file>.txt`.

`make clean` removes the build outputs and the generated files, including `data.c`, `data.h`, and `gen/`.

## Controls

- **Title:** press a button to start.
- **Overworld and towns:** the D-pad moves. Stand on an entrance and press button 2 to enter it.
- **Dungeon:** Up moves forward. Left and right turn. Down turns around. Button 2 climbs a ladder: tile 8 goes up, tiles 7 and 9 go down. Climbing up from depth 1 returns to the overworld. Walls and tiles above 9 block movement. Trapdoors drop the player one floor.

## Project layout

- `src/main.c`: state machine (title, overworld, town, dungeon) and the main loop.
- `src/overworld.c`, `src/town.c`: overworld and town/castle rendering, scrolling, and collision.
- `src/dungeon.c`: dungeon floors, movement, climbing, and input latching.
- `src/dungeon_view.c`: first-person wireframe renderer and the conversion to SMS tiles.
- `src/scan_asm.s`: Z80 cell scan used by the dungeon view.
- `src/dungeon_tables.c`: generated from BEVERY (see above). Not in the source archive.
- `tools/`: data extraction and conversion scripts, emulator probes, and `count_views.c`.
- `tests/`: playtest scripts for `smstest`.
- `PORTING_PLAN.md`: design notes, measurements, decisions, and open items.

## Differences from the original

- Dungeon floors use a deterministic generator seeded from the world position and depth. The original seeds from `srand` and the game clock, so its floors vary by platform and time.
- The dungeon view is scaled from 280 px to 256 px horizontally, and it is drawn in one colour.
- The town and castle pictures scroll horizontally rather than being scaled or cropped.

## License

MIT. See `LICENSE`. It keeps the upstream copyright notice for delcodigo/open-sosaria and adds one for the port.

The license covers the code and documentation in this repository. It does not cover Ultima I itself or its data. You need your own legal copy of the disks to build the game data.
