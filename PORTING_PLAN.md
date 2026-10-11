# Open Sosaria → Sega Master System: porting plan

Source: `delcodigo/open-sosaria` v0.6.0 (C99, MIT). Scope chosen: **full port (v0.6.0 parity)**. Data source: **user-supplied Apple II disk images**.

## Status

| Area | Status |
|---|---|
| Toolchain (SDCC 4.5 + devkitSMS + smstest) | Working |
| Build (`make` → `opensosaria.sms`, SEGA header OK) | Working |
| Title screen, text, input, state machine | Working (playtest passes) |
| Playtest harness (5 checks) | Passing |
| Disk-image data extraction | Disks received and validated (`tools/extract_disks.py`); all 26 required files extracted with the engine's expected sizes. Raw output in `/home/user/ultima-raw/` (not in the project). |
| Overworld (terrain render, ring-buffered scroll, movement, collision) | Working. 8-step playtest passes; horizontal and vertical ring streaming matches a forced full redraw pixel-for-pixel. Terrain verified on screen (grass, forest, mountains, lake, town icon). Fixed: terrain tiles were at VRAM tile 448 (0x3800), which overlapped the name table; now at 384–447 (`OW_TILE_BASE`). Scroll direction verified on screen (right 5 shifts content −80 px, down 4 shifts −64 px). Sprite Y corrected by −1 (SAT Y is drawn one line low), so the player's top edge sits on row 96. Not yet compared with the original game's look. |
| Towns, castles, dungeons | Views, movement and collision working (see below). Shops, NPCs and dungeon monsters not started. |
| Console, stats panel, game text | Working in the overworld (see "Console and stats panel"). |
| Overworld monsters and combat | Working (see "Overworld combat"). |
| Space / Mondain | Not started |

## What the source actually depends on

- The repo ships **no game data**. Maps, towns, castles, monsters, text, and shapes are read at runtime from the Apple II DOS 3.3 disks.
- Files the engine requires (from `sceneDiskLoader.c`):
  - Disk 1: `PIC.ULTIMATUM` (8192), `OUT.SHAPES` (768), `TWN.CAS.SHAPES` (512), `SPA.SHAPES` (1024), `ULTSHAPES` (768), `DRAW 64.OBJ` (768), `OUT MOVE` (11008), `TWN MOVE` (14336), `CAS MOVE` (12032), `DNG MOVE 1` (11520), `SPA MOVE` (8448), `SET1`–`SET4` (2560 each), `SET5` (2304)
  - Disk 2: `TWN.PIC`, `CAS.PIC` (8192 each), `BTERRA0`–`BTERRA3` (4352 each), `BTWN` and `BCAS` (1024 each), `TIME MACHINE` (8960), `MONDAIN` (512)
- The disk names conflict: the README says `disk1.dsk`/`disk2.dsk`, but the code opens `program.dsk`/`player.dsk`. The extractor will accept either.
- `SET1`–`SET5` are tokenized Applesoft BASIC containing dungeon enemy-rendering routines. Porting them means decoding those routines, not just reading tables.
- The overworld is four 86×86 chunks (`BTERRA0`–`3`), of which the playable area is 64×64.

## Architecture

1. **Offline extractor (Python)** reads the `.dsk` images, parses the DOS 3.3 catalog, and emits SMS-ready `data/*.bin` (tile maps, town/castle layouts, monster tables, text). Nothing copyrighted gets committed; the extractor runs on the user's own disks.
2. **Game logic** ported from C99 to SDCC-friendly C: fixed-point (8.8) instead of floats, static pools, no heap, state in RAM (8 KB total).
3. **Platform layer** on SMSlib/PSGlib: 256×192 tilemap for the overworld and towns, sprites for the player and enemies, PSG for music/SFX.
4. **Banking** (`Makefile.banked`) once data exceeds 32 KB: code in bank 0, data banks mapped into slot 2.
5. **Test harness** (`tests/playtest.txt`) extended per phase, plus screenshots for each state.

## Phases

1. **Foundation** (done): build, boot, input, text, state machine, playtest.
2. **Data pipeline** (needs disks): DOS 3.3 reader, extraction of all required files, validation against the file sizes the engine checks.
3. **Overworld**: tile rendering from `BTERRA*`, player movement, collision, food/time model, vehicles, encounter spawning.
4. **Combat and status screens**: encounter UI, attack/flee/spells, inventory, character sheet, save/load (SRAM).
5. **Towns and castles**: town layouts, merchant shops, guards, dialogue.
6. **Dungeons**: first-person view. Use the skill's pseudo-3D approach (precomputed projection tables and sprite columns), and decode `SET1`–`SET5`.
7. **Space**: pseudo-3D first-person combat plus the sector map.
8. **Mondain**: the final battle and the Time Machine.
9. **Audio**: convert the Apple II click audio (`bin/audio.bin`) into PSG effects, plus title/town music.

