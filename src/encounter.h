/* ---------------------------------------------------------------------------
 * Overworld monsters and combat (sceneOverworld.c and the attack, dodge and
 * kill parts of playerOverworld.c in open-sosaria).
 *
 * enc_monster follows the engine's enemyEncounter.monsterId: -1 = none,
 * 0 = dodged, 6..20 = an overworld monster. Monster names, group sizes and
 * ranks come from the disk's enemy table (text.h).
 *
 * The engine draws from rand01(), a float in [0, 1). Here rnd() returns a
 * 16-bit fraction r, meaning r / 65536, and the formulas use fixed point.
 * ------------------------------------------------------------------------- */
#ifndef ENCOUNTER_H
#define ENCOUNTER_H

extern signed char   enc_monster;     /* enemyEncounter.monsterId */
extern unsigned int  enc_hp;          /* hit points of the current group member */
extern unsigned char enc_number;      /* monsters left in the group */
extern unsigned char enc_shown;       /* renderEnemy: draw the monster sprite */
extern signed char   enc_dx, enc_dy;  /* sprite offset from the player, in tiles */
extern unsigned char enc_sprite;      /* 0 sea, 1 land, 2 woods */
extern unsigned char enc_enabled;     /* 0 = never spawn (playtests poke this) */
extern unsigned int  rng_state;       /* xorshift state, never 0 */

unsigned int rnd(void);               /* 0..65535, uniform */

void enc_reset(void);
/* After an action: the monsters attack, or a new group may appear. */
void enc_resolve(unsigned char tile);
/* Before a step: 1 = the path is free (and any monster is gone), 0 = blocked. */
unsigned char enc_try_dodge(signed char mx, signed char my, unsigned char tile);
/* The A(ttack) command. */
void enc_attack(void);

#endif
