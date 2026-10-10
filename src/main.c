/* ---------------------------------------------------------------------------
 * Open Sosaria - Sega Master System port.
 *
 * States: title -> overworld. Frame rule (devkitSMS skill): game logic is
 * RAM-only; every VRAM write (tilemap, palette, SAT) happens after
 * SMS_waitForVBlank(), except the full redraw, which turns the display off.
 * ------------------------------------------------------------------------- */
#include "SMSlib.h"
#include "PSGlib.h"
#include "data.h"
#include "overworld.h"
#include "town.h"
#include "dungeon.h"
#include "player.h"
#include "hud.h"
#include "encounter.h"
#include "debug.h"

enum { STATE_TITLE = 0, STATE_OVERWORLD = 1, STATE_TOWN = 2, STATE_DUNGEON = 3 };

/* Plain globals so the playtest harness can read them via the .noi. */
unsigned char game_state    = STATE_TITLE;
unsigned char redraw_needed = 1;     /* set by logic, consumed after VBlank */
unsigned int  frame_count   = 0;

/* HUD split: the line interrupt at line 159 sets X scroll to 0, so the
 * overworld's bottom four rows (the console) do not scroll sideways. It must
 * not touch the VDP while main code is writing VRAM (the address latch is
 * shared; see the skill's interrupts notes), so main sets vram_busy around
 * its VRAM work. hud_split_missed counts frames where the split was skipped;
 * the playtests expect it to stay 0. */
#define HUD_SPLIT_LINE 159
volatile unsigned char vram_busy = 0;
unsigned char hud_split = 0;
unsigned char display_on = 0;        /* only frames on screen can show a missed split */
unsigned char hud_split_missed = 0;

static void frame_interrupt(void) {
    if (hud_split) {
        if (!vram_busy) INLINE_SMS_setBGScrollX(0);
        else if (display_on) hud_split_missed++;
    }
    PSGFrame();
    PSGSFXFrame();
}

/* SMSlib's font, moved down one pixel. The HUD split's scroll write can
 * land a line late (interrupt latency), so the first pixel row of the HUD
 * must be blank. Glyphs that use their bottom row (comma, semicolon,
 * underscore) already have a blank top row, so they stay as they are. */
extern const unsigned char devkitSMS_font__tiles__1bpp[];
static void load_font(void) {
    unsigned char glyph[8];
    unsigned char t, y;
    const unsigned char *src = devkitSMS_font__tiles__1bpp;
    for (t = 0; t < 96; t++, src += 8) {
        if (src[7]) {
            SMS_load1bppTiles(src, t, 8, 0, 1);
            continue;
        }
        glyph[0] = 0;
        for (y = 1; y < 8; y++) glyph[y] = src[y - 1];
        SMS_load1bppTiles(glyph, t, 8, 0, 1);
    }
    SMS_configureTextRenderer(-32);
}

static void start_overworld(void) {
    game_state = STATE_OVERWORLD;
    overworld_begin();
    redraw_needed = 1;
}

/* Writes the whole 32x24 tilemap with tile 0 (blank). Only in VBlank. */
static void clear_tilemap(void) {
    unsigned int i;
    SMS_setNextTileatXY(0, 0);
    for (i = 0; i < 32u * 24u; i++) {
        SMS_setTile(0);
    }
}

static void draw_title(void) {
    clear_tilemap();
    SMS_printatXY(9,  8, "OPEN SOSARIA");
    SMS_printatXY(8, 12, "PRESS START");
}

/* ---- RAM-only logic ---------------------------------------------------- */
static void title_update(unsigned int pressed) {
    if (pressed & PORT_A_KEY_1) {
        rng_state = frame_count | 1u;      /* vary the encounters per game */
        player_new_game();
        enc_reset();
        start_overworld();
    }
}

