/* ---------------------------------------------------------------------------
 * Dungeon floors. See dungeon.h.
 *
 * Tile codes in dgn_map[x][y] (from sceneDungeon.c and playerDungeon.c):
 *   0      floor
 *   1      wall (solid)
 *   2      trapdoor: stepping on it drops the player one floor
 *   3,4    features (solid to the player only when > 9; treated as floor here)
 *   5,6    features (6 is searched by the engine's C command)
 *   7,8    ladders: 8 climbs up one floor, 7 climbs down
 *   9      ladder that goes down, climbable without facing it
 *   10-12  features (solid, > 9)
 *   > 12   monsters (not ported yet)
 *
 * The view is drawn in dungeon_view.c (first person, wireframe).
 * ------------------------------------------------------------------------- */
#include "SMSlib.h"
#include "dungeon.h"

unsigned char dgn_map[DGN_W][DGN_H];
unsigned char dgn_depth = 1;
unsigned int  dgn_px = 1, dgn_py = 1;
signed char   dgn_dx = 0, dgn_dy = 1;
unsigned char dungeon_leave = 0;
unsigned int  dgn_latched = 0;   /* presses sampled during a redraw, see below */

static unsigned long rng_state;
static unsigned int  dgn_world_seed;     /* from world position, set on entry */

static void rng_seed(unsigned long s) { rng_state = s * 2654435761ul + 12345ul; }

static unsigned int rng_next(void) {
    rng_state = rng_state * 1103515245ul + 12345ul;
    return (unsigned int)((rng_state >> 16) & 0x7FFFu);
}

/* Returns 0 .. n-1. */
static unsigned int rnd(unsigned int n) { return rng_next() % n; }

/* Floor generation, following sceneDungeon_generateFloor. */
static void generate_floor(void) {
    unsigned int x, y, zz, xx;

    /* Seed from the world position and depth (the engine's first srand). */
    rng_seed((unsigned long)dgn_world_seed * 4096ul + (unsigned long)dgn_depth * 16ul + 1ul);

    for (x = 0; x < DGN_W; x++)
        for (y = 0; y < DGN_H; y++)
            dgn_map[x][y] = 0;

    /* Outer walls. */
    for (x = 0; x < DGN_W; x++) {
        dgn_map[x][0] = 1;
        dgn_map[x][DGN_H - 1] = 1;
        dgn_map[0][x] = 1;
        dgn_map[DGN_W - 1][x] = 1;
    }

    /* Interior wall lines at x,y = 2,4,6,8. */
    for (x = 2; x <= 8; x += 2) {
        for (y = 1; y <= 9; y++) {
            dgn_map[x][y] = 1;
            dgn_map[y][x] = 1;
        }
    }

    /* Random contents of the gaps. */
    for (zz = 0; zz < 2; zz++) {
        for (x = 2; x <= 8; x += 2) {
            for (y = 1; y <= 9; y += 2) {
                unsigned int zx = rnd(20) + 1;
                unsigned char xz = 0;
                if (zx < 3)                                   xz = 0;
                else if (zx < 9)                              xz = 3;
                else if (zx < 19)                             xz = 4;
                else if (zx < 20 && dgn_depth < 10)           xz = 9;
                else if (zx == 20 && dgn_depth < 10)          xz = 2;

                if (zz == 0) dgn_map[x][y] = xz;
                else         dgn_map[y][x] = xz;
            }
        }
    }

    /* Second phase: the engine reseeds from the game clock here. The port
     * uses a fixed value instead, so this phase is deterministic too. */
    rng_seed(0x5EED0000ul + dgn_depth);
    for (xx = 1; xx <= dgn_depth; xx++) {
        unsigned int px = (rnd(4) + 1) * 2;
        unsigned int py = (rnd(4) + 1) * 2;
        unsigned int zx;

        if (rnd(100) > 49) py += 1;
        else               px += 1;

        zx = rnd(3) + 5;                 /* 5, 6 or 7 */
        if (zx == 7) zx = 12;

        dgn_map[px][py] = (unsigned char)zx;
    }

    dgn_map[1][2] = 0;

    if (dgn_depth % 2 == 0) {
        dgn_map[7][3] = 7;
        dgn_map[3][7] = 8;
    } else {
        dgn_map[7][3] = 8;
        dgn_map[3][7] = 7;
    }

    if (dgn_depth == 10) dgn_map[7][3] = 0;
}

static void start_position(void) {
    dgn_px = 1;
    dgn_py = 1;
    dgn_dx = 0;
    dgn_dy = 1;
}

