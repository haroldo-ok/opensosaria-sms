/* ---------------------------------------------------------------------------
 * Overworld implementation. See overworld.h for the layout.
 *
 * Screen: rows 0-19 (160 px, 10 terrain tiles) show the map; rows 20-23 are
 * the console and stats (hud.c), as in the original's 280x192 screen.
 *
 * Camera state:
 *   cam_tx, cam_ty   world tile coordinates of the view's top-left tile (wrap at 172)
 *   hslot            tilemap column (0..31) that holds the view's left edge
 *   vslot            tilemap row (0..27) that holds the view's top edge
 * hslot/vslot advance by 2 cells per tile step and wrap at 32/28 cells. They
 * never reset at the 172-tile world wrap, so the ring stays continuous.
 *
 * The HUD lives in the same ring, in rows vslot+20..vslot+23. Vertical scroll
 * moves it, so a vertical step rewrites all four HUD rows. Horizontal scroll
 * does not: the line interrupt sets X scroll to 0 at line 160 (main.c), so
 * the HUD always reads tilemap columns 0-31.
 *
 * Turn logic follows sceneOverworld_update and playerOverworld_update:
 * input waits while console lines are queued or a lag timer runs; after each
 * action the monsters get their turn (encounter.c) and "COMMAND: " appears.
 * ------------------------------------------------------------------------- */
#include "SMSlib.h"
#include "data.h"
#include "overworld.h"
#include "player.h"
#include "hud.h"
#include "text.h"
#include "encounter.h"
#include "debug.h"

#define VIEW_COLS_CELLS  32
#define MAP_ROWS_CELLS   28
#define HUD_ROW_OFFSET   (OW_VIEW_TILES_H * 2)   /* 20: first HUD row below the map */
#define PLAYER_COL       8                       /* view tile of the player */
#define PLAYER_ROW       5

/* Frame counts for the engine's float timers (60 Hz). */
#define REPEAT_MOVE      6     /* keyRepeatDelay 0.1 */
#define REPEAT_BLOCKED   18    /* keyRepeatDelay 0.3 */
#define LAG_ATTACK       90    /* lagTime 1.5 */
#define LAG_RESURRECT    300   /* lagTime 5.0 */
#define ENTER_DELAY      90    /* vmExecuter_createSceneTransition(1.5) */
#define IDLE_PASS        300   /* waitingTime 5.0 */

/* Disk text ids (ultimaStrings numbers). */
#define T_COMMAND        98
#define T_PASS           99
#define T_NORTH          117
#define T_SOUTH          118
#define T_EAST           119
#define T_WEST           120
#define T_NO_WATER       122
#define T_MOUNTAINS      123
#define T_ENTERING       163
#define T_NOT_APPLIC     164
#define T_CITY_OF        180
#define T_YOU_ARE_DEAD   259
#define T_RESURRECTION   260

unsigned int player_tx = 40;     /* start position from the original game */
unsigned int player_ty = 40;
unsigned char overworld_enter = 0;
unsigned char ow_acted = 0;      /* playerActed */
unsigned int  ow_lag = 0;        /* lagTime, frames */
unsigned char ow_moves = 0;      /* steps taken, for the playtests */
unsigned char overworld_respawned = 0;

static unsigned int cam_tx, cam_ty;
static unsigned char hslot, vslot;
static unsigned char repeat_timer;
static unsigned int  idle_frames;
static unsigned char enter_pending;      /* place to enter once the lag is over */
static unsigned char respawn_pending;
static unsigned char respawn_x, respawn_y;
static signed char pending_dx, pending_dy;   /* set by a step, consumed in VBlank */
static unsigned char hud_full;               /* HUD rows moved: rewrite all four */

static const char player_name[] = "STRANGER";   /* no character generator yet */

/* Map data lives in ROM banks (see PORTING_PLAN.md): realms 0-1 in bank 2,
 * realms 2-3 in bank 3. Each bank's array is its first symbol, so it reads at
 * 0x8000 once the bank is mapped. */
extern const unsigned char bank2_data[];
extern const unsigned char bank3_data[];

/* Terrain byte for world tile (wx, wy), wx/wy in 0..171: tile id in the high
 * nibble, place index in the low nibble. Pages the map bank in and restores
 * the previous slot-2 bank before returning. */
