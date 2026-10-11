# Open Sosaria - Sega Master System port
# Requires the devkitSMS toolchain (scripts/setup_toolchain.sh in the
# sega-master-system-devkitsms skill). Installs to ~/.devkitsms by default.
#
# Bank-switched ROM: code and small data live in the fixed 32 KiB. Large data
# is split into 16 KiB banks (BANKS), each compiled into its own CONST segment
# and placed at its slot-2 window by the linker. See PORTING_PLAN.md.

DEVKIT   ?= $(HOME)/.devkitsms
PROG     ?= opensosaria

CC       := sdcc
IHX2SMS  := ihx2sms
FOLDER2C := $(DEVKIT)/bin/folder2c

# Banks in use. Bank n sits at file offset n*0x10000 + 0x8000 for the linker
# (the bank number is the top byte; see the skill's Makefile.banked).
BANKS       := 2 3 4 5 6 7
BANKFLAGS   := -Wl-b_BANK2=0x28000 -Wl-b_BANK3=0x38000 -Wl-b_BANK4=0x48000 -Wl-b_BANK5=0x58000 -Wl-b_BANK6=0x68000 -Wl-b_BANK7=0x78000
BANKRELS    := $(addsuffix .rel,$(addprefix bank,$(BANKS)))

# EXTRA_CFLAGS=-DDEBUG_TIMERS turns on the emulator cycle timers (src/debug.h).
CFLAGS   := $(EXTRA_CFLAGS) -mz80 -I$(DEVKIT)/include --peep-file $(DEVKIT)/include/peep-rules.txt -I. -Isrc
LDFLAGS  := -mz80 --no-std-crt0 --data-loc 0xC000 $(BANKFLAGS)

OBJS     := main.rel overworld.rel player.rel hud.rel text.rel encounter.rel town.rel dungeon.rel dungeon_view.rel scan_asm.rel hud_asm.rel dungeon_tables.rel data.rel

all: $(PROG).sms

# Game data (data/*.bin, produced by tools/build_overworld.py from the user's
# disks) is turned into C arrays named <file>_bin by folder2c. Only the top
# level of data/ is picked up; data/banked/ is handled below.
data.c data.h: $(wildcard data/*)
	$(FOLDER2C) data data

%.rel: src/%.c src/*.h data.h
	$(CC) $(CFLAGS) -c $< -o $@

data.rel: data.c
	$(CC) $(CFLAGS) -c $< -o $@

# Hand-written Z80 for the dungeon cell scan (src/scan_asm.s).
scan_asm.rel: src/scan_asm.s
	sdasz80 -o $@ $<

# Hand-written Z80 for the paced HUD cell writes (src/hud_asm.s).
hud_asm.rel: src/hud_asm.s
	sdasz80 -o $@ $<

# Banked data: data/banked/bank<N>.bin becomes gen/bank<N>.c, a single array
# named bank<N>_data, compiled into that bank's segment.
gen/bank%.c: data/banked/bank%.bin tools/bin2c_bank.py
	@mkdir -p gen
	python3 tools/bin2c_bank.py $< bank$*_data $@

bank%.rel: gen/bank%.c
	$(CC) -mz80 --constseg BANK$* -c $< -o $@

# SDCC 4.5 + the bundled SMSlib print "Conflicting sdcc options" (sdcccall(1)
# vs. the library's default) and exit 1 even though the link is good. Only
# fail on real link errors (undefined symbols, errors, missing output).
.PHONY: $(PROG).ihx
$(PROG).ihx: $(OBJS) $(BANKRELS)
	$(CC) -o $@ $(LDFLAGS) $(DEVKIT)/lib/crt0_sms.rel $(OBJS) $(BANKRELS) \
	      $(DEVKIT)/lib/SMSlib.lib $(DEVKIT)/lib/PSGlib.lib 2> link.log; \
	  if grep -q "Undefined\|error\|Error" link.log; then cat link.log; exit 1; fi; \
	  grep -v "Conflicting sdcc options\|sdcccall(1)" link.log || true; \
	  test -s $@

$(PROG).sms: $(PROG).ihx
	$(IHX2SMS) $< $@

playtest: $(PROG).sms
	@mkdir -p shots
	smstest $(PROG).sms tests/playtest.txt
	smstest $(PROG).sms tests/banks.txt
	smstest $(PROG).sms tests/town.txt
	smstest $(PROG).sms tests/enter.txt
	smstest $(PROG).sms tests/dungeon.txt
	smstest $(PROG).sms tests/dungeon_input.txt
	smstest $(PROG).sms tests/combat.txt
	@rm -rf shots/walk && mkdir -p shots/walk
	smstest $(PROG).sms tests/walk_frames.txt
	python3 tools/check_walk_frames.py shots/walk

clean:
	rm -f *.rel *.ihx *.sym *.lst *.map *.noi *.lk *.asm *.rst link.log \
	      $(PROG).sms data.c data.h
	rm -rf gen

.PHONY: all playtest clean
