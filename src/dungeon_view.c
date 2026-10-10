/* ---------------------------------------------------------------------------
 * First-person dungeon view: a port of dungeonRenderer_update (wireframe).
 *
 * The engine draws into a 280x160 RGBA buffer, using the perspective table
 * PE% (dgn_pe) and the door/trap/ladder tables from BEVERY (dgn_cd, dgn_ld,
 * dgn_ft, dgn_la). Here the lines are drawn in the engine's 280-wide space
 * and then scaled to 256 px (x * 32 / 35), one bit per pixel.
 *
 * The 32x20 cells are then turned into SMS tiles. Identical cells share one
 * tile, found through a 256-entry hash whose candidates are compared against
 * the tile already in VRAM. Pixels use palette index 1 (white, from the
 * overworld palette), so each tile is plane 0 only.
 *
 * Differences from the engine:
 *   - Monsters are not drawn (the map holds no monster codes yet).
 *   - Engine messages (console text) are not shown.
 *   - The special tile-6 shape is drawn, but its message is skipped.
 *   - dgn_pe[distance + 1] is clamped to row 10 at the far end. The engine
 *     reads past the array there.
 *   - Lookups off the floor count as walls.
 * ------------------------------------------------------------------------- */
#include "SMSlib.h"
#include "dungeon.h"

extern const int dgn_pe[11][4];   /* left, right, top, bottom */
extern const int dgn_cd[11][4];   /* front door */
extern const int dgn_ld[11][6];   /* side doors */
extern const int dgn_ft[11][6];   /* traps */
extern const int dgn_la[11][4];   /* ladders */

#define VIEW_W      280           /* engine coordinate space */
#define VIEW_H      160
#define FB_ROW      32            /* bytes per row: 256 px */
#define CELL_COLS   32
#define CELL_ROWS   20
#define TILE_BASE   96            /* tiles 0-95 are the font */

/* Framebuffer in cell order: [tile row][tile column][pixel row]. Each 8x8
 * cell is 8 consecutive bytes, one bit per pixel, so reading a cell is cheap. */
/* Not static: the cell scan in scan_asm.s reads these directly. */
unsigned char fb[CELL_ROWS][CELL_COLS][8];
unsigned char zero_tile_g;                 /* tile for an empty cell, set per redraw */
void scan_fast(void);                      /* scan_asm.s */
#define UNIQ_MAX    160       /* unique cell patterns kept in RAM (a view uses ~90) */
unsigned char hash_slot[256];              /* 0 = empty, else pattern index + 1 */
unsigned char uniq[UNIQ_MAX][8];           /* 1bpp rows of each unique tile */
static unsigned char tile_count;


void dungeon_sample_keys(void);

static int iabs(int v) { return v < 0 ? -v : v; }

/* Engine x (0..279) -> screen x (0..255): x * 32 / 35, precomputed so the
 * line code avoids a 16-bit division per pixel. */
static const unsigned char xs_map[280] = {
    0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 11, 12, 13,
    14, 15, 16, 17, 18, 19, 20, 21, 21, 22, 23, 24, 25, 26, 27, 28,
    29, 30, 31, 32, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 42,
    43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 53, 54, 55, 56, 57,
    58, 59, 60, 61, 62, 63, 64, 64, 65, 66, 67, 68, 69, 70, 71, 72,
    73, 74, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 85, 86,
    87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 96, 97, 98, 99, 100, 101,
    102, 103, 104, 105, 106, 106, 107, 108, 109, 110, 111, 112, 113, 114, 115, 116,
    117, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128, 128, 129, 130,
    131, 132, 133, 134, 135, 136, 137, 138, 138, 139, 140, 141, 142, 143, 144, 145,
    146, 147, 148, 149, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160,
    160, 161, 162, 163, 164, 165, 166, 167, 168, 169, 170, 170, 171, 172, 173, 174,
    175, 176, 177, 178, 179, 180, 181, 181, 182, 183, 184, 185, 186, 187, 188, 189,
    190, 191, 192, 192, 193, 194, 195, 196, 197, 198, 199, 200, 201, 202, 202, 203,
    204, 205, 206, 207, 208, 209, 210, 211, 212, 213, 213, 214, 215, 216, 217, 218,
    219, 220, 221, 222, 223, 224, 224, 225, 226, 227, 228, 229, 230, 231, 232, 233,
    234, 234, 235, 236, 237, 238, 239, 240, 241, 242, 243, 244, 245, 245, 246, 247,
    248, 249, 250, 251, 252, 253, 254, 255,
};

