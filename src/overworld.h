/* ---------------------------------------------------------------------------
 * Overworld: Sosaria terrain on the SMS background.
 *
 * World: 2x2 realms, each 86x86 tiles with the real 64x64 map in the top-left
 * corner and water in the rest (same layout as open-sosaria's bterraMaps).
 * Total world: 172x172 tiles of 16x16 pixels.
 *
 * Screen: the 32x28 tilemap is a ring. A world cell (x,y) lives at tilemap
 * column x mod 32 and row y mod 28; scroll registers select the visible
 * window. Only the leading edge is rewritten when the camera moves, during
 * VBlank.
 * ------------------------------------------------------------------------- */
#ifndef OVERWORLD_H
#define OVERWORLD_H

#define OW_WORLD_TILES   172   /* tiles per world axis */
#define OW_TILE_BASE     384   /* VRAM tile slot of the first terrain tile. Tiles must stay below 448:
                                  the tilemap is at VRAM 0x3800 (= tile 448). Uses 384..447. */
#define OW_VIEW_TILES_W  16    /* visible 16px tiles across */
#define OW_VIEW_TILES_H  10    /* map rows; the bottom 32 px are the HUD (hud.h) */

/* Sprite tiles (sprites use VRAM tiles 0-255). They overwrite the font's
 * glyphs for characters 96-111, which the game text never uses. */
#define OW_PLAYER_TILE   64    /* 4 tiles: the 16x16 player */
#define OW_ENEMY_TILE    68    /* 12 tiles: sea, land, woods monsters */

extern unsigned int player_tx;   /* world tile coordinates, 0..171 */
extern unsigned int player_ty;
/* Set by the E (button 2) action on a town or castle tile. 0 = none, 1 = town,
 * 2 = castle. main.c reads it and clears it. */
extern unsigned char overworld_enter;

extern unsigned char overworld_respawned;  /* set after a resurrection; main.c redraws */
extern unsigned char ow_acted;             /* playerActed, read by the playtests */
extern unsigned int  ow_lag;               /* lagTime in frames */
extern volatile unsigned char ow_scroll_x; /* map X scroll, restored each VBlank (main.c) */

unsigned char overworld_terrain(unsigned int wx, unsigned int wy);  /* banked map read */
void overworld_init_graphics(void);   /* display must be off */
void overworld_begin(void);           /* new console + prompt, on (re)entering the overworld */
void overworld_redraw_full(void);     /* display must be off or in VBlank budget */
void overworld_update(unsigned int keys, unsigned int pressed);  /* RAM only */
void overworld_vblank(void);          /* scroll registers + streamed tiles */
void overworld_add_sprites(void);     /* SAT building, RAM only */

#endif
