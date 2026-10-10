/* ---------------------------------------------------------------------------
 * Town and castle implementation. See town.h.
 *
 * Coordinates:
 *   town_px, town_py        player cell (40 x 22 collision grid)
 *   town_left               leftmost picture column in view (0..3)
 *   scroll_px()             horizontal pixel offset, 0..24
 * The screen shows picture columns town_left .. town_left+31 in tilemap
 * columns (c & 31). Rows 0..23 are the picture; rows 24..27 are unused.
 * ------------------------------------------------------------------------- */
#include "SMSlib.h"
#include "town.h"
#include "overworld.h"

extern const unsigned char bank4_data[];   /* from banked/bank4.bin (town) */
extern const unsigned char bank5_data[];   /* from banked/bank5.bin (castle) */

#define TOWN_CELLS_W     35               /* picture width in 8x8 cells (280 px) */
#define TOWN_CELLS_H     24               /* picture height in 8x8 cells (192 px) */
#define TOWN_VIEW_COLS   32               /* tilemap columns on screen */
#define TOWN_MAP_W       40               /* collision grid */
#define TOWN_MAP_H       22
#define TOWN_TILE_BASE   96               /* VRAM tile slot for screen tiles (96..383) */
#define TOWN_SCROLL_MAX  24               /* 280 - 256 */
#define TOWN_COLL_BYTES  (TOWN_MAP_W * TOWN_MAP_H)
#define TOWN_KEY_REPEAT  8

unsigned char town_which = 0;
unsigned int  town_px = 20;               /* start cell from sceneTown_init */
unsigned int  town_py = 20;
unsigned char town_leave = 0;

static unsigned int town_tiles;           /* N, unique tiles, read from the bank header */
static unsigned int town_left;
static unsigned char hold_timer;

static unsigned char town_bank(void) { return town_which ? 5 : 4; }

static const unsigned char *town_src(void) {
    return town_which ? bank5_data : bank4_data;
}

/* Bank layout: [0..1] N | [2 .. 2+32N) tiles | tilemap 35*24*2 | collision 40*22 */
static unsigned int tilemap_offset(void) { return 2u + town_tiles * 32u; }
static unsigned int collision_offset(void) { return tilemap_offset() + TOWN_CELLS_W * TOWN_CELLS_H * 2u; }

/* Collision lookup. Pages the bank in, reads one byte, restores the bank. */
static unsigned char collision_at(unsigned int x, unsigned int y) {
    unsigned char saved = SMS_getROMBank();
    unsigned char v;
    SMS_mapROMBank(town_bank());
    v = town_src()[collision_offset() + x * TOWN_MAP_H + y];
    SMS_mapROMBank(saved);
    return v;
}

/* Reads town_tiles from the bank header on entry. */
static void read_header(void) {
    unsigned char saved = SMS_getROMBank();
    const unsigned char *src;
    SMS_mapROMBank(town_bank());
    src = town_src();
    town_tiles = src[0] | ((unsigned int)src[1] << 8);
    SMS_mapROMBank(saved);
}

/* Writes one picture column into tilemap column (c & 31). VBlank or display-off only. */
static void write_column(unsigned int c) {
    unsigned char saved = SMS_getROMBank();
    unsigned char cell = (unsigned char)(c & 31u);
    unsigned int r;
    SMS_mapROMBank(town_bank());
    {
        const unsigned char *map = town_src() + tilemap_offset();
        for (r = 0; r < TOWN_CELLS_H; r++) {
            unsigned int off = (r * TOWN_CELLS_W + c) * 2u;
            unsigned int w = map[off] | ((unsigned int)map[off + 1] << 8);
            SMS_setTileatXY(cell, (unsigned char)r, (unsigned int)(TOWN_TILE_BASE + w));
        }
    }
    SMS_mapROMBank(saved);
}

/* Scroll in whole 8-px steps. A fine offset would need a 33rd column on the
 * right edge, which the 32-cell tilemap ring cannot hold. */
static unsigned int scroll_px(void) {
    int s = (int)town_px * 7 + 3 - 128;   /* keep the player's cell centre near the middle */
    if (s < 0) s = 0;
    if (s > TOWN_SCROLL_MAX) s = TOWN_SCROLL_MAX;
    return (unsigned int)(s & ~7);
}

static void apply_scroll(void) {
    /* Horizontal register is negated (same as the overworld). */
    SMS_setBGScrollX((unsigned char)((256u - scroll_px()) & 255u));
    SMS_setBGScrollY(0);
}

/* Moves the window to match scroll_px(), one column per step, during VBlank. */
static void stream_columns(void) {
    unsigned int want = scroll_px() >> 3;
    while (town_left < want) {
        town_left++;
        write_column(town_left + TOWN_VIEW_COLS - 1);
    }
    while (town_left > want) {
        write_column(town_left - 1);
        town_left--;
    }
}

void town_redraw_full(void) {
    unsigned int c;
    unsigned char saved = SMS_getROMBank();

    read_header();
    SMS_mapROMBank(town_bank());
    SMS_loadTiles(town_src() + 2, TOWN_TILE_BASE, town_tiles * 32u);
    SMS_mapROMBank(saved);

    town_left = scroll_px() >> 3;
    for (c = town_left; c < town_left + TOWN_VIEW_COLS; c++) {
        write_column(c);
    }
    apply_scroll();
    hold_timer = 0;
}

void town_vblank(void) {
    apply_scroll();
    stream_columns();
}

static void try_step(int dx, int dy) {
    int nx = (int)town_px + dx;
    int ny = (int)town_py + dy;
    /* Exits (sceneTown / playerTown_checkExit): town leaves through the bottom
     * edge, castle through the left edge. */
    if (town_which == 0 && ny >= TOWN_MAP_H) { town_leave = 1; return; }
    if (town_which == 1 && nx < 0)           { town_leave = 1; return; }
    if (nx < 0 || nx >= TOWN_MAP_W || ny < 0 || ny >= TOWN_MAP_H) return;
    if (collision_at((unsigned int)nx, (unsigned int)ny)) return;   /* walls and solids */
    town_px = (unsigned int)nx;
    town_py = (unsigned int)ny;
}

void town_update(unsigned int keys, unsigned int pressed) {
    int dx = 0, dy = 0;
    (void)pressed;

    if (keys & PORT_A_KEY_LEFT)       dx = -1;
    else if (keys & PORT_A_KEY_RIGHT) dx = 1;
    else if (keys & PORT_A_KEY_UP)    dy = -1;
    else if (keys & PORT_A_KEY_DOWN)  dy = 1;

    if (dx == 0 && dy == 0) {
        hold_timer = 0;
        return;
    }
    if (hold_timer == 0) {
        try_step(dx, dy);
        hold_timer = TOWN_KEY_REPEAT;
    } else {
        hold_timer--;
    }
}

void town_add_sprites(void) {
    /* Player: a 16x16 metasprite centred on the cell. SAT Y is drawn one line low. */
    int left = (int)town_px * 7 + 3 - 8 - (int)scroll_px();
    int top  = (int)town_py * 7 + 3 - 8;
    SMS_addSpriteClipping(left,     top - 1, OW_PLAYER_TILE);
    SMS_addSpriteClipping(left + 8, top - 1, OW_PLAYER_TILE + 1);
    SMS_addSpriteClipping(left,     top + 7, OW_PLAYER_TILE + 2);
    SMS_addSpriteClipping(left + 8, top + 7, OW_PLAYER_TILE + 3);
}