static void px_set(int x, int y) {
    unsigned int xs;
    if (x < 0 || x >= VIEW_W || y < 0 || y >= VIEW_H) return;
    xs = xs_map[x];
    {
        unsigned char *c = &fb[0][0][0];
        unsigned int idx = ((((unsigned int)y >> 3) << 5) + (xs >> 3)) << 3;
        c[idx + ((unsigned int)y & 7u)] |= (unsigned char)(0x80u >> (xs & 7u));
    }
}

/* dungeonRenderer_drawLine: Bresenham in engine coordinates. */
static void line(int x0, int y0, int x1, int y1) {
    dungeon_sample_keys();          /* lines are short; sampling every call keeps gaps under a frame */
    int dx = iabs(x1 - x0), dy = iabs(y1 - y0);
    int sx = x0 < x1 ? 1 : -1, sy = y0 < y1 ? 1 : -1;
    int err = dx - dy, e2;
    for (;;) {
        px_set(x0, y0);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x0 += sx; }
        else if (e2 < dx) { err += dx; y0 += sy; }
    }
}

static int tile_at(int x, int y) {
    if (x < 0 || y < 0 || x >= DGN_W || y >= DGN_H) return 1;
    return dgn_map[x][y];
}
static int left_tile(int d)  { return tile_at((int)dgn_px + dgn_dx * d + dgn_dy, (int)dgn_py + dgn_dy * d - dgn_dx); }
static int right_tile(int d) { return tile_at((int)dgn_px + dgn_dx * d - dgn_dy, (int)dgn_py + dgn_dy * d + dgn_dx); }
static int front_tile(int d) { return tile_at((int)dgn_px + dgn_dx * d, (int)dgn_py + dgn_dy * d); }

static int is_wall_like(int t) { return t == 1 || t == 3 || t == 4; }

static void render_chest(int distance) {
    int t2 = distance + 1;
    int b = dgn_pe[distance][3];
    int a = 20 / t2, c = 10 / t2, e = 30 / t2;
    line(139 - a, b, 139 - a, b - a);
    line(139 - a, b - a, 139 + a, b - a);
    line(139 + a, b - a, 139 + a, b);
    line(139 + a, b, 139 - a, b);
    line(139 - a, b - a, 139 - c, b - e);
    line(139 - c, b - e, 139 + e, b - e);
    line(139 + e, b - e, 139 + e, b - c);
    line(139 + e, b - c, 139 + a, b);
    line(139 + a, b - a, 139 + e, b - e);
}

void dungeon_sample_keys(void);

