# THE FIVE SEALS - build
# Needs cc65 (cc65, ca65, ld65) on PATH and python3.
CC65   ?= cc65
CA65   ?= ca65
LD65   ?= ld65
CFLAGS  = -t nes -Oirs -Cl -g --add-source
ROM     = build/fiveseals.nes

OBJS = build/crt0.o build/sound.o build/gen_sound.o build/main.o build/gen_data.o

all: $(ROM)

build/chr.bin src/gen_data.c src/gen_data.h src/gen_sound.s: tools/build.py tools/art.py assets/world.txt
	python3 tools/build.py

build/%.s: src/%.c src/gen_data.h src/lib.h
	$(CC65) $(CFLAGS) -o $@ $<

build/%.o: build/%.s
	$(CA65) -g -o $@ $<

build/crt0.o: src/crt0.s build/chr.bin
	$(CA65) -g -o $@ src/crt0.s

build/sound.o: src/sound.s
	$(CA65) -g -o $@ $<

build/gen_sound.o: src/gen_sound.s
	$(CA65) -g -o $@ $<

$(ROM): $(OBJS) src/fiveseals.cfg
	$(LD65) -C src/fiveseals.cfg -o $@ $(OBJS) nes.lib -m build/map.txt --dbgfile build/fiveseals.dbg
	@echo "Built $@"

clean:
	rm -f build/*.o build/*.s build/chr.bin $(ROM) build/map.txt build/fiveseals.dbg

.PHONY: all clean
