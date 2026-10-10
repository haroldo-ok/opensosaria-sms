/* ---------------------------------------------------------------------------
 * Dungeon floors (sceneDungeon.c / playerDungeon.c in open-sosaria).
 *
 * This is the logic and a top-down view of the floor. The first-person
 * wireframe view (dungeonRenderer.c), monsters, chests, and combat are not
 * ported yet. See PORTING_PLAN.md.
 *
 * Floor layout follows sceneDungeon_generateFloor. The engine seeds its
 * random generator from the world position and the game clock using the host
 * C library's rand(), so the exact layout is not reproducible across
 * platforms. This port uses its own deterministic generator instead.
 * ------------------------------------------------------------------------- */
#ifndef DUNGEON_H
#define DUNGEON_H

#define DGN_W 11
#define DGN_H 11

extern unsigned char dgn_map[DGN_W][DGN_H];  /* tile codes, see dungeon.c */
extern unsigned char dgn_depth;              /* 1..10 */
extern unsigned int  dgn_px, dgn_py;         /* player cell */
extern signed char   dgn_dx, dgn_dy;         /* facing */
extern unsigned char dungeon_leave;          /* set when the player climbs out */

void dungeon_enter(unsigned char world, unsigned int tx, unsigned int ty);
int  dungeon_update(unsigned int keys, unsigned int pressed);  /* returns 1 if state changed */
void dungeon_redraw_full(void);                                /* display must be off */
void dungeon_sample_keys(void);     /* latch presses; call often in dungeon mode */
void dungeon_reset_input(void);     /* clear the latch and the sampler state */

#endif
