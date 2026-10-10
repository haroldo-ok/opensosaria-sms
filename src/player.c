/* Player record and the food/time rules of data/player.c. */
#include "player.h"

Player player;
unsigned char player_stats_dirty = 1;

void player_new_game(void) {
    unsigned char i;
    player.hits = 100;
    player.strength = 20;
    player.agility = 20;
    player.stamina = 15;
    player.charisma = 15;
    player.wisdom = 15;
    player.intelligence = 15;
    player.gold = 100;
    player.experience = 1;
    player.food = 100;
    player.food_frac = 0;
    player.time = 0;
    player.time_frac = 0;
    player.weapon = 1;              /* dagger */
    player.armor = 1;
    player.vehicle = 0;
    for (i = 0; i < PLAYER_WEAPONS; i++) player.weapons[i] = 0;
    player.weapons[0] = 2;
    player_stats_dirty = 1;
}

unsigned char player_alive(void) {
    return player.hits > 0 && (player.food || player.food_frac);
}

void player_sub_food(unsigned char hundredths) {
    unsigned int whole = player.food;
    while (hundredths >= 100) { hundredths -= 100; if (whole) whole--; else { player.food_frac = 0; break; } }
    if (player.food_frac >= hundredths) {
        player.food_frac -= hundredths;
    } else if (whole) {
        whole--;
        player.food_frac = (unsigned char)(player.food_frac + 100 - hundredths);
    } else {
        player.food_frac = 0;
    }
    if (player.food != whole) player_stats_dirty = 1;
    player.food = whole;
}

void player_add_time(unsigned char hundredths) {
    unsigned int f = player.time_frac + hundredths;
    while (f >= 100) { f -= 100; player.time++; }
    player.time_frac = (unsigned char)f;
}

void player_hurt(unsigned int damage) {
    player.hits = (player.hits > damage) ? player.hits - damage : 0;
    player_stats_dirty = 1;
}

/* player.food -= (7 - vehicle) / 14; player.time += (7 - vehicle) / 7 */
void player_consume_food(void) {
    unsigned char k = 7 - player.vehicle;
    player_sub_food((k * 100u + 7u) / 14u);   /* rounded: 50 on foot */
    player_add_time((k * 100u + 3u) / 7u);    /* 100 on foot */
}

/* player.food -= 0.05; player.time += 0.5 */
void player_wait_penalty(void) {
    player_sub_food(5);
    player_add_time(50);
}
