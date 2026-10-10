/* Console and stats panel. See hud.h. */
#include "SMSlib.h"
#include "hud.h"
#include "text.h"
#include "player.h"

#define REVEAL_FRAMES 9           /* 0.15 s at 60 Hz, uiConsole's timeToNextMessage */
#define MAP_ROWS      28

/* Disk text ids of the stat labels (uiConsole_init). */
static const unsigned char stat_label_id[4] = { 90, 92, 94, 96 };

static char lines[HUD_ROWS][HUD_COLS];
static unsigned char wait_frames; /* input waits this long for queued lines */
static unsigned char dirty;       /* bit r = console row r changed */

char hud_sb[HUD_SB_SIZE];
static unsigned char sb_len;

/* ---- message builder ---------------------------------------------------- */
void sb_clear(void) { sb_len = 0; hud_sb[0] = 0; }

void sb_str(const char *s) {
    while (*s && sb_len < sizeof(hud_sb) - 1) hud_sb[sb_len++] = *s++;
    hud_sb[sb_len] = 0;
}

void sb_text(unsigned int id, unsigned char maxlen) {
    unsigned char room = (unsigned char)(sizeof(hud_sb) - 1 - sb_len);
    if (maxlen > room) maxlen = room;
    sb_len += text_get(id, hud_sb + sb_len, maxlen);
}

/* Decimal without a divide: the Z80 has none, and SDCC's is ~900 cycles. */
static const unsigned int pow10[4] = { 10000, 1000, 100, 10 };
void sb_num(unsigned int n) {
    unsigned char i, d, started = 0;
    char c[2];
    c[1] = 0;
    for (i = 0; i < 4; i++) {
        d = 0;
        while (n >= pow10[i]) { n -= pow10[i]; d++; }
        if (d || started) { c[0] = (char)('0' + d); sb_str(c); started = 1; }
    }
    c[0] = (char)('0' + n);
    sb_str(c);
}

/* ---- console lines ------------------------------------------------------
 * lines[] holds font tile indices (character - 32), so drawing is a plain copy. */
static unsigned char to_tile(char c) {
    unsigned char u = (unsigned char)c;
    return (u >= 32 && u < 128) ? u - 32 : 0;
}

static void copy_line(char *dst, const char *src, unsigned char n) {
    unsigned char i;
    for (i = 0; i < HUD_COLS; i++) dst[i] = (char)((i < n) ? to_tile(src[i]) : 0);
}

static void push_visible(const char *line) {
    unsigned char r, i;
    for (r = 0; r + 1 < HUD_ROWS; r++)
        for (i = 0; i < HUD_COLS; i++) lines[r][i] = lines[r + 1][i];
    for (i = 0; i < HUD_COLS; i++) lines[HUD_ROWS - 1][i] = line[i];
    dirty |= 0x0F;
}

/* Drops ^x codes (and the digit after ^T), then splits into console lines.
 * mode 0 = add, 1 = queue (adds input wait), 2 = replace the last line. */
static void emit(const char *s, unsigned char mode) {
    char clean[HUD_SB_SIZE];
    char line[HUD_COLS];
    unsigned char n = 0, pos = 0, len, cut, k;

    while (*s && n < sizeof(clean) - 1) {
        if (*s == '^') {
            s++;
            if (!*s) break;
            if (*s == 'T' && s[1]) s++;
            s++;
            continue;
        }
        clean[n++] = *s++;
    }

    do {
        len = n - pos;
        if (len > HUD_COLS) {
            cut = HUD_COLS;
            for (k = HUD_COLS; k > 0; k--) {
                if (clean[pos + k] == ' ') { cut = k; break; }
            }
            len = cut;
        }
        copy_line(line, clean + pos, len);
        pos += len;
        while (pos < n && clean[pos] == ' ') pos++;

        if (mode == 2) {
            for (k = 0; k < HUD_COLS; k++) lines[HUD_ROWS - 1][k] = line[k];
            dirty |= 1u << (HUD_ROWS - 1);
            return;                             /* only the first line fits */
        }
        push_visible(line);
        if (mode == 1 && wait_frames < 255 - REVEAL_FRAMES) wait_frames += REVEAL_FRAMES;
    } while (pos < n);
}

