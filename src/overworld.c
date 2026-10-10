/* ---------------------------------------------------------------------------
 * Overworld implementation. See overworld.h for the layout.
 *
 * Camera state:
 *   cam_tx, cam_ty   world tile coordinates of the view's top-left tile (wrap at 172)
 *   hslot            tilemap column (0..31) that holds the view's left edge
 *   vslot            tilemap row (0..27) that holds the view's top edge
 * hslot/vslot advance by 2 cells per tile step and wrap at 32/28 cells. They
 * never reset at the 172-tile world wrap, so the ring stays continuous.
 * ------------------------------------------------------------------------- */
#include "SMSlib.h"
#include "data.h"
#include "overworld.h"

#define VIEW_COLS_CELLS  32
#define VIEW_ROWS_CELLS  24
#define MAP_ROWS_CELLS   28
#define KEY_REPEAT_FRAMES 8

unsigned int player_tx = 40;     /* start position from the original game */
unsigned int player_ty = 40;
unsigned int food_tenths = 1000;
unsigned char overworld_enter = 0;

static unsigned int cam_tx, cam_ty;
static unsigned char hslot, vslot;
static unsigned char hold_timer;
static signed char pending_dx, pending_dy;   /* set by a step, consumed in VBlank */

/* Map data lives in ROM banks (see PORTING_PLAN.md): realms 0-1 in bank 2,
 * realms 2-3 in bank 3. Each bank's array is its first symbol, so it reads at
 * 0x8000 once the bank is mapped. */
extern const unsigned char bank2_data[];
extern const unsigned char bank3_data[];

/* Terrain byte for world tile (wx, wy), wx/wy in 0..171: tile id in the high nibble.
 * Pages the map bank in and restores the previous slot-2 bank before returning,
 * so callers in the fixed bank are unaffected. */
static unsigned char terrain_at(unsigned int wx, unsigned int wy) {
    unsigned int realm = 0, lx = wx, ly = wy, off;
    unsigned char saved, v;
    if (lx >= 86) { lx -= 86; realm += 1; }
    if (ly >= 86) { ly -= 86; realm += 2; }
    if (lx >= 64 || ly >= 64) return 0;             /* sea around the realm */
    off = ly * 64u + lx;

    saved = SMS_getROMBank();
    if (realm < 2) {
        SMS_mapROMBank(2);
        v = bank2_data[realm * 4096u + off];
    } else {
        SMS_mapROMBank(3);
        v = bank3_data[(realm - 2) * 4096u + off];
    }
    SMS_mapROMBank(saved);
    return v;
}

/* Write one 16x16 terrain tile at tilemap cell (cx, cy) as 2x2 SMS tiles. */
static void place_tile(unsigned int wx, unsigned int wy, unsigned char cx, unsigned char cy) {
    unsigned char t = terrain_at(wx, wy) >> 4;
    unsigned int base = OW_TILE_BASE + (unsigned int)t * 4u;
    unsigned char cx2 = (cx + 1) & 31;
    unsigned char cy2 = (cy + 1 == MAP_ROWS_CELLS) ? 0 : cy + 1;
    SMS_setTileatXY(cx,  cy,  base);
    SMS_setTileatXY(cx2, cy,  base + 1);
    SMS_setTileatXY(cx,  cy2, base + 2);
    SMS_setTileatXY(cx2, cy2, base + 3);
}

static unsigned int wrap172(int v) {
    while (v < 0) v += OW_WORLD_TILES;
    while (v >= OW_WORLD_TILES) v -= OW_WORLD_TILES;
    return (unsigned int)v;
}

static void apply_scroll(void) {
    /* Horizontal register is negated relative to vertical (see smslib notes). */
    SMS_setBGScrollX((unsigned char)((256 - (unsigned int)hslot * 8u) & 255u));
    SMS_setBGScrollY((unsigned char)((unsigned int)vslot * 8u));
}

void overworld_init_graphics(void) {
    SMS_loadTiles(ow_tiles_bin, OW_TILE_BASE, ow_tiles_bin_size);
    SMS_loadBGPalette(ow_palette_bin);
    SMS_loadSpritePalette(ow_sprite_palette_bin);
    SMS_loadTiles(ow_sprites_bin, 4, ow_sprites_bin_size);
}

void overworld_redraw_full(void) {
    unsigned char r, c;

    cam_tx = wrap172((int)player_tx - 8);
    cam_ty = wrap172((int)player_ty - 6);
    hslot = (unsigned char)((2u * cam_tx) & 31u);
    vslot = (unsigned char)((2u * cam_ty) % MAP_ROWS_CELLS);

    for (r = 0; r < OW_VIEW_TILES_H; r++) {
        unsigned char cy = (vslot + 2u * r) % MAP_ROWS_CELLS;
        for (c = 0; c < OW_VIEW_TILES_W; c++) {
            unsigned char cx = (hslot + 2u * c) & 31;
            place_tile(wrap172((int)cam_tx + c), wrap172((int)cam_ty + r), cx, cy);
        }
    }
    apply_scroll();
    pending_dx = pending_dy = 0;
}

