/* Overworld monsters and combat. See encounter.h. */
#include "encounter.h"
#include "player.h"
#include "text.h"
#include "hud.h"

signed char   enc_monster = -1;
unsigned int  enc_hp;
unsigned char enc_number;
unsigned char enc_shown;
signed char   enc_dx, enc_dy;
unsigned char enc_sprite;
unsigned char enc_enabled = 1;
unsigned int  rng_state = 0xACE1u;

/* Disk text ids used here (ultimaStrings numbers). */
#define T_PATH_BLOCKED  121
#define T_ATTACK        126
#define T_NOTHING       127
#define T_OUT_OF_RANGE  128
#define T_WITH          129
#define T_MISS          130
#define T_HIT           131
#define T_DAMAGE        132
#define T_KILLED_GOLD   134
#define T_NOT_APPLIC    135
#define T_COMMAND        98
#define T_BEING_ATTACKED 252
#define T_BY            253
#define T_PLURAL_S      255
#define T_HITS          256
#define T_MISSES        257

/* 16-bit xorshift (7, 9, 8): period 65535. */
unsigned int rnd(void) {
    unsigned int x = rng_state;
    x ^= x << 7;
    x ^= x >> 9;
    x ^= x << 8;
    if (!x) x = 1;
    rng_state = x;
    return x;
}

/* floor(a * rand01()) */
static unsigned int mul_rnd(unsigned long a) {
    return (unsigned int)((a * rnd()) >> 16);
}

/* rand01() squared, as a 16-bit fraction */
static unsigned int rnd_sq(void) {
    unsigned long r = rnd();
    return (unsigned int)((r * r) >> 16);
}

void enc_reset(void) {
    enc_monster = -1;
    enc_shown = 0;
    enc_hp = 0;
    enc_number = 0;
}

static unsigned char in_range_monster(void) {
    return enc_monster >= 6 && enc_monster <= 20;
}

/* sceneOverworld_spawnMonsters */
static void spawn(unsigned char tile) {
    unsigned int t100 = player.time / 100u;               /* (int)(time / 100) */
    unsigned long hp_fx;                                   /* 16.16 */
    unsigned char id;

    if (!enc_enabled) return;
    if (tile > 2) tile = 1;
    if (rnd() > 6554u || (tile == 1 && rnd() > 32768u)) return;   /* > 0.1, > 0.5 */

    if (tile == 0) {
        id = (unsigned char)(6 + (rnd() >> 14));                   /* 6..9 */
        hp_fx = (unsigned long)(id - 4) * 20u * rnd() + (30ul << 16);
    } else if (tile == 1) {
        id = (unsigned char)(15 + mul_rnd(6));                     /* 15..20 */
        hp_fx = (unsigned long)(id - 14) * 10u * rnd() + (10ul << 16);
    } else {
        id = (unsigned char)(10 + mul_rnd(5));                     /* 10..14 */
        hp_fx = (unsigned long)(id - 9) * 20u * rnd() + (30ul << 16);
    }
    hp_fx += (unsigned long)rnd_sq() * t100;
    enc_monster = (signed char)id;
    enc_hp = (unsigned int)(hp_fx >> 16);
    enc_number = (unsigned char)(((unsigned long)rnd_sq() * monster_group(id)) >> 16) + 1;
}

/* sceneOverworld_resolveEncounter */
void enc_resolve(unsigned char tile) {
    unsigned char i, hits = 0;
    unsigned char id = (unsigned char)enc_monster;
    unsigned char armor = player.armor;
    unsigned int defense, roll;

    if (!in_range_monster()) {
        spawn(tile);
        return;
    }

    sb_clear(); sb_text(T_BEING_ATTACKED, 26); hud_queue(hud_sb);
    sb_clear(); sb_text(T_BY, 8); sb_str(" "); sb_num(enc_number); sb_str(" ");
    sb_text(text_monster(id), TEXT_MAX);
    if (enc_number > 1) sb_text(T_PLURAL_S, 1);
    hud_queue(hud_sb);

    for (i = 0; i < enc_number; i++) {
        defense = armor * 2u;
        if (defense > 6) defense = 5;
        if (player.vehicle > 3 && player.vehicle < 7) defense += 5;
        defense = armor * defense / 10u + armor;
        roll = mul_rnd(15ul << 8);                 /* rand01() * 15, 8.8 fixed */
        roll = (unsigned int)(((unsigned long)roll * roll) >> 16);   /* (int)pow(., 2) */
        if (roll > 80 || roll > defense) {
            player_hurt(mul_rnd(monster_rank(id)) + 1);
            hits++;
        }
    }

    sb_clear(); sb_text(T_HITS, 8); sb_num(hits); sb_text(T_MISSES, 10); sb_num(enc_number - hits);
    hud_queue(hud_sb);
}