unsigned char overworld_terrain(unsigned int wx, unsigned int wy) {
    unsigned char x = (unsigned char)wx, y = (unsigned char)wy;
    unsigned char saved, v, bank = 2;
    unsigned int off;
    const unsigned char *map = bank2_data;
    if (y >= 86) { y -= 86; bank = 3; map = bank3_data; }
    if (x >= 86) x -= 86, off = 4096u; else off = 0;
    if (x >= 64 || y >= 64) return 0;               /* sea around the realm */
    off |= ((unsigned int)y << 6) | x;

    saved = SMS_getROMBank();
    SMS_mapROMBank(bank);
    v = map[off];
    SMS_mapROMBank(saved);
    return v;
}

#define terrain_at overworld_terrain

static unsigned char player_tile(void) {
    return terrain_at(player_tx, player_ty) >> 4;
}

/* Realm (world) index, as in the engine: (ty / 86) * 2 + tx / 86. */
static unsigned char player_world(void) {
    return (unsigned char)((player_ty >= 86 ? 2 : 0) + (player_tx >= 86 ? 1 : 0));
}

/* Write one 16x16 terrain tile at tilemap cell (cx, cy) as 2x2 SMS tiles.
 * cx is always even (the ring moves in 2-cell steps), so cx + 1 is on the
 * same tilemap row and each pair is one sequential write. */
static void place_tile(unsigned int wx, unsigned int wy, unsigned char cx, unsigned char cy) {
    unsigned char t = terrain_at(wx, wy) >> 4;
    unsigned int base = OW_TILE_BASE + ((unsigned int)t << 2);
    unsigned char cy2 = (cy + 1 == MAP_ROWS_CELLS) ? 0 : cy + 1;
    SMS_setNextTileatXY(cx, cy);
    SMS_setTile(base);
    SMS_setTile(base + 1);
    SMS_setNextTileatXY(cx, cy2);
    SMS_setTile(base + 2);
    SMS_setTile(base + 3);
}

/* The 16 terrain ids of world row wy from cam_tx on, with one bank switch. */
static void terrain_run(unsigned int wy, unsigned char *out) {
    unsigned char y = (unsigned char)wy, x = (unsigned char)cam_tx, i, saved;
    const unsigned char *map = bank2_data;
    unsigned char bank = 2;
    if (y >= 86) { y -= 86; bank = 3; map = bank3_data; }
    saved = SMS_getROMBank();
    SMS_mapROMBank(bank);
    for (i = 0; i < OW_VIEW_TILES_W; i++) {
        unsigned char lx = x;
        unsigned int off = 0;
        if (lx >= 86) { lx -= 86; off = 4096u; }
        out[i] = (lx >= 64 || y >= 64) ? 0 : (map[off | ((unsigned int)y << 6) | lx] >> 4);
        if (++x == OW_WORLD_TILES) x = 0;
    }
    SMS_mapROMBank(saved);
}

/* Writes one tilemap row of the 2x2 terrain cells: half 0 = top, 2 = bottom. */
static void put_row_cells(const unsigned char *ids, unsigned char half, unsigned char cy) {
    unsigned char cells[2 * OW_VIEW_TILES_W];
    unsigned char i, first;
    /* Terrain tile base 384 = 0x180: low byte 0x80 + 4 * id + corner, high byte 1. */
    for (i = 0; i < OW_VIEW_TILES_W; i++) {
        unsigned char lo = (unsigned char)(0x80 + (ids[i] << 2) + half);
        cells[2 * i] = lo;
        cells[2 * i + 1] = lo + 1;
    }
    /* The view starts at column hslot and wraps at 32. */
    first = (unsigned char)(VIEW_COLS_CELLS - hslot);
    hud_put_hi = 1;
    SMS_setNextTileatXY(hslot, cy);
    hud_put_src = cells;
    hud_put_n = first;
    hud_put_tiles();
    if (first < 2 * OW_VIEW_TILES_W) {
        SMS_setNextTileatXY(0, cy);
        hud_put_src = cells + first;
        hud_put_n = (unsigned char)(2 * OW_VIEW_TILES_W - first);
        hud_put_tiles();
    }
}

