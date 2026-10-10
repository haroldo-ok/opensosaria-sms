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

enum { STATE_TITLE = 0, STATE_OVERWORLD = 1, STATE_TOWN = 2, STATE_DUNGEON = 3 };

/* Plain globals so the playtest harness can read them via the .noi. */
unsigned char game_state    = STATE_TITLE;
unsigned char redraw_needed = 1;     /* set by logic, consumed after VBlank */
unsigned int  frame_count   = 0;

static void frame_interrupt(void) {
    PSGFrame();
    PSGSFXFrame();
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
        game_state = STATE_OVERWORLD;
        redraw_needed = 1;
    }
}

void main(void) {
    unsigned int keys, pressed;

    /* ---- one-time setup with the display off ---- */
    SMS_displayOff();
    SMS_useFirstHalfTilesforSprites(1);
    SMS_setSpriteMode(SPRITEMODE_NORMAL);
    SMS_autoSetUpTextRenderer();
    SMS_setBackdropColor(0);
    SMS_loadBGPalette(ow_palette_bin);              /* index 1 = white, used by text */
    SMS_loadSpritePalette(ow_sprite_palette_bin);
    overworld_init_graphics();

    SMS_setLineInterruptHandler(&frame_interrupt);
    SMS_setLineCounter(200);
    SMS_enableLineInterrupt();

    SMS_displayOn();

    for (;;) {
        keys    = SMS_getKeysStatus();
        pressed = SMS_getKeysPressed();
        frame_count++;

        /* 1. RAM-only logic */
        if (game_state == STATE_TITLE) {
            title_update(pressed);
        } else if (game_state == STATE_OVERWORLD) {
            overworld_update(keys, pressed);
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
                game_state = STATE_OVERWORLD;
                redraw_needed = 1;
            }
        } else {
            if (dungeon_update(keys, pressed)) redraw_needed = 1;
            if (dungeon_leave) {
                dungeon_leave = 0;
                game_state = STATE_OVERWORLD;
                redraw_needed = 1;
            }
        }

        /* 2. Build the sprite list in RAM */
        SMS_initSprites();
        if (game_state == STATE_OVERWORLD) overworld_add_sprites();
        if (game_state == STATE_TOWN) town_add_sprites();
        SMS_finalizeSprites();

        /* 3. VBlank: push sprites, then VRAM work */
        SMS_waitForVBlank();
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
                if (game_state == STATE_OVERWORLD)      overworld_redraw_full();
                else if (game_state == STATE_TOWN)      town_redraw_full();
                else                                    dungeon_redraw_full();
                SMS_displayOn();
            }
        } else if (game_state == STATE_OVERWORLD) {
            overworld_vblank();
        } else if (game_state == STATE_TOWN) {
            town_vblank();
        }
    }
}

SMS_EMBED_SEGA_ROM_HEADER(9999, 0);
SMS_EMBED_SDSC_HEADER_AUTO_DATE(0, 1,
    "Open Sosaria port",
    "Open Sosaria",
    "Port of delcodigo/open-sosaria (MIT) to Sega Master System");
