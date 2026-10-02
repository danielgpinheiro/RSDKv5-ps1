# RSDKv5 (Sonic Mania) PS1 port — Makefile
#
# Builds a .ps-exe via the psyqo toolchain (mipsel-none-elf, C++20), as the RSDKv2/v3/v4 ports
# (../RSDKv3-ps1/Makefile: the flags below come from there). Plan: ../docs/32.
#
# The RSDKv5 engine (RSDKv5/RSDK, vendored from the dependencies/RSDKv5 submodule at bd59396 and modified
# behind RETRO_PLATFORM == RETRO_PS1) + the Mania game code (SonicMania/: the resident objects here, the stage objects
# as code overlays, tools/gen_overlays.py) on the PS1 platform layer (ps1/, from RSDKv3-ps1 at 8d7c1cd). `make PLATFORM_TEST=1` builds the
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
RSDKv5/RSDK/Graphics/PS1/PS1Scene3D.cpp \
RSDKv5/RSDK/Graphics/PS1/PS1UfoFloor.cpp \
RSDKv5/RSDK/Graphics/PS1/PS1Deform.cpp \
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

# The game: Game.c + the resident objects of build/gen/game_srcs.txt (tools/gen_overlays.py plan, from the data).
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
ps1/pc_sampler.cpp \
ps1/gpu_chain.cpp \
ps1/spu.cpp \
ps1/xa_music.cpp \
ps1/mdec.cpp \
ps1/video.cpp \
ps1/gte3d.cpp \
ps1/fmv.cpp \
ps1/loading.cpp \
ps1/memcard.cpp \
ps1/overlay.cpp \
ps1/vram.cpp \

SRCS = ps1/rsdk5_main.cpp $(ENGINE_SRCS) $(GAME_SRCS) $(PS1_SRCS)

# The smoke test has no engine.
ifeq ($(PLATFORM_TEST),1)
SRCS := ps1/platform_test.cpp $(PS1_SRCS)
endif

# ps1/libc comes FIRST: its C/C++ shim headers (<string>, <cmath>, <stdio.h>, ...) shadow any
# system headers the freestanding toolchain might otherwise surface.
# RSDK_AUTOBUILD=1: the engine's license (dependencies/RSDKv5/LICENSE.md) wants every pre-built executable with the
# DLC off (DummyCore: Mania Plus off), which is also the port's scope, base Mania (docs/32, 2026-09-28 review).
CPPFLAGS = -I ps1/libc -I RSDKv5 -I SonicMania -I SonicMania/Objects -I dependencies/RSDKv5/dependencies/all \
  -DRETRO_PS1_BUILD -DRETRO_REVISION=2 -DRETRO_USE_MOD_LOADER=0 -DRETRO_STANDALONE=0 -DGAME_INCLUDE_EDITOR=0 \
  -DMANIA_PREPLUS=0 -DMANIA_FIRST_RELEASE=0 -DRSDK_AUTOBUILD=1

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
# `make PROFILE=1`: CPU time per object class and engine phase (root counter 2), read with GDB (docs/32 phase 4).
ifneq ($(PROFILE),)
CPPFLAGS += -DPS1_PROFILE=1
endif
# `make SAMPLE=1`: PC sampler (ps1/pc_sampler.cpp, docs/32 3.3e): run with EMU_RAM=8mb, tools/play/ghz1_sample.gdb.
ifneq ($(SAMPLE),)
CPPFLAGS += -DPS1_SAMPLE=1
endif
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
# The switches above are defines: a stamp per switch (rewritten only when its value changes) rebuilds just the
# objects that read it, so a BOOT= / MEASURE_RAM= / PROFILE= change can't leave a stale object behind. A change also
# deletes those objects: an object compiled in the same second as the stamp (another make running) looks up to date.
PS1_BOOT_OBJS    := ps1/rsdk5_main.o
PS1_SAMPLE_OBJS  := ps1/rsdk5_main.o ps1/pc_sampler.o
PS1_RAM_OBJS     := ps1/vram.o RSDKv5/RSDK/Storage/Storage.o RSDKv5/RSDK/Graphics/Drawing.o RSDKv5/RSDK/Scene/Scene.o
PS1_PROFILE_OBJS := ps1/rsdk5_main.o RSDKv5/RSDK/Storage/Storage.o RSDKv5/RSDK/Core/RetroEngine.o RSDKv5/RSDK/Scene/Object.o \
                    RSDKv5/RSDK/Scene/Scene.o SonicMania/Objects/Global/Soundboard.o