void main(void) {
    unsigned int keys, pressed;

    /* ---- one-time setup with the display off ---- */
    SMS_displayOff();
    SMS_useFirstHalfTilesforSprites(1);
    SMS_setSpriteMode(SPRITEMODE_NORMAL);
    load_font();
    SMS_setBackdropColor(0);
    SMS_loadBGPalette(ow_palette_bin);              /* index 1 = white, used by text */
    SMS_loadSpritePalette(ow_sprite_palette_bin);
    overworld_init_graphics();

    SMS_setLineInterruptHandler(&frame_interrupt);
    SMS_setLineCounter(HUD_SPLIT_LINE);
    SMS_enableLineInterrupt();

    SMS_displayOn();
    display_on = 1;

    for (;;) {
        keys    = SMS_getKeysStatus();
        pressed = SMS_getKeysPressed();
        frame_count++;

        /* 1. RAM-only logic */
        if (game_state == STATE_TITLE) {
            title_update(pressed);
        } else if (game_state == STATE_OVERWORLD) {
            overworld_update(keys, pressed);
            if (overworld_respawned) {
                overworld_respawned = 0;
                redraw_needed = 1;
            }
            if (overworld_enter == 3) {
                /* Dungeon: world index and realm-local entrance, as in sceneDungeon_generateFloor. */
                unsigned char world = (unsigned char)((player_ty >= 86 ? 2 : 0) + (player_tx >= 86 ? 1 : 0));
                dungeon_enter(world, player_tx % 86, player_ty % 86);
                overworld_enter = 0;
                game_state = STATE_DUNGEON;
                redraw_needed = 1;
            } else if (overworld_enter) {
                town_which = (unsigned char)(overworld_enter - 1);   /* 0 town, 1 castle */
                town_px = town_which ? 1 : 20;                        /* engine start cells */
                town_py = town_which ? 11 : 20;
                overworld_enter = 0;
                game_state = STATE_TOWN;
                redraw_needed = 1;
            }
        } else if (game_state == STATE_TOWN) {
            town_update(keys, pressed);
            if (town_leave) {
                town_leave = 0;
                start_overworld();
            }
        } else {
            if (dungeon_update(keys, pressed)) redraw_needed = 1;
            if (dungeon_leave) {
                dungeon_leave = 0;
                start_overworld();
            }
        }

        /* 2. Build the sprite list in RAM */
        SMS_initSprites();
        if (game_state == STATE_OVERWORLD) overworld_add_sprites();
        if (game_state == STATE_TOWN) town_add_sprites();
        SMS_finalizeSprites();

        /* 3. VBlank: push sprites, then VRAM work */
        SMS_waitForVBlank();
        vram_busy = 1;
        DBG_START(0);
        /* Sample the joypad right after the wait, so a press that started during
         * the frame's idle time is latched. The redraw keeps the sampling going. */
        if (game_state == STATE_DUNGEON) dungeon_sample_keys();
        SMS_copySpritestoSAT();

        if (redraw_needed) {
            redraw_needed = 0;
            if (game_state == STATE_TITLE) {
                draw_title();
            } else {
                SMS_displayOff();
                display_on = 0;
                if (game_state == STATE_OVERWORLD)      overworld_redraw_full();
                else if (game_state == STATE_TOWN)      town_redraw_full();
                else                                    dungeon_redraw_full();
                SMS_displayOn();
                display_on = 1;
                /* Turned on past the split line: this frame's HUD lines still
                 * need X scroll 0. overworld_vblank restores it next frame. */
                if (game_state == STATE_OVERWORLD) {
                    unsigned char v = SMS_getVCount();
                    if (v >= HUD_SPLIT_LINE && v < 192) SMS_setBGScrollX(0);
                }
            }
        } else if (game_state == STATE_OVERWORLD) {
            DBG_START(1);
            overworld_vblank();
            DBG_STOP(1);
        } else if (game_state == STATE_TOWN) {
            town_vblank();
        }
        hud_split = (game_state == STATE_OVERWORLD);
        vram_busy = 0;
        DBG_STOP(0);
    }
}

SMS_EMBED_SEGA_ROM_HEADER(9999, 0);
SMS_EMBED_SDSC_HEADER_AUTO_DATE(0, 1,
    "Open Sosaria port",
    "Open Sosaria",
    "Port of delcodigo/open-sosaria (MIT) to Sega Master System");
