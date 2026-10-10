/* ---------------------------------------------------------------------------
 * Player record (data/player.c in open-sosaria), reduced to the fields the
 * port uses so far. The engine keeps food and time as floats; here each is a
 * whole part plus hundredths, so 0.05 food or 0.6 time units stay exact and
 * the HUD never needs a 32-bit divide.
 *
 * There is no character generator yet, so a new game starts from fixed
 * stats (player_new_game). The values follow sceneCharacterGenerator's
 * finishing step for gold, food, hits, weapon and armour.
 * ------------------------------------------------------------------------- */
#ifndef PLAYER_H
#define PLAYER_H

#define PLAYER_WEAPONS 15

typedef struct {
    unsigned int  hits;           /* player.health */
    unsigned char strength;
    unsigned char agility;
    unsigned char stamina;
    unsigned char charisma;
    unsigned char wisdom;
    unsigned char intelligence;
    unsigned int  gold;
    unsigned int  experience;
    unsigned int  food;           /* whole food units (what the HUD shows) */
    unsigned char food_frac;      /* hundredths, 0..99 */
    unsigned int  time;           /* whole time units */
    unsigned char time_frac;      /* hundredths, 0..99 */
    unsigned char weapon;         /* readied weapon, index into the weapon names */
    unsigned char armor;          /* worn armour */
    unsigned char vehicle;        /* 0 = on foot; vehicles are not ported yet */
    unsigned char weapons[PLAYER_WEAPONS];
} Player;

extern Player player;
extern unsigned char player_stats_dirty;   /* set when a HUD value changed */

void player_new_game(void);
unsigned char player_alive(void);            /* hits > 0 and food > 0 */
void player_sub_food(unsigned char hundredths);
void player_add_time(unsigned char hundredths);
void player_hurt(unsigned int damage);

/* Food and time costs, from data/player.c */
void player_consume_food(void);              /* one overworld step */
void player_wait_penalty(void);              /* "PASS" after 5 s idle */

#endif