/* Called from VBlank: stream the new edge of the view for the last step. */
void overworld_vblank(void) {
    unsigned char i;

    apply_scroll();

    if (pending_dx > 0) {                 /* moved right: new rightmost column */
        unsigned char cx = (hslot + 30) & 31;
        for (i = 0; i < OW_VIEW_TILES_H; i++) {
            unsigned char cy = (vslot + 2u * i) % MAP_ROWS_CELLS;
            place_tile(wrap172((int)cam_tx + 15), wrap172((int)cam_ty + i), cx, cy);
        }
    } else if (pending_dx < 0) {          /* moved left: new leftmost column */
        unsigned char cx = hslot;
        for (i = 0; i < OW_VIEW_TILES_H; i++) {
            unsigned char cy = (vslot + 2u * i) % MAP_ROWS_CELLS;
            place_tile(wrap172((int)cam_tx), wrap172((int)cam_ty + i), cx, cy);
        }
    } else if (pending_dy > 0) {          /* moved down: new bottom row */
        unsigned char cy = (vslot + 22u) % MAP_ROWS_CELLS;
        for (i = 0; i < OW_VIEW_TILES_W; i++) {
            unsigned char cx = (hslot + 2u * i) & 31;
            place_tile(wrap172((int)cam_tx + i), wrap172((int)cam_ty + 11), cx, cy);
        }
    } else if (pending_dy < 0) {          /* moved up: new top row */
        unsigned char cy = vslot;
        for (i = 0; i < OW_VIEW_TILES_W; i++) {
            unsigned char cx = (hslot + 2u * i) & 31;
            place_tile(wrap172((int)cam_tx + i), wrap172((int)cam_ty), cx, cy);
        }
    }
    pending_dx = pending_dy = 0;
}

static void try_step(int dx, int dy) {
    unsigned int nx = wrap172((int)player_tx + dx);
    unsigned int ny = wrap172((int)player_ty + dy);
    unsigned char tile = terrain_at(nx, ny) >> 4;

    /* On foot: sea (0) and mountains (3) block. Vehicles come later. */
    if (tile == 0 || tile == 3) return;

    player_tx = nx;
    player_ty = ny;
    cam_tx = wrap172((int)cam_tx + dx);
    cam_ty = wrap172((int)cam_ty + dy);
    if (dx > 0)      hslot = (hslot + 2) & 31;
    else if (dx < 0) hslot = (hslot + 30) & 31;
    if (dy > 0)      vslot = (vslot + 2) % MAP_ROWS_CELLS;
    else if (dy < 0) vslot = (vslot + MAP_ROWS_CELLS - 2) % MAP_ROWS_CELLS;

    pending_dx = (signed char)dx;
    pending_dy = (signed char)dy;

    food_tenths = (food_tenths > 5) ? food_tenths - 5 : 0;
}

void overworld_update(unsigned int keys, unsigned int pressed) {
    int dx = 0, dy = 0;

    /* E (button 2): enter the town (tile type 6) or castle (tile type 4) under the player. */
    if (pressed & PORT_A_KEY_2) {
        unsigned char tile = terrain_at(player_tx, player_ty) >> 4;
        if (tile == 6)      overworld_enter = 1;
        else if (tile == 4) overworld_enter = 2;
        else if (tile == 7) overworld_enter = 3;
    }

    if (keys & PORT_A_KEY_LEFT)       dx = -1;
    else if (keys & PORT_A_KEY_RIGHT) dx = 1;
    else if (keys & PORT_A_KEY_UP)    dy = -1;
    else if (keys & PORT_A_KEY_DOWN)  dy = 1;

    if (dx == 0 && dy == 0) {
        hold_timer = 0;
        return;
    }
    /* A new VBlank must consume the previous step before another one starts. */
    if (pending_dx || pending_dy) return;

    if (hold_timer == 0) {
        try_step(dx, dy);
        hold_timer = KEY_REPEAT_FRAMES;
    } else {
        hold_timer--;
    }
}

void overworld_add_sprites(void) {
    /* Player is always at the centre of the view: tile (8, 6) of the 16x12 view. */
    SMS_addSprite(128, 95, 4);   /* SAT Y is drawn one line low, so 95 puts the top at row 96 */
    SMS_addSprite(136, 95, 5);
    SMS_addSprite(128, 103, 6);
    SMS_addSprite(136, 103, 7);
}
