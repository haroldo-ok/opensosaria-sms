/* ---------------------------------------------------------------------------
 * Game text from the user's disks (ROM banks 6 and 7, tools/build_text.py).
 *
 * Ids 0..n-1 are open-sosaria's ultimaStrings[] numbers, so the code can use
 * the engine's indices directly (for example TXT(98) is "COMMAND: ").
 * The name tables from BEVERY (weapons, spells, ...) and the monster names
 * follow, reached through text_table() and text_monster().
 *
 * Every call copies into a caller buffer of at least TEXT_MAX + 1 bytes and
 * restores the slot-2 bank before it returns, so no banked pointer escapes.
 * ------------------------------------------------------------------------- */
#ifndef TEXT_H
#define TEXT_H

#define TEXT_MAX 40

enum {
    TBL_STATS = 0, TBL_RACES, TBL_TYPES, TBL_ARMORS,
    TBL_VEHICLES, TBL_WEAPONS, TBL_SPELLS, TBL_PLACES
};

/* Copies at most `max` characters of string `id` into dst, then a NUL;
 * returns the number copied. dst needs max + 1 bytes. */
unsigned char text_get(unsigned int id, char *dst, unsigned char max);
/* Id of entry i of a name table (TBL_*). */
unsigned int  text_table(unsigned char table, unsigned char i);
/* Id of a monster's name. */
unsigned int  text_monster(unsigned char monster);
/* Monster stats from the enemy table (overworld group size, combat rank). */
unsigned char monster_group(unsigned char monster);
unsigned char monster_rank(unsigned char monster);

#endif
