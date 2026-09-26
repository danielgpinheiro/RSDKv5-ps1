# RSDKv5 (Sonic Mania) PS1 port — Makefile
#
# Builds a .ps-exe via the psyqo toolchain (mipsel-none-elf, C++20), as the RSDKv2/v3/v4 ports
# (../RSDKv3-ps1/Makefile: the flags below come from there). Plan: ../docs/32.
#
# The RSDKv5 engine (RSDKv5/RSDK, vendored from the dependencies/RSDKv5 submodule at bd59396 and modified
# behind RETRO_PLATFORM == RETRO_PS1) + the Mania game code (SonicMania/, the objects tools/gen_objects.py
# selects) on the PS1 platform layer (ps1/, from RSDKv3-ps1 at 8d7c1cd). `make PLATFORM_TEST=1` builds the
# phase 0 smoke test (ps1/platform_test.cpp) on that layer instead.
TARGET = rsdkv5
TYPE = ps-exe

ENGINE_SRCS = \
RSDKv5/RSDK/Core/RetroEngine.cpp \
RSDKv5/RSDK/Core/Math.cpp \
RSDKv5/RSDK/Core/Reader.cpp \
RSDKv5/RSDK/Core/Link.cpp \
RSDKv5/RSDK/Dev/Debug.cpp \
RSDKv5/RSDK/Storage/Storage.cpp \
RSDKv5/RSDK/Storage/Text.cpp \
RSDKv5/RSDK/Graphics/Drawing.cpp \
RSDKv5/RSDK/Graphics/Scene3D.cpp \
RSDKv5/RSDK/Graphics/Animation.cpp \
RSDKv5/RSDK/Graphics/Sprite.cpp \
RSDKv5/RSDK/Graphics/Palette.cpp \
RSDKv5/RSDK/Graphics/Video.cpp \
RSDKv5/RSDK/Audio/Audio.cpp \
RSDKv5/RSDK/Input/Input.cpp \
RSDKv5/RSDK/Scene/Scene.cpp \
RSDKv5/RSDK/Scene/Collision.cpp \
RSDKv5/RSDK/Scene/Object.cpp \
RSDKv5/RSDK/Scene/Objects/DefaultObject.cpp \
RSDKv5/RSDK/Scene/Objects/DevOutput.cpp \
RSDKv5/RSDK/User/Core/UserAchievements.cpp \
RSDKv5/RSDK/User/Core/UserCore.cpp \
RSDKv5/RSDK/User/Core/UserLeaderboards.cpp \
RSDKv5/RSDK/User/Core/UserPresence.cpp \
RSDKv5/RSDK/User/Core/UserStats.cpp \
RSDKv5/RSDK/User/Core/UserStorage.cpp \

# The game: Game.c + the objects of build/gen/game_srcs.txt (tools/gen_objects.py, from the data).
GAME_SRCS = SonicMania/Game.c $(shell cat build/gen/game_srcs.txt 2>/dev/null)

PS1_SRCS = \
ps1/libc/string.cpp \
ps1/libc/stdlib.cpp \
ps1/libc/math.cpp \
ps1/libc/stdio.cpp \
ps1/libc/time.cpp \
ps1/libc/new_guard.cpp \
ps1/cdrom_fs.cpp \
ps1/cdrom_stream.cpp \
ps1/irq_guard.cpp \
ps1/gpu_chain.cpp \
ps1/spu.cpp \
ps1/xa_music.cpp \
ps1/mdec.cpp \
ps1/video.cpp \
ps1/fmv.cpp \
ps1/loading.cpp \
ps1/sprite_atlas.cpp \
ps1/tileset.cpp \
ps1/memcard.cpp \

SRCS = ps1/rsdk5_main.cpp $(ENGINE_SRCS) $(GAME_SRCS) $(PS1_SRCS)

# The smoke test has no engine.
ifeq ($(PLATFORM_TEST),1)
SRCS := ps1/platform_test.cpp $(PS1_SRCS)
endif

# ps1/libc comes FIRST: its C/C++ shim headers (<string>, <cmath>, <stdio.h>, ...) shadow any
# system headers the freestanding toolchain might otherwise surface.
CPPFLAGS = -I ps1/libc -I RSDKv5 -I SonicMania -I SonicMania/Objects -I dependencies/RSDKv5/dependencies/all \
  -DRETRO_PS1_BUILD -DRETRO_REVISION=2 -DRETRO_USE_MOD_LOADER=0 -DRETRO_STANDALONE=0 -DGAME_INCLUDE_EDITOR=0 \
  -DMANIA_PREPLUS=0 -DMANIA_FIRST_RELEASE=0

# `make MEMCARD_TEST=1`: memory card self-test (ps1/memcard_test.cpp; tools/memcard/save_test.sh).
ifeq ($(MEMCARD_TEST),1)
CPPFLAGS += -DPS1_MEMCARD_TEST=1
SRCS += ps1/memcard_test.cpp
endif