#define LABEL_LEN   5            /* "H.P.=" and the others */
#define VALUE_LEN   (32 - HUD_STATS_X - LABEL_LEN)
#define VALUE_X     (HUD_STATS_X + LABEL_LEN)
#define DIRTY_STATS 0x10

static unsigned char labels[HUD_ROWS][LABEL_LEN];   /* tile indices, from the disk text */
static unsigned char values[HUD_ROWS][VALUE_LEN];   /* tile indices, refreshed in hud_tick */

void hud_reset(void) {
    unsigned char r, i, n;
    for (r = 0; r < HUD_ROWS; r++) {
        for (i = 0; i < HUD_COLS; i++) lines[r][i] = 0;
        sb_clear();
        sb_text(stat_label_id[r], LABEL_LEN);
        n = sb_len;
        for (i = 0; i < LABEL_LEN; i++) labels[r][i] = (i < n) ? to_tile(hud_sb[i]) : 0;
    }
    wait_frames = 0;
    dirty = 0x1F;
    player_stats_dirty = 1;
    hud_tick();                  /* fill values[] now */
}

void hud_add(const char *s)          { emit(s, 0); }
void hud_queue(const char *s)        { emit(s, 1); }
void hud_replace_last(const char *s) { emit(s, 2); }
unsigned char hud_busy(void)         { return wait_frames; }

static void value_tiles(unsigned char r, unsigned char *out);

/* Logic phase, once per frame: count the input wait down, and turn changed
 * stats into tiles here rather than in the VRAM phase. */
void hud_tick(void) {
    unsigned char r;
    if (wait_frames) wait_frames--;
    if (player_stats_dirty) {
        player_stats_dirty = 0;
        for (r = 0; r < HUD_ROWS; r++) value_tiles(r, values[r]);
        dirty |= DIRTY_STATS;
    }
}

/* ---- drawing ------------------------------------------------------------ *
 * dirty bits 0-3 = console rows, bit 4 = the stat values. A full draw writes
 * every cell of the four rows (the rows may hold map tiles before it).
 * SMS_setTile is the paced write, safe with the display on; a full draw is
 * about 128 cells, so it fits well inside the frame before the HUD split. */
static void put_tiles(const unsigned char *t, unsigned char n) {
    hud_put_src = t;
    hud_put_n = n;
    hud_put_hi = 0;
    hud_put_tiles();
}

/* Stat value as VALUE_LEN tile indices, left-aligned, blank-padded. */
static void value_tiles(unsigned char r, unsigned char *out) {
    unsigned int v = (r == 0) ? player.hits : (r == 1) ? player.food
                   : (r == 2) ? player.experience : player.gold;
    unsigned char i;
    sb_clear();
    sb_num(v);
    for (i = 0; i < VALUE_LEN; i++) out[i] = (i < sb_len) ? to_tile(hud_sb[i]) : 0;
}

void hud_draw(unsigned char row0, unsigned char full) {
    unsigned char r, y, mask;
    mask = full ? 0x1F : dirty;
    if (!mask) return;
    for (r = 0, y = row0; r < HUD_ROWS; r++, y++) {
        const unsigned char *val = values[r];
        if (y >= MAP_ROWS) y -= MAP_ROWS;
        if (full) {
            SMS_setNextTileatXY(0, y);
            put_tiles((const unsigned char *)lines[r], HUD_COLS);
            SMS_setTile(0);                                    /* separator column */
            put_tiles(labels[r], LABEL_LEN);
            put_tiles(val, VALUE_LEN);
            continue;
        }
        if (mask & (1u << r)) {
            SMS_setNextTileatXY(0, y);
            put_tiles((const unsigned char *)lines[r], HUD_COLS);
        }
        if (mask & DIRTY_STATS) {
            SMS_setNextTileatXY(VALUE_X, y);
            put_tiles(val, VALUE_LEN);
        }
    }
    dirty = 0;
}