static void stream_row(unsigned int wy, unsigned char cy) {
    unsigned char ids[OW_VIEW_TILES_W];
    unsigned char cy2 = (cy + 1 == MAP_ROWS_CELLS) ? 0 : cy + 1;
    terrain_run(wy, ids);
    put_row_cells(ids, 0, cy);
    put_row_cells(ids, 2, cy2);
}

static unsigned int wrap172(int v) {
    while (v < 0) v += OW_WORLD_TILES;
    while (v >= OW_WORLD_TILES) v -= OW_WORLD_TILES;
    return (unsigned int)v;
}

static unsigned char hud_row0(void) {
    return (unsigned char)((vslot + HUD_ROW_OFFSET) % MAP_ROWS_CELLS);
}

/* Called only in the VRAM phase (or with the display off), after the edge
 * for this camera position has been streamed. ow_scroll_x is what the VBlank
 * interrupt restores after the HUD split (main.c), so it must only change
 * together with the tilemap it belongs to. */
volatile unsigned char ow_scroll_x = 0;

static void apply_scroll(void) {
    /* Horizontal register is negated relative to vertical (see smslib notes). */
    ow_scroll_x = (unsigned char)((256 - (unsigned int)hslot * 8u) & 255u);
    SMS_setBGScrollX(ow_scroll_x);
    SMS_setBGScrollY((unsigned char)((unsigned int)vslot * 8u));
}

void overworld_init_graphics(void) {
    SMS_loadTiles(ow_tiles_bin, OW_TILE_BASE, ow_tiles_bin_size);
    SMS_loadBGPalette(ow_palette_bin);
    SMS_loadSpritePalette(ow_sprite_palette_bin);
    SMS_loadTiles(ow_sprites_bin, OW_PLAYER_TILE, ow_sprites_bin_size);
    SMS_loadTiles(ow_enemies_bin, OW_ENEMY_TILE, ow_enemies_bin_size);
}

/* sceneOverworld_init: a fresh console with the prompt. Called when the game
 * starts and whenever the player comes back to the overworld. */
void overworld_begin(void) {
    hud_reset();
    sb_clear(); sb_text(T_COMMAND, TEXT_MAX); hud_add(hud_sb);
    ow_acted = 0;
    ow_lag = 0;
    repeat_timer = 0;
    idle_frames = 0;
    enter_pending = 0;
    enc_shown = 0;
}

void overworld_redraw_full(void) {
    unsigned char r, c;

    cam_tx = wrap172((int)player_tx - PLAYER_COL);
    cam_ty = wrap172((int)player_ty - PLAYER_ROW);
    hslot = (unsigned char)((2u * cam_tx) & 31u);
    vslot = (unsigned char)((2u * cam_ty) % MAP_ROWS_CELLS);

    for (r = 0; r < OW_VIEW_TILES_H; r++) {
        unsigned char cy = (vslot + 2u * r) % MAP_ROWS_CELLS;
        for (c = 0; c < OW_VIEW_TILES_W; c++) {
            unsigned char cx = (hslot + 2u * c) & 31;
            place_tile(wrap172((int)cam_tx + c), wrap172((int)cam_ty + r), cx, cy);
        }
    }
    hud_draw(hud_row0(), 1);
    apply_scroll();
    pending_dx = pending_dy = 0;
    hud_full = 0;
}

/* Called after VBlank: scroll, stream the new edge of the view for the last
 * step, then the HUD. The writes are paced (SMSlib), and they finish before
 * the beam reaches the HUD rows. */
void overworld_vblank(void) {
    unsigned char i;

    apply_scroll();

    if (pending_dx) {                     /* new rightmost or leftmost column */
        unsigned char cx = (pending_dx > 0) ? ((hslot + 30) & 31) : hslot;
        unsigned int wx = wrap172((int)cam_tx + (pending_dx > 0 ? 15 : 0));
        unsigned int wy = cam_ty;
        unsigned char cy = vslot;
        for (i = 0; i < OW_VIEW_TILES_H; i++) {
            place_tile(wx, wy, cx, cy);
            if (++wy == OW_WORLD_TILES) wy = 0;
            cy += 2;
            if (cy >= MAP_ROWS_CELLS) cy -= MAP_ROWS_CELLS;
        }
    } else if (pending_dy) {              /* new bottom or top row */
        unsigned char cy = vslot;
        unsigned int wy = cam_ty;
        if (pending_dy > 0) {
            cy += HUD_ROW_OFFSET - 2;
            if (cy >= MAP_ROWS_CELLS) cy -= MAP_ROWS_CELLS;
            wy = wrap172((int)cam_ty + OW_VIEW_TILES_H - 1);
        }
        stream_row(wy, cy);
    }
    pending_dx = pending_dy = 0;

    DBG_START(2);
    hud_draw(hud_row0(), hud_full);
    DBG_STOP(2);
    hud_full = 0;
}

