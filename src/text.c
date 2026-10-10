/* Game text from ROM banks 6 and 7. Layout: see tools/build_text.py. */
#include "SMSlib.h"
#include "text.h"

#define TEXT_BANK_A 6
#define TEXT_BANK_B 7

extern const unsigned char bank6_data[];
extern const unsigned char bank7_data[];

#define HDR_TABLES   4
#define HDR_MONSTERS 20
#define HDR_GROUP    24
#define HDR_RANK     72
#define HDR_INDEX    120

/* Reads a header word with bank 6 already mapped. */
static unsigned int hdr_word(unsigned int off) {
    return bank6_data[off] | ((unsigned int)bank6_data[off + 1] << 8);
}

unsigned char text_get(unsigned int id, char *dst, unsigned char max) {
    unsigned char saved = SMS_getROMBank();
    unsigned char n = 0;
    unsigned int entry;
    const unsigned char *src;

    SMS_mapROMBank(TEXT_BANK_A);
    if (id >= hdr_word(0)) {
        SMS_mapROMBank(saved);
        dst[0] = 0;
        return 0;
    }
    entry = hdr_word(HDR_INDEX + id * 2u);
    if (entry & 0x8000u) {
        SMS_mapROMBank(TEXT_BANK_B);
        src = bank7_data + (entry & 0x7FFFu);
    } else {
        src = bank6_data + entry;
    }
    while (n < max && src[n]) {
        dst[n] = (char)src[n];
        n++;
    }
    dst[n] = 0;
    SMS_mapROMBank(saved);
    return n;
}

static unsigned int header_word(unsigned int off) {
    unsigned char saved = SMS_getROMBank();
    unsigned int v;
    SMS_mapROMBank(TEXT_BANK_A);
    v = hdr_word(off);
    SMS_mapROMBank(saved);
    return v;
}

static unsigned char header_byte(unsigned int off) {
    unsigned char saved = SMS_getROMBank();
    unsigned char v;
    SMS_mapROMBank(TEXT_BANK_A);
    v = bank6_data[off];
    SMS_mapROMBank(saved);
    return v;
}

unsigned int text_table(unsigned char table, unsigned char i) {
    return header_word(HDR_TABLES + table * 2u) + i;
}

unsigned int text_monster(unsigned char monster) {
    return header_word(HDR_MONSTERS) + monster;
}

unsigned char monster_group(unsigned char monster) {
    return header_byte(HDR_GROUP + monster);
}

unsigned char monster_rank(unsigned char monster) {
    return header_byte(HDR_RANK + monster);
}
