/* Cycle timers for the smstest emulator (debug port 0x06, `timers` command).
 * Only in builds made with -DDEBUG_TIMERS: on a real SMS, port 0x06 mirrors
 * the memory control port, so a release build must never write it.
 *
 * Timers in use: 0 = the whole VRAM phase after VBlank (main.c),
 * 1 = overworld_vblank, 2 = the HUD draw inside it. The VRAM phase must end
 * before line 159, where the HUD split happens; see PORTING_PLAN.md.
 *   make clean && make EXTRA_CFLAGS=-DDEBUG_TIMERS
 *   then add `timers` at the end of a playtest script. */
#ifndef DEBUG_H
#define DEBUG_H
#ifdef DEBUG_TIMERS
__sfr __at 0x06 dbg_timer_port;
#define DBG_START(id) (dbg_timer_port = (unsigned char)(((id) << 1) | 1))
#define DBG_STOP(id)  (dbg_timer_port = (unsigned char)((id) << 1))
#else
#define DBG_START(id)
#define DBG_STOP(id)
#endif
#endif