static void render_view(void) {
    int distance;
    int shouldBreak = 0;

    for (distance = 0; distance < 11; distance++) {
        dungeon_sample_keys();          /* a press can land in any phase */
        int centerTile = front_tile(distance);
        int leftTile = left_tile(distance);
        int rightTile = right_tile(distance);
        int d2 = distance + 1 > 10 ? 10 : distance + 1;

        int l1 = dgn_pe[distance][0], r1 = dgn_pe[distance][1];
        int t1 = dgn_pe[distance][2], b1 = dgn_pe[distance][3];
        int l2 = dgn_pe[d2][0], r2 = dgn_pe[d2][1];
        int t2 = dgn_pe[d2][2], b2 = dgn_pe[d2][3];

        if (distance > 0) {
            if (centerTile == 1 || centerTile == 3 || centerTile == 4) {
                line(l1, t1, r1, t1);
                line(r1, b1, l1, b1);
                if (is_wall_like(left_tile(distance - 1)))  line(l1, t1, l1, b1);
                if (is_wall_like(right_tile(distance - 1))) line(r1, t1, r1, b1);
            }
            if (centerTile == 1 || centerTile == 3) shouldBreak = 1;
            if (centerTile == 4) {
                shouldBreak = 1;
                line(dgn_cd[distance][0], dgn_cd[distance][3], dgn_cd[distance][0], dgn_cd[distance][2]);
                line(dgn_cd[distance][0], dgn_cd[distance][2], dgn_cd[distance][1], dgn_cd[distance][2]);
                line(dgn_cd[distance][1], dgn_cd[distance][2], dgn_cd[distance][1], dgn_cd[distance][3]);
            }
        }

        if (!shouldBreak) {
            if (centerTile == 6) {
                int t3 = (b2 * 3 + b1) / 4;
                int t4 = (b2 + b1) / 2;
                int t5 = (l2 * 7 + r2 * 3) / 10;
                int t6 = (l2 * 15 + r2 * 85) / 100;      /* (l2*1.5 + r2*8.5)/10 */
                int l3 = (l2 * 8 + r2 * 2) / 10;
                int r3 = (l2 + r2 * 9) / 10;
                int t7 = (b2 + b1 * 3) / 4;
                line(l3, t3, t5, b2);
                line(t5, b2, r3, t3);
                line(r3, t3, r3, t7);
                line(r3, t7, t6, b1);
                line(t6, b1, t5, b1);
                line(t5, b1, l3, t7);
                line(l3, t7, l3, t3);
                line(l3, t3, t5, t4);
                line(t5, t4, t6, t4);
                line(t6, t4, t6, b1);
                line(t5, b1, t5, t4);
                line(t6, t4, r3, t3);
            }
            if (centerTile == 12) {
                int l = (l1 + l2) / 2, r = (r1 + r2) / 2;
                int t = (t1 + t2) / 2, b = (b1 + b2) / 2;
                int x, h;
                for (x = 1; x <= 4; x++) {
                    h = (t * x + (5 - x) * b) / 5;
                    line(l, h, r, h);
                }
            }

            if (is_wall_like(leftTile)) {
                line(l1, t1, l2, t2);
                line(l1, b1, l2, b2);
            } else {
                if (distance != 0) line(l1, t1, l1, b1);
                line(l1, t2, l2, t2);
                line(l2, b2, l1, b2);
                if (!is_wall_like(front_tile(distance + 1))) line(l2, t2, l2, b2);
            }
            if (leftTile == 4) {
                if (distance > 0) {
                    line(dgn_ld[distance][0], dgn_ld[distance][4], dgn_ld[distance][0], dgn_ld[distance][2]);
                    line(dgn_ld[distance][0], dgn_ld[distance][2], dgn_ld[distance][1], dgn_ld[distance][3]);
                    line(dgn_ld[distance][1], dgn_ld[distance][3], dgn_ld[distance][1], dgn_ld[distance][5]);
                } else {
                    line(0, dgn_ld[distance][2] - 3, dgn_ld[distance][1], dgn_ld[distance][3]);
                    line(dgn_ld[distance][1], dgn_ld[distance][3], dgn_ld[distance][1], dgn_ld[distance][5]);
                }
            }

            if (is_wall_like(rightTile)) {
                line(r1, t1, r2, t2);
                line(r1, b1, r2, b2);
            } else {
                if (distance != 0) line(r1, t1, r1, b1);
                line(r1, t2, r2, t2);
                line(r2, b2, r1, b2);
                if (!is_wall_like(front_tile(distance + 1))) line(r2, t2, r2, b2);
            }
            if (rightTile == 4) {
                if (distance > 0) {
                    line(279 - dgn_ld[distance][0], dgn_ld[distance][4], 279 - dgn_ld[distance][0], dgn_ld[distance][2]);
                    line(279 - dgn_ld[distance][0], dgn_ld[distance][2], 279 - dgn_ld[distance][1], dgn_ld[distance][3]);
                    line(279 - dgn_ld[distance][1], dgn_ld[distance][3], 279 - dgn_ld[distance][1], dgn_ld[distance][5]);
                } else {
                    line(279, dgn_ld[distance][2] - 3, 279 - dgn_ld[distance][1], dgn_ld[distance][3]);
                    line(279 - dgn_ld[distance][1], dgn_ld[distance][3], 279 - dgn_ld[distance][1], dgn_ld[distance][5]);
                }
            }

            if (centerTile == 7 || centerTile == 9) {
                line(dgn_ft[distance][0], dgn_ft[distance][4], dgn_ft[distance][2], dgn_ft[distance][5]);
                line(dgn_ft[distance][2], dgn_ft[distance][5], dgn_ft[distance][3], dgn_ft[distance][5]);
                line(dgn_ft[distance][3], dgn_ft[distance][5], dgn_ft[distance][1], dgn_ft[distance][4]);
                line(dgn_ft[distance][1], dgn_ft[distance][4], dgn_ft[distance][0], dgn_ft[distance][4]);
            }
            if (centerTile == 8) {
                line(dgn_ft[distance][0], 158 - dgn_ft[distance][4], dgn_ft[distance][2], 158 - dgn_ft[distance][5]);
                line(dgn_ft[distance][2], 158 - dgn_ft[distance][5], dgn_ft[distance][3], 158 - dgn_ft[distance][5]);
                line(dgn_ft[distance][3], 158 - dgn_ft[distance][5], dgn_ft[distance][1], 158 - dgn_ft[distance][4]);
                line(dgn_ft[distance][1], 158 - dgn_ft[distance][4], dgn_ft[distance][0], 158 - dgn_ft[distance][4]);
            }
            if (centerTile == 7 || centerTile == 8) {
                int ba = dgn_la[distance][3];
                int tp = dgn_la[distance][2];
                if (dgn_dy == 0) {
                    line(139, ba, 139, tp);
                    line(140, tp, 140, ba);
                } else {
                    int lx = dgn_la[distance][0];
                    int rx = dgn_la[distance][1];
                    line(lx, ba, lx, tp);
                    line(rx, tp, rx, ba);
                    line(lx, (ba * 4 + tp) / 5, rx, (ba * 4 + tp) / 5);
                    line(lx, (ba * 3 + tp * 2) / 5, rx, (ba * 3 + tp * 2) / 5);
                    line(lx, (ba * 2 + tp * 3) / 5, rx, (ba * 2 + tp * 3) / 5);
                    line(lx, (ba + tp * 4) / 5, rx, (ba + tp * 4) / 5);
                }
            }
            if (centerTile == 5) render_chest(distance);
        }

        if (shouldBreak) break;
    }
}

