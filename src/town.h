/* ---------------------------------------------------------------------------
 * Town and castle screens (Ultima I TWN.PIC / CAS.PIC + BTWN / BCAS).
 *
 * Data is in ROM bank 4 (town) or 5 (castle); see PORTING_PLAN.md and
 * tools/build_town.py for the bank layout. The picture is 35 x 24 8x8 cells,
 * wider than the 32-column screen, so the view scrolls horizontally over
 * the 24-px overhang and streams one picture column at a time, like the
 * overworld does for rows and columns.
 * ------------------------------------------------------------------------- */
#ifndef TOWN_H
#define TOWN_H

extern unsigned char town_which;   /* 0 = town (bank 4), 1 = castle (bank 5) */
extern unsigned int  town_px;      /* player cell, 0..39 */
extern unsigned int  town_py;      /* player cell, 0..21 */
extern unsigned char town_leave;   /* set when the player walks out; main.c clears it */

void town_redraw_full(void);                              /* display must be off */
void town_update(unsigned int keys, unsigned int pressed); /* RAM only */
void town_vblank(void);                                   /* scroll + streamed columns */
void town_add_sprites(void);                              /* SAT building, RAM only */

#endif
