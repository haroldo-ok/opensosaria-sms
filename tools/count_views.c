/* Host-side count of distinct dungeon views.
 *
 * Mirrors generate_floor() in src/dungeon.c (32-bit RNG, 16-bit seed) and the
 * tile reads of render_view() in src/dungeon_view.c. A view is identified by
 * the sequence of (front, left, right) tiles it reads, stopping at the first
 * solid wall, plus the facing. Every passable cell of every floor, for every
 * 16-bit seed and depths 1..10, is counted. Reachability is not checked, so
 * the count is an upper bound.
 *
 * Build: gcc -O2 -o /tmp/count_views tools/count_views.c
 * Run:   /tmp/count_views
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define DGN_W 11
#define DGN_H 11
#define HBITS 22
#define HSIZE (1u << HBITS)

static unsigned char map[DGN_W][DGN_H];
static uint32_t rng_state;
static unsigned int depth;
static uint16_t world_seed;

static void rng_seed(uint32_t s) { rng_state = s * 2654435761u + 12345u; }
static unsigned int rng_next(void) {
    rng_state = rng_state * 1103515245u + 12345u;
    return (unsigned int)((rng_state >> 16) & 0x7FFFu);
}
static unsigned int rnd(unsigned int n) { return rng_next() % n; }

static void generate_floor(void) {
    unsigned int x, y, zz, xx;
    rng_seed((uint32_t)world_seed * 4096u + (uint32_t)depth * 16u + 1u);
    memset(map, 0, sizeof map);
    for (x = 0; x < DGN_W; x++) {
        map[x][0] = 1; map[x][DGN_H - 1] = 1;
        map[0][x] = 1; map[DGN_W - 1][x] = 1;
    }
    for (x = 2; x <= 8; x += 2)
        for (y = 1; y <= 9; y++) { map[x][y] = 1; map[y][x] = 1; }
    for (zz = 0; zz < 2; zz++)
        for (x = 2; x <= 8; x += 2)
            for (y = 1; y <= 9; y += 2) {
                unsigned int zx = rnd(20) + 1;
                unsigned char xz = 0;
                if (zx < 3) xz = 0;
                else if (zx < 9) xz = 3;
                else if (zx < 19) xz = 4;
                else if (zx < 20 && depth < 10) xz = 9;
                else if (zx == 20 && depth < 10) xz = 2;
                if (zz == 0) map[x][y] = xz; else map[y][x] = xz;
            }
    rng_seed(0x5EED0000u + depth);
    for (xx = 1; xx <= depth; xx++) {
        unsigned int px = (rnd(4) + 1) * 2;
        unsigned int py = (rnd(4) + 1) * 2;
        unsigned int zx;
        if (rnd(100) > 49) py += 1; else px += 1;
        zx = rnd(3) + 5;
        if (zx == 7) zx = 12;
        map[px][py] = (unsigned char)zx;
    }
    map[1][2] = 0;
    if (depth % 2 == 0) { map[7][3] = 7; map[3][7] = 8; }
    else                { map[7][3] = 8; map[3][7] = 7; }
    if (depth == 10) map[7][3] = 0;
}

static int tile(int x, int y) {
    if (x < 0 || y < 0 || x >= DGN_W || y >= DGN_H) return 1;
    return map[x][y];
}

/* Render classes, from render_view(). Centre classes:
 *   1 wall (1 and 3 draw the same), 2 ladder-solid (4), 3 empty (0,2,10,11),
 *   4 ladder down (7,9), 5 ladder up (8), 6 chest (5), 7 (6), 8 (12).
 * Side classes only test is_wall_like() and ==4: 0 wall, 1 (4), 2 other. */
static int centre_class(int t) {
    switch (t) {
    case 1: case 3: return 1;
    case 4: return 2;
    case 0: case 2: case 10: case 11: return 3;
    case 7: case 9: return 4;
    case 8: return 5;
    case 5: return 6;
    case 6: return 7;
    case 12: return 8;
    default: return 3;
    }
}
static int side_class(int t) {
    if (t == 1 || t == 3) return 0;
    if (t == 4) return 1;
    return 2;
}