/* ---- conversion to tiles ------------------------------------------------ */

/* Build the 8 row bytes of the cell (cx, cy) from the framebuffer, and a hash. */
static unsigned char cell_is_empty;

static unsigned char cell_bits(unsigned int cx, unsigned int cy, unsigned char *bits) {
    unsigned char r, h = 0, any = 0;
    unsigned char *c = &fb[cy][cx][0];
    for (r = 0; r < 8; r++) {
        unsigned char b = c[r];
        bits[r] = b;
        any |= b;
        h = (unsigned char)(((h << 1) | (h >> 7)) ^ b);   /* rotate and xor */
    }
    cell_is_empty = (any == 0);
    return h;
}

/* Return the tile index for the cell bits, loading a new tile if needed.
 * Falls back to the blank tile (index 0) when all UNIQ_MAX slots are in use. */
static unsigned char tile_for(const unsigned char *bits, unsigned char h) {
    unsigned char probe = h;
    unsigned char i, k;
    unsigned char tile4[32];

    for (i = 0; i < 255; i++) {
        unsigned char s = hash_slot[probe];
        if (s == 0) {
            if (tile_count >= UNIQ_MAX) return 0;
            for (k = 0; k < 8; k++) uniq[tile_count][k] = bits[k];
            /* 4bpp: plane 0 carries the pixels, so index 1 is drawn in white */
            for (k = 0; k < 8; k++) {
                tile4[k * 4 + 0] = bits[k];
                tile4[k * 4 + 1] = 0;
                tile4[k * 4 + 2] = 0;
                tile4[k * 4 + 3] = 0;
            }
            SMS_loadTiles((void *)tile4, TILE_BASE + tile_count, 32);
            hash_slot[probe] = (unsigned char)(tile_count + 1);
            return tile_count++;
        }
        {
            unsigned char *u = uniq[s - 1];
            unsigned char same = 1;
            for (k = 0; k < 8; k++) if (u[k] != bits[k]) { same = 0; break; }
            if (same) return (unsigned char)(s - 1);
        }
        probe++;
    }
    return 0;
}

