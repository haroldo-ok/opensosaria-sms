/* ---------------------------------------------------------------------------
 * Console and stats panel (entities/ui/uiConsole.c in open-sosaria).
 *
 * The bottom four 8-px rows of the screen, as in the original:
 *   columns 0-20   console, the last four lines of messages
 *   columns 22-31  stats: H.P., FOOD, EXP., GOLD (labels from the disk text)
 *
 * The original is 40 columns wide; the SMS has 32, so a message longer than
 * 21 characters wraps onto the next line at a space.
 *
 * Pacing: the engine shows queued lines one every 0.15 s and blocks input
 * until they are all out. RAM is too tight here to hold queued text (see
 * PORTING_PLAN.md, RAM budget), so a queued line is shown at once and adds
 * 9 frames (0.15 s) of input wait instead (hud_busy()).
 * Colour, flash, and typewriter codes (^1, ^F, ^T1) are dropped.
 *
 * Text is plain RAM; hud_draw() writes it to the name table, so it must run
 * where VRAM writes are allowed (the paced SMSlib writes are used).
 * ------------------------------------------------------------------------- */
#ifndef HUD_H
#define HUD_H

#define HUD_ROWS     4
#define HUD_COLS     21
#define HUD_STATS_X  22

void hud_reset(void);                    /* clear the console and the wait */
void hud_add(const char *s);             /* show now (uiConsole_addMessage) */
void hud_queue(const char *s);           /* show now, plus 0.15 s input wait (uiConsole_queueMessage) */
void hud_replace_last(const char *s);    /* rewrite the last visible line */
unsigned char hud_busy(void);            /* input must wait for queued lines */
void hud_tick(void);                     /* once per frame, logic phase: wait countdown, stat tiles */
void hud_draw(unsigned char row0, unsigned char full);  /* VRAM: rows row0..row0+3 mod 28 */

/* Paced name-table writer (hud_asm.s): n cells at the current VDP address,
 * low bytes from src, high byte hi for all. */
extern const unsigned char *hud_put_src;
extern unsigned char hud_put_n;
extern unsigned char hud_put_hi;
void hud_put_tiles(void);

/* Message builder: compose a line from disk text and numbers, then pass
 * hud_sb to hud_add / hud_queue / hud_replace_last. */
#define HUD_SB_SIZE 41
extern char hud_sb[HUD_SB_SIZE];
void sb_clear(void);
void sb_text(unsigned int id, unsigned char maxlen);   /* like "%.<maxlen>s" of ultimaStrings[id] */
void sb_str(const char *s);
void sb_num(unsigned int n);

#endif
