# Run from Botched root after sourcing workspace scripts/env.sh.
CC = m68k-amigaos-gcc
NIO ?= $(NIO_WORKSPACE)/repos/fujinet-nio-lib
OUT ?= build/amiga
CPPFLAGS = -Iinclude -Iclient/amiga -I$(NIO)/include
CFLAGS = -m68000 -mcrt=nix13 -std=c99 -Os -Wall -Wextra -MMD -MP
SOURCES = client/amiga/main.c client/amiga/input.c client/amiga/model.c client/amiga/display.c client/amiga/network.c client/amiga/stack.c client/nio.c client/nio_fujinet.c wire/v1.c
OBJECTS = $(patsubst %.c,$(OUT)/%.o,$(SOURCES)) $(OUT)/client/amiga/worker_entry.o
all: $(OUT)/botched-amiga
$(OUT)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@
$(OUT)/%.o: %.S
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@
$(OUT)/botched-amiga: $(OBJECTS) $(NIO)/build/fujinet-nio-amiga.a
	$(CC) -m68000 -mcrt=nix13 -Wl,-Map,$(OUT)/botched.map $^ -lamiga -o $@
-include $(OBJECTS:.o=.d)