define ps1_stamp
$(shell mkdir -p build/gen; [ -f build/gen/$(1).txt ] && [ "$$(cat build/gen/$(1).txt)" = "$(2)" ] || { echo "$(2)" > build/gen/$(1).txt; rm -f $(3); })
endef
$(call ps1_stamp,boot,$(BOOT),$(PS1_BOOT_OBJS))
$(call ps1_stamp,ram,$(MEASURE_RAM),$(PS1_RAM_OBJS))
$(call ps1_stamp,profile,$(PROFILE),$(PS1_PROFILE_OBJS))
$(call ps1_stamp,sample,$(SAMPLE),$(PS1_SAMPLE_OBJS))
$(PS1_BOOT_OBJS): build/gen/boot.txt
$(PS1_RAM_OBJS): build/gen/ram.txt
$(PS1_PROFILE_OBJS): build/gen/profile.txt
$(PS1_SAMPLE_OBJS): build/gen/sample.txt
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
# docs/32 7.3: the 3D draw (an overlay's: speed over size)
RSDKv5/RSDK/Graphics/PS1/PS1Scene3D.o RSDKv5/RSDK/Graphics/PS1/PS1UfoFloor.o ps1/gte3d.o: CPPFLAGS += -O2
# docs/32 7.3: the UFO stage's objects (an overlay: no resident RAM) call abs() inline (~20 hblanks a frame as calls)
$(patsubst %.c,%.o,$(wildcard SonicMania/Objects/UFO/*.c)): CPPFLAGS += '-Dabs(n)=__builtin_abs(n)'

# Cancel make's built-in `%: %.o` link rule: the extensionless libc shim headers
# (ps1/libc/string, vector, ...) listed in .dep files must not be rebuilt from `string.o`.
%: %.o

# Code overlays (docs/32 2.4, tools/gen_overlays.py): the resident game objects link here; every other object's code
# goes to the overlay of each stage folder that lists it (build/ovl/<folder>.bin, linked at STG's fixed top less its
# size, loaded by ps1/overlay.cpp). The resident keeps every other object's pointer and a jump stub for each overlay
# function resident code or another overlay names.
OVL_FOLDERS = $(shell cat build/gen/ovl/folders.txt 2>/dev/null)
OVL_OBJS    = $(foreach f,$(OVL_FOLDERS),$(patsubst %.c,%.o,$(shell cat build/gen/ovl/$(f).srcs)))
# docs/32 7b: one overlay per folder, or per act for the folders tools/gen_overlays.py SPLIT names (<folder>_<scene>)
OVL_UNITS   = $(shell cut -d' ' -f1 build/gen/ovl/units.txt 2>/dev/null)
OVL_BINS    = $(foreach f,$(OVL_UNITS),build/ovl/$(f).bin)
STUBS = build/gen/ovl_resident
LDFLAGS += $(STUBS).o -Wl,@build/gen/ovl_keep.txt
$(TARGET).elf: $(STUBS).o
$(STUBS).c: $(OBJS) $(OVL_OBJS) tools/gen_overlays.py
	@/usr/bin/python3 tools/gen_overlays.py stubs $@ $(OBJS)
$(STUBS).o: $(STUBS).c
	$(CC) -march=mips1 -mabi=32 -EL -fno-pic -mno-shared -mno-abicalls -msoft-float -G0 -Os -g -c -o $@ $<
# a new folder's registration files are otherwise intermediate: make deletes them after the first build
.SECONDARY: $(foreach f,$(OVL_UNITS),build/gen/ovl/$(f)_reg.c build/gen/ovl/$(f)_reg.o)
build/gen/ovl/%_reg.c: $(STUBS).c
	@test -f $@
# the resident image every overlay links against: one job builds it (parallel links used to rewrite it together)
build/gen/ovl/resimg.o: $(TARGET).elf tools/gen_overlays.py
	@/usr/bin/python3 tools/gen_overlays.py resimg $(TARGET).elf
build/ovl/%.bin: $(TARGET).elf build/gen/ovl/resimg.o build/gen/ovl/%_reg.o tools/gen_overlays.py
	@/usr/bin/python3 tools/gen_overlays.py link $* $(TARGET).elf $(CC) $(ARCHFLAGS) -msoft-float -g
overlays: $(OVL_BINS)
all: overlays
ifneq ($(PLATFORM_TEST),1)
$(TARGET).$(TYPE): | $(OVL_BINS)
endif

# Trig tables computed at build time on the host (tools/trig/gen_trig.c), included by Math.cpp on PS1.
build/gen/trig_tables.inc: tools/trig/gen_trig.c
	mkdir -p build/gen
	cc -O2 -o build/gen/gen_trig $<
	build/gen/gen_trig > $@
RSDKv5/RSDK/Core/Math.o RSDKv5/RSDK/Core/Math.dep: build/gen/trig_tables.inc

# The objects to link (writes ps1/rsdk5_objects.inc + build/gen/game_srcs.txt; rerun after changing STAGES).
STAGES ?= Logos,Title,Menu,AIZ,GHZCutscene,GHZ,CPZ,SPZ1,SPZ2,FBZ,PSZ1,PSZ2,SSZ1,SSZ2,HCZ,MSZ,OOZ1,OOZ2,LRZ1,LRZ2,LRZ3,SpecialBS,UFO1,UFO2,UFO3,UFO4,UFO5,UFO6,UFO7
objects:
	/usr/bin/python3 tools/gen_overlays.py plan --stages $(STAGES)

# Build-time graphics (docs/32 3.1): each scene of the STAGES folders -> build/gfx/<Folder>/Scene<N>.ptl (tile images)
# and, for the scenes with a sprite manifest (tools/gfx/manifests), Scene<N>.atl (the sprite atlas; it reads the
# disc's sprite files: run tools/data_disc.sh first)
GAME_DATA ?= ../SonicMania_Extracted/Data
gfx:
	for f in $(subst $(comma), ,$(STAGES)); do \
	  ids=$$(cd $(GAME_DATA)/Stages/$$f && ls Scene*.bin | sed 's/Scene\(.*\)\.bin/\1/'); \
	  /usr/bin/python3 tools/gfx/build_tiles.py $(GAME_DATA) build/gfx $$f $$ids || exit 1; \
	  for i in $$ids; do \
	    if [ -f tools/gfx/manifests/$$f$$i.txt ]; then \
	      /usr/bin/python3 tools/gfx/build_atlas.py $(GAME_DATA) build/iso/Data build/gfx $$f $$i sonic | head -1 || exit 1; \
	    fi; \
	  done; \
	done

# Build-time SFX (docs/32 5.1): one SPU-ADPCM bank per scene of the STAGES folders -> build/sfx/Stages/<Folder>/PS1Sfx<N>.bin
# (tools/audio/sfx_sets.py picks what each scene's objects play; rates in build/sfx_rates.txt)
sfx:
	/usr/bin/python3 tools/audio/build_sfx.py $(GAME_DATA) build/sfx $(subst $(comma), ,$(STAGES))

# Build-time music (docs/32 5.2): Data/Music/*.ogg -> build/music/Music/Music.xa (CD-XA, 8 channels) + Music.bin
music:
	/usr/bin/python3 tools/audio/build_music.py $(GAME_DATA) build/music

# Build-time video (docs/32 6.1): the video sequences with their music -> build/video/Video/<Seq>.str + .sti, PS1Video.bin
video:
	/usr/bin/python3 tools/fmv/build_str.py $(GAME_DATA) build/video

# Build-time meshes (docs/32 7.2): the Dreamcast port's mesh optimiser, then the PS1 PMD5 format -> build/meshes/Meshes
meshes:
	/usr/bin/python3 tools/mesh/build_meshes.py $(GAME_DATA) build/meshes

# Test disc (phase 0): a hand-picked subset of ../SonicMania_Extracted/Data (tools/smoke_disc.sh),
# then mkpsxiso + the byte-level verifier. The real asset pipeline arrives in phase 1.
disc:
	rm -rf build/iso/Data/PS1Ovl && mkdir -p build/iso/Data/PS1Ovl
	for f in $(OVL_UNITS); do cp build/ovl/$$f.bin build/iso/Data/PS1Ovl/$$f.bin; done
	for d in build/gfx/*/; do f=$$(basename $$d); mkdir -p build/iso/Data/Stages/$$f && cp $$d*.ptl $$d*.atl $$d*.pda build/iso/Data/Stages/$$f/ 2>/dev/null; done; true
	for d in build/sfx/Stages/*/; do f=$$(basename $$d); mkdir -p build/iso/Data/Stages/$$f && cp $$d*.bin build/iso/Data/Stages/$$f/; done; true
	if [ -d build/meshes/Meshes ]; then cp -R build/meshes/Meshes/ build/iso/Data/Meshes/; fi
	if [ -d build/video/Video ]; then mkdir -p build/iso/Data/Video && cp build/video/Video/*.str build/video/Video/*.sti build/video/Video/PS1Video.bin build/iso/Data/Video/; fi
	if [ -d build/music/Music ]; then mkdir -p build/iso/Data/Music && cp build/music/Music/Music.xa build/music/Music/Music.bin build/iso/Data/Music/; fi
	/usr/bin/python3 tools/disc/build_disc.py build/iso rsdkv5.ps-exe none build/game
	/usr/bin/python3 tools/disc/verify_disc.py build/game.bin build/iso rsdkv5.ps-exe none

.PHONY: disc objects overlays gfx sfx music video meshes