## Risks

- Dungeon and space 3D rendering is the most expensive part on the SMS (no framebuffer, 8 sprites/line).
- SDCC 4.5 link warnings: the bundled SMSlib reports a calling-convention mismatch. The build currently passes with a filtered link check. Should be re-verified for `SMS_addSprite` before adding complex sprite logic.
- The skill's `prof_report.py` is missing from the public repo. `setup_toolchain.sh` was patched in the sandbox to skip it.

## What's needed from the user

- The disk images (`disk1.dsk` + `disk2.dsk`, or `program.dsk` + `player.dsk`). They must be DOS 3.3 `.dsk` images. The archive.org item is WOZ format and would need converting first.
- Confirmation that you own a legal copy of the original.

## VRAM layout (SMS, 16 KiB)

- Tile slots 0–447 are usable for graphics. The name table (tilemap) is at 0x3800, which is tile slot 448, and the sprite attribute table is above it.
- Current use: font at tiles 0–95 (loaded by `load_font`, one pixel lower than SMSlib's), tiles 64–67 player sprite and 68–79 overworld monsters (they overwrite the glyphs for characters 96–111, which the game text never uses), tiles 384–447 terrain (16 terrain × 4 SMS tiles). The player sprite used to be at tiles 4–7, over the glyphs `$%&'`.

## ROM banking plan

**Budget before banking.** The build was one 32 KiB fixed bank with 23,782 bytes used. Of that, 16,384 bytes were the overworld map (four 64×64 realms). The rest was about 7.4 KB. The map has since moved to banks 2 and 3 (see "Order of work" below).

**Raw source sizes (from `/home/user/ultima-raw/`).** These are the disk files the port has to convert. Sizes are on-disk raw bytes, not SMS sizes.

| File | Raw bytes | Likely use |
|---|---|---|
| `BTERRA0`–`3` | 4352 each | Overworld realms (already converted to the 16 KB map) |
| `BTWN`, `BCAS` | 1024 each | Town and castle maps |
| `BEVERY` | 7936 | Dungeon / town-interior data (to decode) |
| `SET1`–`SET5` | ~2.3–2.6 KB each | Tile sets (HGR bit planes) |
| `TWN.PIC`, `CAS.PIC` | 8192 each | Town and castle pictures |
| `SPA.SHAPES`, `STAR.OBJ`, `NEAR.OBJ` | 1024 / 1280 / 256 | Space section sprites and objects |
| `PIC.ULTIMATUM` | 8192 | Title picture (already decoded to the title screen) |

The `*MOVE` files (`OUT MOVE`, `TWN MOVE`, `CAS MOVE`, `DNG MOVE 1`, `SPA MOVE`) are 6502 game code, not data. The port reimplements that logic in C, so they are not stored in ROM.

**Layout.**

- **Fixed bank 0 (`0x0000–0x7FFF`).** Code, SMSlib, PSGlib, the state machine, the overworld engine, the terrain lookup, and small tables. Target: under 24 KB.
- **Bank 2 (`0x28000`).** Overworld realms 0 and 1 (8 KB).
- **Bank 3 (`0x38000`).** Overworld realms 2 and 3 (8 KB).
- **Banks 4+.** Town, castle, and dungeon data, tile sets, and pictures. Space and Mondain assets go in their own banks. Each bank is kept under 16,320 bytes, as the skill's notes advise.

**Build.** Switch from `Makefile` to the skill's `assets/template/Makefile.banked`. Each bank is one `gen/bankN.c` compiled with `--constseg BANKN`, and `BANKS`/`BANKFLAGS` are extended together. Arrays are emitted with `folder2c` or `assets2banks`.

**Runtime rules.**

- Put the terrain lookup in fixed bank 0. It saves the current bank number, calls `SMS_mapROMBank(2 or 3)`, reads one byte, and restores the saved bank before returning. Callers never see a banked address.
- Never hold a pointer into a banked array across a bank switch. Use indices.
- SRAM save/load (later) shares the `0x8000` window with the banks. Do not read banked data while SRAM is mapped. Save ids, not addresses.

**Tests.**

- Realms 1–3 start at x or y = 86, and a sea strip (x or y in 64–85) blocks walking, so on foot the player cannot reach them. Test the bank switch with a playtest poke to (100,40), (40,100), and (100,100), each followed by `poke redraw_needed 1`, and check the frame against a full redraw. Boat travel will reach the realms later.
- Walking across the realm borders in both directions must give the same frame as a forced full redraw.
- Every new bank needs one smoke read at its first and last byte.

**Order of work.**

1. **Done.** Overworld map moved to banks 2 and 3 (`data/banked/ow_map_bank{2,3}.bin`, generated by `tools/build_overworld.py`). `gen/bankN.c` is built by `tools/bin2c_bank.py`, and the Makefile uses `BANKS := 2 3`. `terrain_at()` pages the bank in, reads one byte, and restores the previous bank.
2. **Done.** `tests/banks.txt` pokes the player into realms 1, 2, and 3. Its four screenshots are pixel-identical to the pre-banking ROM. `make playtest` runs both tests, and both pass.
3. **Data done, display pending.** Town and castle data is in banks 4 and 5 (`tools/build_town.py`, outputs `data/banked/bank4.bin` and `bank5.bin`). Each bank holds: unique 8×8 tiles (town 266, castle 151); a 35×24 tilemap of 16-bit entries relative to the screen's tile base; and the 40×22 collision grid (column-major, `index = x * 22 + y`, verified by the decoded layout). The picture is decoded with the engine's HGR rules, and the output matches the in-game town. Planned VRAM base for screen tiles: 96–383 (font assumed at 0–95; not yet verified in an emulator).
4. **Done (town and castle view).** Display, walking, and collision work. Source: `src/town.c`, `src/town.h`. The view scrolls in 8-px steps (decided: horizontal scroll). `tests/town.txt` checks the town at (20,20) and (25,20), a forced redraw, and the castle at (1,11). `tools/town_check.py` compares each frame pixel-for-pixel with the picture decoded from the disk. All frames match. Entry and exit work: button 2 (E) on an overworld tile of type 6 opens the town at (20,20), and on type 4 opens the castle at (1,11). The town exits through the bottom edge and the castle through the left edge, back to the overworld tile the player came from. `tests/enter.txt` covers both directions, and the entry frames match the decoded pictures. Not done: NPCs and shops, and the 16x16 player sprite, which is larger than the original 7x7 cell.
5. Add space and Mondain assets last.

**Input map (SMS pad).** Button 1 = start on the title, A (attack) in the overworld. Button 2 = E (enter a town, castle, or dungeon from its tile). Directions move. Other engine commands (T, B, I, etc.) are not yet mapped.

**Open question for the town view.** The picture is 280 px wide and the SMS screen is 256 px. Options: (A) horizontal scroll, 1:1 pixels, camera follows the player over a 24-px range; (B) scale to 256 px, showing the whole town with slight distortion; (C) crop the right 24 px. Option A is closest to the original.

**Current numbers.** The fixed bank (banks 0 and 1) holds 7,450 bytes of code and data. Banks 2–3 hold the overworld map (8,192 bytes each). Banks 4–5 hold the town (11,072 bytes) and castle (7,392 bytes). The ROM is 96 KiB (`ihx2sms` pads to the largest bank). All four banks were checked at their expected ROM offsets.


## Dungeons (status)

**Working.** E (button 2) on an overworld tile of type 7 enters dungeon depth 1 at (1,1), facing south. Up moves forward, left and right turn, and down turns around. Walls and tiles above 9 block movement. A trapdoor (tile 2) drops the player one floor. Button 2 climbs a ladder: tile 8 goes up, and tiles 7 and 9 go down. Climbing up from depth 1 returns to the overworld. The floor is drawn top-down, not in first person. `tests/dungeon.txt` covers entry, a forward move, a blocked turn, and the climb out.

**Source.** `src/dungeon.c` and `src/dungeon.h`, following `sceneDungeon.c` and `playerDungeon.c`.

**Decision: floor layouts are not reproducible across platforms.** The engine seeds its generator from the world position, then from the game clock (`srand(-player.time)`), using the host C library's `rand()`. The original's floors therefore depend on the platform and the clock, so this port uses its own deterministic generator. The fixed second seed is `0x5EED0000 + depth`. If exact parity with a particular platform is wanted, that needs a decision.

**Not done.**
- First-person wireframe view (`dungeonRenderer.c`). This is the largest remaining dungeon piece.
- Monsters (`sceneDungeon_spawnEnemies`) and combat.
- Chests, doors, and the other C-command actions.
- Trapdoor health cost, and the player record.
- Trapdoor landing: the port returns the player to (1,1). The engine keeps the coordinates. Confirm against the source before relying on this.

**VRAM.** Dungeon colour tiles use slots 362–369. Those slots are unused by the town (96–361) and the terrain (384–447).

## First-person dungeon view (in progress)

**Done.** The perspective table `PE%` is decoded from `BEVERY` by `tools/extract_dungeon_table.py`, which mirrors the engine's name search and array offsets. It is stored as `data/dungeon_pe.bin` (11×4 big-endian int16, `{left, right, top, bottom}` per distance). Row 0 is the full view `[0, 279, 0, 159]`, and each row shrinks toward the centre, so the table is valid.

**Open decision.** The engine's view is 280×160 px. The SMS shows 256 px across. Options: (A) scale horizontally to 256 px, keeping 160 rows at 1:1; (B) crop 24 px from the sides; (C) scroll, which does not suit a first-person view.

**Not done.** The wall-drawing pass (`dungeonRenderer_update`), line rasterisation, the HGR colouring pass (the port would draw in one colour), monsters, chests, and doors.

### Status of the first-person view

**Working.** The first-person view draws. The cause of the earlier overworld picture was a VRAM write during the tilemap pass: `tile_for` loads tiles with `SMS_loadTiles`, which moved the VDP address away from the sequential `SMS_setNextTileatXY`/`SMS_setTile` cursor. Each cell now uses `SMS_setTileatXY`. `shots/41_probe_settled.png` and `shots/40_dungeon_depth1.png` show the view. `tests/dungeon.txt` passes, and `make playtest` passes all five suites.

**Still open.**
- **Input lost during the redraw.** The main loop reads input once per pass, and the redraw takes roughly 20 frames inside that pass. A short key tap during the redraw is dropped. The test waits for each redraw (`run 600`). Fix by speeding up the redraw or splitting it across frames, then latching input.
- **Monsters and console messages** are not drawn.
- **Parity:** the view is checked by eye and against the engine's logic, not pixel-compared with the original.

### Device report: stale band in the dungeon view (fixed, not yet confirmed on device)

A RetroArch phone screenshot showed a full-width band of overworld terrain tiles across roughly four rows, with the wireframe correct elsewhere. The emulator runs (`tests/dungeon.txt`, `tools/dungeon_scroll_probe.txt`) do not reproduce it.

Likely cause, two parts:
1. `dungeon_view_redraw` wrote only rows 0–23. The SMS nametable is 32×28, and rows 24–27 keep the overworld's tiles. Vertical scroll can bring them on screen.
2. The overworld and town leave BG scroll set (`apply_scroll` uses `hslot`/`vslot`). The dungeon never reset it.

Fix: the redraw now writes all 28 rows (rows 20 and below get the blank tile), and `dungeon_redraw_full` sets BG scroll X and Y to 0.

**Needs a device check.** The next step is to confirm the band is gone on the device after walking the overworld and then entering the dungeon.

### Input during the dungeon redraw (taps fixed, redraw still slow)

Cause: taps were lost during the redraw, because nothing sampled the joypad while the framebuffer was cleared and the view was rendered. The first samples only started about 20 frames after entry.

Fix: dungeon_sample_keys() runs during the framebuffer clear, in each render step, and in each row. It reads joystick port A (0xDC) directly, so SMSlib's own edge state is never touched. Calling SMS_getKeysStatus from the redraw had stolen the press from the main loop. New presses are latched in dgn_latched and merged with OR in dungeon_update.

tests/dungeon_input.txt (tap up right after entry) passes and is in make playtest.

Open: the redraw takes about 2 seconds. Sample counts show about 4 frames per row, so a full redraw is roughly 110 frames plus about 20 frames before the row loop. Presses are now kept, but the view still freezes for about 2 seconds on every step. Next: profile the phases (framebuffer clear, render_view, tile dedupe, SMS_setTileatXY) and cut the largest one.

### Dungeon redraw timing (measured, still slow)

Measured with chained waituntil calls on one redraw (frames at 60 Hz):
- framebuffer clear: about 8 frames
- render_view (wireframe): about 17 frames
- tile scan and tilemap writes: about 52 frames
- total: about 77 frames, roughly 1.3 seconds per step

Findings from the experiments:
- Removing the tilemap write saves only about 5 frames, so it is not the main cost.
- Skipping the tile lookup for non-blank cells saves about 20 frames.
- Blank cells now skip the lookup entirely.
- The hash was changed to rotate-and-xor, which reduces probe clustering.
- The framebuffer is cell-major, so reading a cell is a short copy. The tile scan dropped from about 62 to about 52 frames.

Options to cut the redraw further:
1. Redraw only changed cells. Keep the tile table between frames and write only cells whose bits differ from the last frame.
2. Replace the per-pixel line code with a faster rasteriser, or move hot loops to assembly.
3. Reduce work per step, for example by caching the static parts of the view.

Input during the redraw is kept by the sampler (see the section above), so this is a speed problem only.

### Cell cache between redraws (built, does not help turns)

dungeon_view.c keeps the tile table and each cell's last tile between redraws (prev_tile, cache_valid). A cell is rewritten only when its pixels differ from the tile it shows. The cache is reset in dungeon_enter, because other scenes overwrite VRAM. UNIQ_MAX is now 160; one view uses about 92 unique tiles.

Result: a turn changes almost every cell, so the cache skips little. tools/dungeon_turn_timing.txt measures about 80 frames per turn, about the same as before.

Input: in the same probe, one of six turns was lost, so input during the redraw is still not fully reliable. The sampler latches presses (see the input section above), but this case is not explained yet.

RAM: BSS ends at 0xDE37, so about 440 bytes remain before the stack. Watch this when adding RAM.

Next steps, in order of expected payoff:
1. Make the per-cell path cheaper for changed cells, or move it to assembly.
2. Move the line rasteriser to a faster routine, or to assembly.
3. Find the lost-input case with the turn probe, then fix the latch.

### Input sampling gap (partly fixed)

Cause found: the sampler ran once per row, about every two frames. A two-frame press could fall between two samples. The sampler now runs inside line(), in every cell of the scan, and every 128 bytes while clearing.

Result: tests/dungeon_input.txt passes, and make playtest passes all six suites. In tools/dungeon_turn_timing.txt, four of six turns registered (before the fix, three of six).

Still open:
- One turn was still dropped in that run.
- After that, dgn_dx read 1. A left or right turn cannot produce that value (dx only moves between 0 and -1 with those turns), so something else is changing the facing. The latch is merged by OR in dungeon_update, and LEFT is tested before RIGHT. Check whether the raw joystick read sets a bit that is not a turn key, and whether a press can combine with a stale bit.
- Two different keys pressed during one redraw merge into one action. The second press is lost. This is a known limitation of the OR merge.

### Input during the dungeon redraw: fixed (turn probe passes)

Correction: the earlier note about an impossible facing was wrong. From facing south, a left turn gives east (dx = 1). That is correct. The probe lost a press, so the later turns were out of step.

Final cause: the sampler restarted at each redraw, and main read the joypad before the VBlank wait. A press that started and ended in that gap was never seen. Presses last about two frames and the gap is at most one frame, so a press always overlaps the sample taken just after the wait.

Fix:
- main.c samples right after SMS_waitForVBlank() in dungeon mode.
- The sampler keeps its state across redraws. dungeon_reset_input() runs only on entry.
- The sampler also runs in every line() call, in every cell of the scan, and every 128 bytes while clearing.

Result: tools/dungeon_turn_timing.txt passes (six turns, final facing south), tests/dungeon_input.txt passes, and make playtest passes all six suites.

Known limitation: two different keys pressed during one redraw are merged by OR into one action, so the second is lost.

### Prerendered dungeon views: counted, full prerender rejected

Question: can every dungeon view be prerendered as a tilemap, so that a turn is a single VRAM write?

Method: tools/count_views.c mirrors generate_floor() (32-bit RNG, 16-bit seed) and the tile reads of render_view(). A view is identified by the render class of each tile it reads, from the first cell to the first solid wall, plus the facing. The count covers all 65,536 seeds, depths 1 to 10, and every passable cell in four facings.

Result: 873,470 distinct views. Reachability from the entry cell made no difference, because every passable cell was reachable. Depth never goes past 10, so the range is complete.

Cost: at about 1.5 KB per tilemap, storing all of them needs more than 1.3 GB. Even the real entrance seeds, a small subset of the 65,536, would still need tens of MB. Full prerender is not feasible on this ROM.

Options still open:
- Speed up the runtime path. Build the 768-entry tile index in RAM, then write the tilemap in one VRAM burst instead of per-cell SMS_setTileatXY. Move the 8-byte pattern hash and compare into assembly. Estimated gain 2 to 4 times. Not measured.
- Keep the current redraw and accept about 1.3 seconds per turn.

### Redraw speed: measured breakdown (after the two-pass tilemap write)

Redraw split into phases, with dgn_dbg_stage markers (tools/dungeon_redraw_stages.txt):

| Phase                                         | Frames (2 turns) |
|-----------------------------------------------|------------------|
| Clear framebuffer (C loop, 5120 bytes)        | 7                |
| render_view (lines into framebuffer)          | 15 to 25         |
| Pass 1: cell scan, hash, tile_for, tile_matches | 52 to 64       |
| Pass 2: nametable rows (one address per row)  | 4 to 5           |

Total per turn: 86 to 89 frames (tools/dungeon_turn_timing.txt, 6 of 6 pass).

What changed in this round:
- Pass 2 writes each nametable row with SMS_setNextTileatXY plus sequential SMS_setTile. Before, every cell set its own address. Pass 2 is now 5 frames, but the turn time did not change, so the old per-cell address set was not the cost.
- Pass 1 only picks tile indices and loads new tiles. It must finish before pass 2, because tile loads move the VRAM address.

Findings:
- Pass 1 is the cost. The sampler is cheap, a few hundred cycles per call.
- SDCC's generated Z80 code is slow for these loops: about 80 cycles per simple loop iteration. Each cell does an 8-byte loop, an 8-byte compare, and a hash probe.
- An inline __asm LDIR clear failed to assemble with SDCC 4.5 (sdasz80: "<a> Invalid Addressing Mode"). It was reverted. The clear is only 7 frames, so it is not the priority.

Options still open:
- Assembly for the whole pass-1 cell loop, in a separate src/*.s file (needs a Makefile rule using sdasz80). Estimated 3 to 4 times on pass 1, roughly 25 to 30 frames saved. Not written yet.
- Accept about 1.5 seconds per turn for now.

Temporary code to remove before release: dgn_dbg_stage markers in src/dungeon_view.c. tools/dungeon_redraw_stages.txt reads them.

### Redraw speed: assembly cell scan (done)

Change: the hash and table lookup for all 640 cells is now in src/scan_asm.s (Z80, built with sdasz80 from the Makefile). It writes each cell's tile index to prev_tile. A pattern not yet in the table gets 255, and dungeon_view.c loads it with tile_for(). Pass 2 is unchanged. tile_matches() and the cache check in pass 1 were removed. Pass 2 rewrites every cell, so the cache check added nothing.

Measured (dgn_dbg_stage markers, tools/dungeon_redraw_stages.txt):

| Phase                   | Before | After   |
|-------------------------|--------|---------|
| Clear                   | 7      | 7       |
| render_view             | 15-25  | 15-25   |
| Pass 1 (cell scan)      | 52-64  | 14      |
| Pass 2 (nametable)      | 4-5    | 4       |
| Total per turn          | 86-89  | 43-49   |

The first turn after entry is slower in pass 1 (41 frames), because it loads new tiles. Later turns load few.

Checks:
- tools/dungeon_view_probe.txt: the settled view is pixel-identical to the earlier known-good shots/41_probe_settled.ppm.
- tools/dungeon_turn_timing.txt: all six turns pass.
- make playtest: 6 of 6 ALL PASSED.

Remaining cost: render_view (15-25 frames) and the clear (7). Pass 1 still does the new-tile path in C.

Build notes:
- sdcc does not accept .s files. The Makefile calls sdasz80 directly for scan_asm.rel.
- Long jr branches fail with "Branching Range Exceeded". All branches in scan_asm.s are jp.
- After a reset, /usr/bin/sdcc was missing again. Reinstall with sudo apt-get install sdcc.

Open items:
- src/dungeon_tables.c line 91 has the value 53445 in an int table. The compiler warns it overflows a signed int. Check which table it belongs to, and whether the game reads it.
- Temporary dgn_dbg_stage markers are still in src/dungeon_view.c for the stage probe. Remove them before release.


## Game text and the monster table (banks 6 and 7)

**Source.** `tools/ultima_text.py` is a Python mirror of the engine's text loader in `sceneDiskLoader.c`: the quoted strings of the BASIC programs and the SET files (`ultimaStrings[]`), the name tables in BEVERY (stats, races, types, armours, vehicles, weapons, spells, places), and the enemy table (name, group, rank, decoded from Applesoft floats). The engine's name overrides (monsters 6, 7, 10, ... from strings 14-25, spell 3, place 44) are applied the same way.

**Check.** On the user's disks it finds 1264 strings, and the numbers match the engine's uses: 98 is "COMMAND: ", 252 "YOU ARE BEING ATTACKED", 259 ", YOU ARE DEAD.". The enemy table has 46 entries; 6-20 are the overworld monsters (sea 6-9, woods 10-14, land 15-20).

**Layout.** `tools/build_text.py` writes the header, a 16-bit index and the first strings to bank 6, and the rest to bank 7 (index bit 15). The header holds the first id of each name table and the monster group and rank bytes. `src/text.c` copies a string into a caller buffer and restores the slot-2 bank, so no banked pointer escapes. The C code uses the engine's string numbers directly.

## Console and stats panel

**Layout.** As in the original's 280x192 screen, the map takes the top 160 px (10 terrain rows, was 12) and the bottom four text rows are the console. Columns 0-20 hold the last four console lines; columns 22-31 hold H.P., FOOD, EXP. and GOLD with the labels from the disk (strings 90, 92, 94, 96). The original console is about 29 characters wide, so lines longer than 21 wrap at a space. Colour, flash and typewriter codes (`^1`, `^F`, `^T1`) are dropped.

**The split.** The map scrolls on both axes through the 32x28 ring; the console must not scroll sideways. A line interrupt at line 159 writes X scroll 0 (`frame_interrupt` in `main.c`), so the console always reads tilemap columns 0-31. Vertical scroll cannot change mid-frame on the SMS, so the console lives in the ring too, at rows `vslot+20..vslot+23`, and a vertical step rewrites all four of its rows.

**Restoring the map's scroll (flicker fix).** The split only sets X to 0; the map's X scroll has to come back before the next frame. At first the main loop's VRAM phase did that. A turn's logic (formatting console text, the monsters' turn) plus the VRAM phase can run past one frame. The next VBlank was then missed, and that frame showed the map at X = 0: a 16- to 248-px jump on almost every step, in every direction. This showed on hardware; the old renderer drew each screenshot from the final register state, so it hid it. Now the VBlank interrupt (`SMS_setFrameInterruptHandler`, `vblank_interrupt` in `main.c`) writes X from `ow_scroll_x` every frame, however long the logic takes. `ow_scroll_x` changes only in `apply_scroll`, inside the VRAM phase, together with the streamed edge it belongs to. Vertical scroll is not involved: the VDP latches it once per frame, and it is only written during VBlank.

`tests/walk_frames.txt` walks 90 frames in each direction with `liverender` on and saves every frame; `tools/check_walk_frames.py` fails if any frame is not the previous one or the previous one moved by one tile, or if the console rows show non-text colours. With the VBlank restore turned off it finds 43 jumps; with it, none.

**Interrupt safety.** The VDP address latch is shared, so neither handler may write a register while main code is writing VRAM. Main sets `vram_busy` for its whole VRAM phase, and a handler skips its write (and counts `hud_split_missed`, display on only) if it fires during it. Every playtest suite now passes with `hud_split_missed == 0` asserted in `tests/combat.txt`.

**Making it fit.** The VRAM phase must end before line 159: about 52,000 cycles from VBlank on NTSC. Measured with the new debug timers (`src/debug.h`), the first version took up to 107,000 cycles on a step. Changes:
- Stat values are turned into tiles in the logic phase (`hud_tick`), not in the VRAM phase. Labels are cached at `hud_reset`.
- `src/hud_asm.s` writes runs of name-table cells at 29-31 cycles per write (about 60 per cell), against about 150 per cell through `SMS_setTile` from C. `expectpace 0` with `pacemin 27` passes on every suite.
- A vertical step streams the new map row as two runs per tilemap row, with one bank switch for its 16 terrain lookups.
- The terrain lookup uses byte coordinates and shifts; `%` and 32-bit divides are gone from the per-frame paths.

Worst case now: 45,300 cycles for a vertical step with a full console redraw (row 28,600, console 15,900). That leaves about 7,000 cycles before line 159. PAL has a longer VBlank, so NTSC is the limit.

**Latency.** The emulator applies a register write during line N from line N+1. On hardware the write can land a line later. The font is loaded one pixel lower (`load_font` in `main.c`) so the first pixel row of every glyph is blank; a split that lands on line 161 is then invisible. Glyphs that use their bottom row (comma, semicolon, underscore) already have a blank top row and stay as they are. **This needs a check on an accurate emulator or a console.**

**Pacing.** The engine shows queued lines one every 0.15 s and blocks input until the queue is empty. There is no RAM for queued text here, so a queued line is shown at once and adds 9 frames (0.15 s) of input wait. The turn rhythm is the same; the lines do not appear one by one.

## Overworld combat

**Source.** `src/encounter.c` follows `sceneOverworld_spawnMonsters`, `sceneOverworld_resolveEncounter`, and the dodge, attack and kill parts of `playerOverworld.c`. `src/overworld.c` follows the turn loop of `sceneOverworld_update` (input waits for the console and the lag timer; after each action the monsters act and "COMMAND: " appears), `playerOverworld_updateMovement`, `_updateEnter` and `sceneOverworld_attemptResurrection`.

**What works.**
- Encounters: after each action with no monster, a 10 % chance (5 % on land) of a group by terrain, with the engine's HP and group-size formulas, including the growth with game time.
- A monster group blocks the next step unless the player dodges (strength + agility, vehicle roll). The monster sprite (from `OUT.SHAPES`, sea / land / woods, decoded with the engine's shape-table rules) is drawn in the blocked direction.
- Monster attacks after each action, with the armour defence formula; hits reduce H.P.
- Attack (button 1): weapon range for sea monsters and hidden archers, the "not applicable" weapons, the hit roll and damage. Kills give experience and gold; the next group member gets new HP.
- Death (H.P. or food at 0): the resurrection message, weapons and gold lost, 99 H.P., 20 food, a random land tile in realm 0 (the open-sosaria softlock check is kept), 5 s lag.
- Food and time: the step cost `(7 - vehicle) / 14`, the action time `+0.6`, the 5-second "PASS" penalty. Food and time are a whole part plus hundredths, so no float or 32-bit divide is needed.
- Entering: the engine's messages ("COMMAND: ENTERING", the place name from the TD$ table, "THE CITY OF ...") and its 1.5 s transition. Signposts show their name only.

**Random numbers.** The engine uses `rand01()` floats from the host C library. Here `rnd()` is a 16-bit xorshift (7, 9, 8) returning a fraction `r / 65536`, and the formulas use fixed point. The seed comes from the frame counter at the title screen. Playtests poke `rng_state` for a fixed sequence.

**Not done.** Vehicles (and so Fire, Board, X-it), spells (Cast), Ztats, Inform, Get, Open, Drop, Ready, signpost stat gains, the alert sounds. The pad has two buttons: button 1 is Attack and button 2 is Enter. The other commands will need a menu.

**Character.** There is no character generator. A new game uses `player_new_game()`: the engine's gold, food, H.P., weapon (dagger) and armour (leather), strength and agility 20, the other stats 15, name "STRANGER".

**Tests.** `tests/combat.txt`: new-game stats, a monster blocking the path, its sprite, attacking until it dies (experience and gold go up), attacking nothing, death and resurrection (99 H.P., 0 gold, 20 food, in realm 0), and a random encounter while walking. The other suites poke `enc_enabled 0` so monsters do not block their walks, and entry waits went from 120 to 300 frames for the 1.5 s transition.

## RAM budget

The SMS has 8 KB. After this round the data ends at 0xDF2F and the deepest stack seen in any suite is 0xDF6D (`spmin`), so about 60 bytes are free.

| User | Bytes |
|---|---|
| Dungeon view (`fb`, `uniq`, `hash_slot`, `prev_tile`) | about 7,300 |
| SMSlib sprite table | 192 |
| Console (4 x 21 text, labels, values) | 124 |
| Player record | 37 |
| Everything else | about 270 |

The dungeon buffers are only used in the dungeon, but nothing else large can share them yet, because the console and player state must live in every scene. Options before adding more state (vehicles map, inventory, town NPCs):
1. Compare dungeon cell patterns against VRAM instead of keeping `uniq` (1,280 bytes) in RAM. Costs a VRAM read per hash hit.
2. Use cartridge RAM as work RAM for the dungeon framebuffer. Shares the slot-2 window with the data banks, so the dungeon code would have to map it around its reads.
3. Put scene-only state for towns and the overworld in a union with the dungeon buffers.

Check `spmin` against `s__HEAP` in `opensosaria.map` after every change.

## Test tooling

`tools/smstest-patches.patch` changes the skill's `smstest.c`:
- horizontal scroll is latched at the start of each line, so screenshots show the HUD split;
- addresses can be `sym+offset`, for fields of `player`;
- `s8` reads a signed byte (`enc_monster` is -1 for none);
- `timers` also prints the maximum per timer;
- `liverender 1` draws each line from the VRAM, CRAM and registers as they are when the beam reaches it (vertical scroll latched at line 0), so screenshots show late VRAM writes and mid-frame register changes as hardware does. Without it, a screenshot is drawn from the state at the moment it is taken.

Build: apply the patch to `assets/smstest/smstest.c` from the skill, then `gcc -O2 -w -o ~/.devkitsms/bin/smstest smstest.c z80.c`. These are worth sending upstream to the skill.

Debug timers: `make clean && make EXTRA_CFLAGS=-DDEBUG_TIMERS` writes port 0x06 around the VRAM phase (timer 0), `overworld_vblank` (1) and the console draw (2). Port 0x06 on a real SMS mirrors the memory control port, so that build must never run on hardware.

**Logic cost (open).** Formatting a console line in C is slow: a "COMMAND: EAST" replacement takes about 21,000 cycles and an attack about 55,000, so a turn's logic can spill into a second frame. It does not affect correctness (the VRAM phase starts at the next VBlank), but `emit` and `text_get` are the place to speed up next.