unsigned char enc_try_dodge(signed char mx, signed char my, unsigned char tile) {
    /* (str + agi) / 400 > rand01()   or   vehicle + 2 > rand01() * 14 */
    if (enc_monster <= 0 ||
        (unsigned long)(player.strength + player.agility) * 65536ul / 400u > rnd() ||
        (unsigned int)(player.vehicle + 2) * 256u > mul_rnd(14ul << 8)) {
        enc_monster = 0;
        enc_shown = 0;
        return 1;
    }
    sb_clear(); sb_text(T_PATH_BLOCKED, 15); sb_str(" ");
    sb_text(text_monster((unsigned char)enc_monster), 8);
    hud_queue(hud_sb);

    if (tile > 2) tile = 1;
    enc_sprite = tile;
    enc_dx = mx;
    enc_dy = my;
    enc_shown = 1;
    return 0;
}

/* playerOverworld_checkIfEnemiesDead; the engine tests hp < 0. */
static void check_dead(unsigned int damage) {
    unsigned char id = (unsigned char)enc_monster;
    unsigned int gold;

    if (damage <= enc_hp) {                 /* hp - damage >= 0: still alive */
        enc_hp -= damage;
        return;
    }
    enc_number--;
    player.experience += monster_rank(id) * 5u;
    gold = mul_rnd(monster_rank(id) * 4u) + 10;
    player.gold += gold;
    player_stats_dirty = 1;

    sb_clear(); sb_text(T_KILLED_GOLD, 15); sb_str("+"); sb_num(gold);
    hud_queue(hud_sb);

    if (enc_number == 0) {
        enc_monster = -1;
        enc_shown = 0;
    } else {
        /* (int)(20 * rand01() + rand01()^2 * (int)(time / 1000)) + 5 */
        unsigned int t1000 = player.time / 1000u;
        enc_hp = (unsigned int)(((unsigned long)20u * rnd() + (unsigned long)rnd_sq() * t1000) >> 16) + 5;
    }
}

/* playerOverworld_updateAttack */
void enc_attack(void) {
    unsigned char id, w = player.weapon;
    unsigned int attack, damage;

    sb_clear(); sb_text(T_COMMAND, 11); sb_text(T_ATTACK, 12);
    hud_replace_last(hud_sb);

    if (!in_range_monster()) {
        sb_clear(); sb_text(T_NOTHING, TEXT_MAX); hud_queue(hud_sb);
        return;
    }
    id = (unsigned char)enc_monster;
    sb_clear(); sb_text(text_monster(id), TEXT_MAX); hud_queue(hud_sb);

    if ((id < 10 || id == 12) && (w < 7 || w == 11 || w == 13)) {
        sb_clear(); sb_text(T_OUT_OF_RANGE, TEXT_MAX); hud_queue(hud_sb);
        return;
    }
    sb_clear(); sb_text(T_WITH, 15); sb_text(text_table(TBL_WEAPONS, w), TEXT_MAX);
    hud_queue(hud_sb);

    if (w > 7 && w < 12) {
        sb_clear(); sb_text(text_table(TBL_WEAPONS, w), 10); sb_text(T_NOT_APPLIC, 19);
        hud_queue(hud_sb);
        return;
    }

    /* attack = rand01() * 20 + (str + agi) / 5 + weapon, compared in 8.8 */
    attack = mul_rnd(20ul << 8)
           + (unsigned int)(((unsigned long)(player.strength + player.agility) << 8) / 5u)
           + ((unsigned int)w << 8);
    if (attack > ((unsigned int)(monster_rank(id) + 10) << 8) || attack > (20u << 8)) {
        damage = mul_rnd(player.strength + w) + 1;
        sb_clear(); sb_text(T_HIT, 5); sb_text(T_DAMAGE, 10); sb_num(damage);
        hud_queue(hud_sb);
        check_dead(damage);
        return;
    }
    sb_clear(); sb_text(T_MISS, TEXT_MAX); hud_queue(hud_sb);
}
