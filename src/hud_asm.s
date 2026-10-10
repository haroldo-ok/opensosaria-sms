; ---------------------------------------------------------------------------
; hud_put_tiles: write _hud_put_n name-table cells to the VDP data port, at
; the address already set (SMS_setNextTileatXY). The low byte of each cell
; comes from the byte array at _hud_put_src, the high byte (tile bit 8 and
; the attributes) is _hud_put_hi for every cell. Used by the HUD (font
; tiles, hi 0) and the overworld's row streaming (terrain tiles, hi 1).
;
; Paced for writes with the display on: consecutive OUTs are 29 or 31 cycles
; apart, above the 27 of SMSlib's own SMS_crt0_RST18 ("VRAM SAFE").
; About 60 cycles per cell, against ~150 through SMS_setTile from C.
; Register use: A, B, C, HL.
; ---------------------------------------------------------------------------
	.module hud_asm

	.globl _hud_put_tiles
	.globl _hud_put_src
	.globl _hud_put_n
	.globl _hud_put_hi

	.area _DATA
_hud_put_src:	.ds 2
_hud_put_n:	.ds 1
_hud_put_hi:	.ds 1

	.area _CODE

_hud_put_tiles:
	ld	hl, (_hud_put_src)
	ld	a, (_hud_put_n)
	or	a
	ret	z
	ld	b, a
	ld	a, (_hud_put_hi)
	ld	c, a
1$:
	ld	a, (hl)
	out	(0xBE), a
	inc	hl
	ld	a, c
	nop
	nop
	out	(0xBE), a
	djnz	1$
	ret