void dungeon_view_invalidate(void);

void dungeon_enter(unsigned char world, unsigned int tx, unsigned int ty) {
    dgn_world_seed = (unsigned int)(world * 1000u + tx * 16u + ty * 256u);
    dgn_depth = 1;
    dungeon_leave = 0;
    dungeon_view_invalidate();      /* other scenes have overwritten VRAM */
    start_position();
    generate_floor();
}

static int is_solid(unsigned int x, unsigned int y) {
    unsigned char t;
    if (x >= DGN_W || y >= DGN_H) return 1;
    t = dgn_map[x][y];
    return t == 1 || t > 9;
}

/* Turn left / right / around, as in playerDungeon_updateRotation. */
static void turn(int dx, int dy) {
    dgn_dx = (signed char)dx;
    dgn_dy = (signed char)dy;
}

/* One step forward (playerDungeon_updateMovement). Falls through trapdoors. */
static void step_forward(void) {
    int nx = (int)dgn_px + dgn_dx;
    int ny = (int)dgn_py + dgn_dy;
    unsigned char t;

    if (nx < 0 || ny < 0 || is_solid((unsigned int)nx, (unsigned int)ny)) return;

    dgn_px = (unsigned int)nx;
    dgn_py = (unsigned int)ny;

    t = dgn_map[dgn_px][dgn_py];
    if (t == 2) {
        /* Trapdoor: drop one floor. The engine also costs health and a
         * trapdoor "key"; both need the player record (not ported yet). */
        dgn_depth++;
        start_position();
        generate_floor();
    }
}

/* K (button 2): climb. Ladder 8 goes up, 7 goes down, 9 either way. Facing
 * matters except on 9 (playerDungeon_updateClimb). */
static void climb(void) {
    unsigned char t = dgn_map[dgn_px][dgn_py];
    if (t != 7 && t != 8 && t != 9) return;
    if (dgn_dy == 0 && t != 9) return;

    if (t == 8) {
        if (dgn_depth == 1) {
            dungeon_leave = 1;        /* back to the overworld */
            return;
        }
        dgn_depth--;
    } else {
        dgn_depth++;              /* 7 and 9 go down */
    }
    start_position();
    generate_floor();
}

int dungeon_update(unsigned int keys, unsigned int pressed) {
    (void)keys;
    pressed |= dgn_latched;
    dgn_latched = 0;

    if (pressed & PORT_A_KEY_LEFT) {
        turn(dgn_dy, -dgn_dx);
        return 1;
    }
    if (pressed & PORT_A_KEY_RIGHT) {
        turn(-dgn_dy, dgn_dx);
        return 1;
    }
    if (pressed & PORT_A_KEY_DOWN) {
        turn(-dgn_dx, -dgn_dy);
        return 1;
    }
    if (pressed & PORT_A_KEY_UP) {
        step_forward();
        return 1;
    }
    if (pressed & PORT_A_KEY_2) {
        climb();
        return 1;
    }
    return 0;
}

/* Redraw the first-person view. The caller has the display off.
 * Interrupts are masked while VRAM is read back (see dungeon_view.c). */
void dungeon_view_redraw(void);

/* Presses that happen while the view is being redrawn. The main loop does not
 * read the joypad during a redraw, so dungeon_view_redraw() samples it and
 * latches new presses here. dungeon_update() merges them with OR, so a press
 * seen both ways is applied once. */
static unsigned int sample_prev = 0;

/* Joystick port A (0xDC) read directly. Its bits are active low and match the
 * PORT_A_KEY_* layout (bit 0 up ... bit 5 button 2). SMS_getKeysStatus is not
 * used here: it would update SMSlib's own previous-state record, and the main
 * loop would then miss the press. */
__sfr __at (0xDC) DGN_JOY_A;

static unsigned int raw_keys(void) {
    return (unsigned int)((~DGN_JOY_A) & 0x3F);
}

void dungeon_sample_keys(void) {
    unsigned int k = raw_keys();
    dgn_latched |= (k & ~sample_prev);
    sample_prev = k;
}

void dungeon_reset_input(void) {
    dgn_latched = 0;
    sample_prev = raw_keys();
}

void dungeon_redraw_full(void) {
    /* The overworld and town leave their scroll set. The dungeon view must
     * start at zero, or the picture shifts and wraps. */
    SMS_setBGScrollX(0);
    SMS_setBGScrollY(0);
    dungeon_view_redraw();
}