/* Moves the camera and the ring with the player (one tile). */
static void scroll_step(int dx, int dy) {
    cam_tx = wrap172((int)cam_tx + dx);
    cam_ty = wrap172((int)cam_ty + dy);
    if (dx > 0)      hslot = (hslot + 2) & 31;
    else if (dx < 0) hslot = (hslot + 30) & 31;
    if (dy > 0)      vslot = (vslot + 2) % MAP_ROWS_CELLS;
    else if (dy < 0) vslot = (vslot + MAP_ROWS_CELLS - 2) % MAP_ROWS_CELLS;
    if (dy) hud_full = 1;
    pending_dx = (signed char)dx;
    pending_dy = (signed char)dy;
}

static void command_line(unsigned int id) {
    sb_clear(); sb_text(T_COMMAND, 14); sb_text(id, 15);
    hud_replace_last(hud_sb);
}

/* playerOverworld_updateMovement for one direction. Returns 1 (acted). */
static unsigned char try_step(signed char dx, signed char dy, unsigned int dir_text) {
    unsigned int nx = wrap172((int)player_tx + dx);
    unsigned int ny = wrap172((int)player_ty + dy);
    unsigned char tile = terrain_at(nx, ny) >> 4;

    idle_frames = 0;
    command_line(dir_text);

    if (!enc_try_dodge(dx, dy, player_tile())) {
        repeat_timer = REPEAT_BLOCKED;
        return 1;
    }
    /* On foot: sea (0) and mountains (3) block. Vehicles come later. */
    if (tile == 0 || tile == 3) {
        sb_clear(); sb_text(tile == 0 ? T_NO_WATER : T_MOUNTAINS, TEXT_MAX);
        hud_add(hud_sb);
        repeat_timer = REPEAT_BLOCKED;
        return 1;
    }

    player_tx = nx;
    player_ty = ny;
    scroll_step(dx, dy);
    repeat_timer = REPEAT_MOVE;
    ow_moves++;
    player_consume_food();
    return 1;
}

/* playerOverworld_updateEnter */
static unsigned char try_enter(void) {
    unsigned char v = terrain_at(player_tx, player_ty);
    unsigned char tile = v >> 4, type = v & 0x0F;
    unsigned char place = player_world() * 20u + type;

    idle_frames = 0;
    if (tile < 4 || tile > 7) {
        sb_clear(); sb_text(T_COMMAND, 10); sb_text(T_ENTERING, 10);
        hud_replace_last(hud_sb);
        sb_clear(); sb_text(T_NOT_APPLIC, TEXT_MAX); hud_add(hud_sb);
        return 1;
    }
    sb_clear(); sb_text(T_COMMAND, 15); sb_text(T_ENTERING, 15);
    hud_replace_last(hud_sb);

    sb_clear();
    if (tile == 4)      sb_text(text_table(TBL_PLACES, place + 1), TEXT_MAX);
    else if (tile == 5) sb_text(text_table(TBL_PLACES, place + 3), TEXT_MAX);
    else if (tile == 6) { sb_text(T_CITY_OF, TEXT_MAX); sb_text(text_table(TBL_PLACES, place + 13), TEXT_MAX); }
    else                sb_text(text_table(TBL_PLACES, place + 5), TEXT_MAX);
    hud_queue(hud_sb);

    if (tile == 5) return 1;      /* signposts: the messages and stat gains are not ported */

    hud_queue(" ");
    enter_pending = (tile == 6) ? 1 : (tile == 4) ? 2 : 3;
    ow_lag = ENTER_DELAY;
    return 0;                     /* a scene change, not a turn */
}