/* Build the view key for a player at (px,py) facing (dx,dy). */
static int build_key(int px, int py, int dx, int dy, unsigned char *key) {
    int n = 0, d;
    key[n++] = (unsigned char)((dx + 1) * 3 + (dy + 1));
    for (d = 0; d <= 10; d++) {
        int f = tile(px + dx * d, py + dy * d);
        int l = tile(px + dx * d + dy, py + dy * d - dx);
        int r = tile(px + dx * d - dy, py + dy * d + dx);
        key[n++] = (unsigned char)centre_class(f);
        key[n++] = (unsigned char)side_class(l);
        key[n++] = (unsigned char)side_class(r);
        if (d > 0 && (f == 1 || f == 3 || f == 4)) break;
        if (d == 10) key[n++] = (unsigned char)side_class(tile(px + dx * 11, py + dy * 11));
    }
    return n;
}

static uint64_t fnv(const unsigned char *k, int n) {
    uint64_t h = 1469598103934665603ull;
    int i;
    for (i = 0; i < n; i++) { h ^= k[i]; h *= 1099511628211ull; }
    return h | 1ull;   /* 0 means empty slot */
}

static uint64_t *table;
static unsigned long distinct = 0, inserted = 0;

static void insert(uint64_t h) {
    uint32_t i = (uint32_t)(h & (HSIZE - 1));
    inserted++;
    while (table[i]) {
        if (table[i] == h) return;
        i = (i + 1) & (HSIZE - 1);
    }
    table[i] = h;
    distinct++;
}

/* Cells reachable from the entry cell (1,1) over passable tiles. */
static unsigned char reach[DGN_W][DGN_H];
static void flood(void) {
    int qx[DGN_W * DGN_H], qy[DGN_W * DGN_H], h = 0, t = 0, dir;
    static const int NX[4] = {1, -1, 0, 0};
    static const int NY[4] = {0, 0, 1, -1};
    memset(reach, 0, sizeof reach);
    if (map[1][1] == 1 || map[1][1] > 9) return;
    reach[1][1] = 1; qx[t] = 1; qy[t] = 1; t++;
    while (h < t) {
        int x = qx[h], y = qy[h]; h++;
        for (dir = 0; dir < 4; dir++) {
            int nx = x + NX[dir], ny = y + NY[dir];
            if (nx < 0 || ny < 0 || nx >= DGN_W || ny >= DGN_H) continue;
            if (reach[nx][ny] || map[nx][ny] == 1 || map[nx][ny] > 9) continue;
            reach[nx][ny] = 1; qx[t] = nx; qy[t] = ny; t++;
        }
    }
}

int main(void) {
    unsigned int seed;
    static const int DX[4] = {0, 1, 0, -1};
    static const int DY[4] = {1, 0, -1, 0};
    unsigned char key[64];
    unsigned long passable_cells = 0;

    table = calloc(HSIZE, sizeof(uint64_t));
    if (!table) return 1;

    for (seed = 0; seed < 65536; seed++) {
        world_seed = (uint16_t)seed;
        for (depth = 1; depth <= 10; depth++) {
            int px, py, dir;
            generate_floor();
            flood();
            for (px = 0; px < DGN_W; px++)
                for (py = 0; py < DGN_H; py++) {
                    int t = map[px][py];
                    /* Passable: floor, trapdoor, features 3..6, ladders 7..9. */
                    if (!reach[px][py]) continue;
                    passable_cells++;
                    for (dir = 0; dir < 4; dir++) {
                        int n = build_key(px, py, DX[dir], DY[dir], key);
                        insert(fnv(key, n));
                    }
                }
        }
    }

    printf("seeds: 65536  depths: 1..10\n");
    printf("reachable cell-visits: %lu\n", passable_cells);
    printf("view keys (inserted): %lu\n", inserted);
    printf("distinct views: %lu\n", distinct);
    return 0;
}