# C++20 is the minimum required by psyqo.
CXXFLAGS = -std=c++20

# This activates the PSYQo library and the rest of the toolchain.
include ../nugget/psyqo/psyqo.mk

# The R3000A has no FPU and the engine uses float: soft-float + libgcc's helpers. Must come
# AFTER the include so it overrides common.mk's -mfp32 ARCHFLAGS.
CPPFLAGS += -msoft-float
LDFLAGS   += -msoft-float
LDFLAGS   += -lgcc
# Retail PS1 = 2 MB RAM (EXE at 0x80010000, ~1.94 MB usable). Never raise this for a release.
comma := ,
# `make BOOT=1,0`: start at GameConfig category 1 ("Mania Mode"), scene 0 (GHZ act 1) instead of the Logos (docs/32 2.5).
ifneq ($(BOOT),)
CPPFLAGS += -DPS1_BOOT_CATEGORY=$(word 1,$(subst $(comma), ,$(BOOT))) -DPS1_BOOT_INDEX=$(word 2,$(subst $(comma), ,$(BOOT)))
endif
# `make EXTRA_CPPFLAGS=-D...`: debug/bisect switches (e.g. -DPS1_NO_IRQ_GUARD=1).
CPPFLAGS += $(EXTRA_CPPFLAGS)
# `make MEASURE_RAM=8M`: a measuring link only (sizes of an over-budget build); the port targets 2 MB.
MEASURE_RAM ?= 2M
ifneq ($(MEASURE_RAM),2M)
CPPFLAGS  += -DPS1_DEBUG_BIGRAM=1 # measuring datasets (RSDKv5/RSDK/Storage/Storage.cpp); never a release setting
endif
LDFLAGS   += -Wl,--defsym,RAM_SIZE=$(MEASURE_RAM) -Wl,-Map,rsdkv5.map
# nugget's hardware-measured memcpy/memset (8-word / 4-word blocks).
LDFLAGS   += -Wl,-wrap,memcpy -Wl,-wrap,memset
# operator new stops at PS1OutOfMemory instead of returning NULL (ps1/libc/new_guard.cpp).
LDFLAGS   += -Wl,-wrap,_Znwj -Wl,-wrap,_Znaj
# Drop the unreferenced functions of the objects that are linked (function sections).
CPPFLAGS += -ffunction-sections -fdata-sections
LDFLAGS  += -Wl,--gc-sections

# The FMV Huffman (VLC) decoder is the hot loop of video playback: optimise it for speed.
ps1/mdec.o: CPPFLAGS += -O2
# Renderer hot paths (tile bands, chain appends): speed over size.
ps1/gpu_chain.o: CPPFLAGS += -O2

# Cancel make's built-in `%: %.o` link rule: the extensionless libc shim headers
# (ps1/libc/string, vector, ...) listed in .dep files must not be rebuilt from `string.o`.
%: %.o

# Game symbols the linked objects name but this build does not link: NULL object pointers + counted function stubs
# (tools/gen_stubs.py, from nm over every other object of the link).
STUBS = build/gen/unlinked_stubs
LDFLAGS += $(STUBS).o
$(TARGET).elf: $(STUBS).o
$(STUBS).c: $(OBJS) tools/gen_stubs.py
	@/usr/bin/python3 tools/gen_stubs.py $@ $(OBJS)
$(STUBS).o: $(STUBS).c
	$(CC) -march=mips1 -mabi=32 -EL -fno-pic -mno-shared -mno-abicalls -msoft-float -G0 -Os -g -c -o $@ $<

# Trig tables computed at build time on the host (tools/trig/gen_trig.c), included by Math.cpp on PS1.
build/gen/trig_tables.inc: tools/trig/gen_trig.c
	mkdir -p build/gen
	cc -O2 -o build/gen/gen_trig $<
	build/gen/gen_trig > $@
RSDKv5/RSDK/Core/Math.o RSDKv5/RSDK/Core/Math.dep: build/gen/trig_tables.inc

# The objects to link (writes ps1/rsdk5_objects.inc + build/gen/game_srcs.txt; rerun after changing STAGES).
STAGES ?= Logos,Title,GHZ
objects:
	/usr/bin/python3 tools/gen_objects.py --stages $(STAGES)

# Test disc (phase 0): a hand-picked subset of ../SonicMania_Extracted/Data (tools/smoke_disc.sh),
# then mkpsxiso + the byte-level verifier. The real asset pipeline arrives in phase 1.
disc:
	/usr/bin/python3 tools/disc/build_disc.py build/iso rsdkv5.ps-exe none build/game
	/usr/bin/python3 tools/disc/verify_disc.py build/game.bin build/iso rsdkv5.ps-exe none

.PHONY: disc objects
