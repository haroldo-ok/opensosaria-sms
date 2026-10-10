; ---------------------------------------------------------------------------
; Dungeon cell scan (see dungeon_view.c, pass 1).
;
; For each of the 640 cells (20 rows x 32 columns, row-major, matching fb and
; prev_tile), this computes the 8-byte rotate-xor hash and checks for an empty
; cell. A non-empty cell is looked up in hash_slot/uniq the same way tile_for()
; does it in C. The result is written to prev_tile:
;   0 .. 159   index of a known tile (returned by tile_for)
;   zero_tile_g the cell is empty
;   255        the pattern is not known yet; C handles it (new tile)
;
; Register use: HL/DE/BC/A inside the loops. IX and IY are not touched.
; ---------------------------------------------------------------------------
	.module scan_asm

	.globl _scan_fast
	.globl _fb
	.globl _prev_tile
	.globl _hash_slot
	.globl _uniq
	.globl _zero_tile_g

	.area _DATA
_scan_src:	.ds 2
_scan_out:	.ds 2
_scan_cnt:	.ds 2
_scan_h:	.ds 1
_scan_probe:	.ds 1
_scan_left:	.ds 1
_scan_idx:	.ds 1

	.area _CODE

_scan_fast:
	ld	hl, #_fb
	ld	(_scan_src), hl
	ld	hl, #_prev_tile
	ld	(_scan_out), hl
	ld	hl, #640
	ld	(_scan_cnt), hl

cell_loop:
	; hash and any-bit over the 8 rows of this cell
	ld	hl, (_scan_src)
	ld	b, #8
	xor	a			; A = h = 0
	ld	c, #0			; C = any
hash_loop:
	rlca				; A = rol(h)
	ld	d, a
	ld	a, (hl)			; A = row byte
	ld	e, a
	or	c
	ld	c, a			; any |= byte
	ld	a, d
	xor	e			; h = rol(h) ^ byte
	inc	hl
	djnz	hash_loop

	ld	(_scan_h), a
	ld	a, c
	or	a
	jp nz, nonempty
	ld	a, (_zero_tile_g)
	jp store

nonempty:
	ld	a, (_scan_h)
	ld	(_scan_probe), a
	ld	a, #255
	ld	(_scan_left), a

probe:
	; slot = hash_slot[probe]
	ld	a, (_scan_probe)
	ld	e, a
	ld	d, #0
	ld	hl, #_hash_slot
	add	hl, de
	ld	a, (hl)
	or	a
	jp z, notfound		; empty slot: pattern is new
	dec	a			; A = pattern index
	ld	(_scan_idx), a

	; DE = &uniq[index] = _uniq + index * 8
	ld	l, a
	ld	h, #0
	add	hl, hl
	add	hl, hl
	add	hl, hl
	ld	de, #_uniq
	add	hl, de
	ex	de, hl			; DE = &uniq[index]

	ld	hl, (_scan_src)
	ld	b, #8
cmp_loop:
	ld	a, (de)
	cp	(hl)
	jp nz, mismatch
	inc	de
	inc	hl
	djnz	cmp_loop

	ld	a, (_scan_idx)		; match
	jp store

mismatch:
	ld	a, (_scan_probe)
	inc	a
	ld	(_scan_probe), a
	ld	a, (_scan_left)
	dec	a
	ld	(_scan_left), a
	jp nz, probe		; flags from dec: stop after 255 probes

notfound:
	ld	a, #255

store:
	ld	hl, (_scan_out)
	ld	(hl), a
	inc	hl
	ld	(_scan_out), hl

	ld	hl, (_scan_src)
	ld	de, #8
	add	hl, de
	ld	(_scan_src), hl

	ld	hl, (_scan_cnt)
	dec	hl
	ld	(_scan_cnt), hl
	ld	a, h
	or	l
	jp nz, cell_loop
	ret