/* playerOverworld_update, idle state. Returns 1 when the player acted. */
static unsigned char player_input(unsigned int keys, unsigned int pressed) {
    if (repeat_timer) { repeat_timer--; return 0; }

    if (pressed & PORT_A_KEY_1) {            /* A(ttack) */
        idle_frames = 0;
        ow_lag = LAG_ATTACK;
        enc_attack();
        return 1;
    }
    if (pressed & PORT_A_KEY_2) return try_enter();    /* E(nter) */

    if (keys & PORT_A_KEY_UP)    return try_step(0, -1, T_NORTH);
    if (keys & PORT_A_KEY_DOWN)  return try_step(0, 1, T_SOUTH);
    if (keys & PORT_A_KEY_LEFT)  return try_step(-1, 0, T_WEST);
    if (keys & PORT_A_KEY_RIGHT) return try_step(1, 0, T_EAST);

    if (++idle_frames >= IDLE_PASS) {        /* waitingTime >= 5: "PASS" */
        idle_frames = 0;
        command_line(T_PASS);
        player_wait_penalty();
        return 1;
    }
    return 0;
}

/* sceneOverworld_attemptResurrection. Returns 1 if the player was dead. */
static unsigned char attempt_resurrection(void) {
    unsigned char i, tx, ty, tile;
    if (player_alive()) return 0;

    sb_clear(); sb_str(player_name); sb_text(T_YOU_ARE_DEAD, 14); hud_add(hud_sb);
    sb_clear(); sb_text(T_RESURRECTION, TEXT_MAX); hud_add(hud_sb);

    for (i = 0; i < PLAYER_WEAPONS; i++) player.weapons[i] = 0;
    player.hits = 99;
    player.weapon = 0;
    player.vehicle = 0;
    player.gold = 0;
    player.food = 20;
    player.food_frac = 0;
    player_stats_dirty = 1;

    /* A random land or woods tile in realm 0. The original does not check the
     * tile; open-sosaria does, to avoid softlocks, and so does this port. */
    do {
        tx = (unsigned char)(rnd() >> 10);       /* (int)(rand01() * 64) */
        ty = (unsigned char)(rnd() >> 10);
        tile = terrain_at(tx, ty) >> 4;
    } while (tile == 0 || tile >= 3);
    respawn_x = tx;
    respawn_y = ty;
    respawn_pending = 1;

    enc_monster = -1;
    enc_shown = 0;
    ow_acted = 0;
    ow_lag = LAG_RESURRECT;
    return 1;
}

void overworld_update(unsigned int keys, unsigned int pressed) {
    hud_tick();
    if (ow_lag) ow_lag--;
    if (hud_busy() || ow_lag) return;

    if (enter_pending) {
        overworld_enter = enter_pending;
        enter_pending = 0;
        enc_shown = 0;
        return;
    }
    if (attempt_resurrection()) return;

    if (respawn_pending) {
        respawn_pending = 0;
        player_tx = respawn_x;
        player_ty = respawn_y;
        overworld_respawned = 1;              /* main.c forces a full redraw */
        player_stats_dirty = 1;
    }

    if (ow_acted) {
        ow_acted = 0;
        enc_resolve(player_tile());
        if (player_alive()) {
            sb_clear(); sb_text(T_COMMAND, TEXT_MAX); hud_add(hud_sb);
        }
        return;
    }

    /* A step must be streamed in VBlank before the next one starts. */
    if (pending_dx || pending_dy) return;

    if (player_alive() && player_input(keys, pressed)) {
        ow_acted = 1;
        player_add_time(60);                  /* player.time += 0.6 per action */
    }
}

void overworld_add_sprites(void) {
    unsigned char x = PLAYER_COL * 16, y = PLAYER_ROW * 16 - 1;  /* SAT Y is drawn one line low */
    SMS_addSprite(x,     y,     OW_PLAYER_TILE);
    SMS_addSprite(x + 8, y,     OW_PLAYER_TILE + 1);
    SMS_addSprite(x,     y + 8, OW_PLAYER_TILE + 2);
    SMS_addSprite(x + 8, y + 8, OW_PLAYER_TILE + 3);

    if (enc_shown) {
        unsigned char t = OW_ENEMY_TILE + enc_sprite * 4u;
        x = (unsigned char)(x + enc_dx * 16);
        y = (unsigned char)(y + enc_dy * 16);
        SMS_addSprite(x,     y,     t);
        SMS_addSprite(x + 8, y,     t + 1);
        SMS_addSprite(x,     y + 8, t + 2);
        SMS_addSprite(x + 8, y + 8, t + 3);
    }
}