void dungeon_sample_keys(void);

/* Cache between redraws: the tile table and each cell's last tile. A cell is
 * rewritten only when its pixels differ from the tile it shows. Valid only
 * while nothing else has touched VRAM; dungeon_view_invalidate() resets it. */
unsigned char prev_tile[CELL_ROWS][CELL_COLS];   /* read by scan_asm.s */
static unsigned char cache_valid = 0;

void dungeon_view_invalidate(void) {
    unsigned int i;
    for (i = 0; i < 256; i++) hash_slot[i] = 0;
    tile_count = 0;
    cache_valid = 0;
}


volatile unsigned char dgn_dbg_stage = 0;   /* TEMP: redraw phase markers */

void dungeon_view_redraw(void) {
    unsigned int row, col;
    dgn_dbg_stage = 0;
    unsigned char bits[8];
    unsigned char zero_tile;

    /* Clear the framebuffer and the dedupe table. */
    {
        unsigned char *p = &fb[0][0][0];
        unsigned int n;
        for (n = 0; n < CELL_ROWS * CELL_COLS * 8u; n++) {
            p[n] = 0;
            if ((n & 127u) == 0) dungeon_sample_keys();
        }
    }
    dgn_dbg_stage = 1;
    /* Start over when the cache is cold, or when the tile table is nearly full. */
    if (!cache_valid || tile_count > UNIQ_MAX - 64) {
        dungeon_view_invalidate();
    }
    render_view();
    dgn_dbg_stage = 2;

    /* Tile 0 is the all-zero pattern. Rows below the view use it. */
    for (col = 0; col < 8; col++) bits[col] = 0;
    zero_tile = tile_for(bits, 0);

    /* Pass 1: pick each cell's tile. scan_asm.s does the hash and the table
     * lookup for every cell, and writes the tile index to prev_tile. A cell
     * whose pattern is not in the table yet gets 255; those are handled here,
     * because tile_for() loads a new tile into VRAM. Nothing is written to the
     * nametable until pass 2. */
    zero_tile_g = zero_tile;
    scan_fast();
    for (row = 0; row < CELL_ROWS; row++) {
        dungeon_sample_keys();
        for (col = 0; col < CELL_COLS; col++) {
            if (prev_tile[row][col] != 255) continue;
            {
                unsigned char h = cell_bits(col, row, bits);
                prev_tile[row][col] = cell_is_empty ? zero_tile : tile_for(bits, h);
            }
        }
    }

    dgn_dbg_stage = 3;
    /* Pass 2: write the nametable one row at a time. One address set per row,
     * then the words follow in order. The nametable is 32x28 even in 192-line
     * mode. Rows 20-27 are below the view and always use the blank tile, so
     * they are cleared too, or they keep the overworld's tiles. */
    for (row = 0; row < 28; row++) {
        dungeon_sample_keys();          /* keep presses made during the redraw */
        SMS_setNextTileatXY(0, row);
        for (col = 0; col < CELL_COLS; col++) {
            unsigned char t = (row < CELL_ROWS) ? prev_tile[row][col] : zero_tile;
            SMS_setTile(TILE_BASE + t);
        }
    }
    dgn_dbg_stage = 4;
    cache_valid = 1;
}
