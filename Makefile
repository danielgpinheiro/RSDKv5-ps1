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
RSDKv5/RSDK/Scene/PS1/PS1RotoPage.cpp \
RSDKv5/RSDK/Scene/PS1/PS1ScanSleep.cpp \
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
ps1/perf_hud.cpp \
ps1/gpu_chain.cpp \
ps1/gpu_detect.cpp \
ps1/spu.cpp \
ps1/xa_music.cpp \
ps1/mdec.cpp \
ps1/video.cpp \
ps1/gte3d.cpp \
ps1/s3d_faces.s \
ps1/s3d_blend.s \
ps1/floor_quads.s \
ps1/scan_sleep.s \
ps1/band_fast.s \
ps1/scan_fast.s \
ps1/fmv.cpp \
ps1/loading.cpp \
ps1/version_text.cpp \
ps1/version_stamp.cpp \
ps1/memcard.cpp \
ps1/user_card.cpp \
ps1/overlay.cpp \
ps1/vram.cpp \
ps1/stack.cpp \
ps1/pad.cpp \

# `make DEVMENU=1` (docs/ports/mania/32 "Stage Select dev menu"): the engine's dev menu, its stage select only, opened with
# Select, on an 8 MB link (run it with the emulator's 8 MB RAM option on; a console showing 2 MB stops at a red screen).
# A debug build for the emulators: `make dist` and tools/export_builder.sh refuse it; tools/dev_disc.sh builds its disc
# in a shadow copy, never in this checkout.
ifneq ($(DEVMENU),)
PS1_SRCS += ps1/dev_menu.cpp
endif
# `make DEBUG_OVERLAY=1` (docs/ports/mania/32 "Debug overlay"): FPS, main RAM, VRAM and SPU RAM in the top-right corner as
# flat rectangles (no VRAM), L2 + R2 hides / shows it, and a [PERF] TTY line a second (ps1/debug_overlay.cpp). A debug
# build: `make dist` and tools/export_builder.sh refuse it.
ifneq ($(DEBUG_OVERLAY),)
PS1_SRCS += ps1/debug_overlay.cpp
endif

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
# `make PERF_HUD=1`: the frame-timing numbers in the top-left corner (ps1/perf_hud.cpp, docs/32 9f); never in `make dist`.
ifneq ($(PERF_HUD),)
CPPFLAGS += -DPS1_PERF_HUD=1
endif
# docs/32 9f (UFO 60 fps plan, step C): the UFO stages' detail cuts, each its own switch (off: the stock look)
#   UFO_FLOOR_NEAR=1  the floor's real tiles to 12 rows (24), the rest from the minimap; ON by default
#   UFO_LOWPOLY=1     the 3D models drawn with their distance LOD at any size (the characters ~1/2, the UFO 0.4); ON by
#                     default (user 2026-10-21, docs/32 9u.17: "go ahead" with the visible cuts for 60 fps; 0: the stock look)
# and the last resort (user 2026-10-08, after every other optimisation: "go to decorations and shadow off path"):
#   UFO_DECOR_FAR=N   the decorations' draw distance, N quarters of their update range (3: the Dreamcast port's)
#   UFO_NO_DECOR=1    no decorations (trees, flowers, pillars, birds: each removes itself in its Create); ON by default
#   UFO_NO_SHADOWS=1  no shadows (the player's, the UFO's, the rings' and spheres'); ON by default (0: the stock look)
# Measured (docs/32 9f, all 7 stages, both emulators): the draw distance cut changes nothing (decorations are drawn close up
# only); decorations off is the big step (UFO1 472 -> 379 CPU hblanks a frame), shadows off a further 2-5.
UFO_NO_DECOR   ?= 1
UFO_NO_SHADOWS ?= 1
UFO_FLOOR_NEAR ?= 1
UFO_LOWPOLY    ?= 1
UFO_CUTS := $(if $(filter 1,$(UFO_FLOOR_NEAR)),floornear )$(if $(filter 1,$(UFO_LOWPOLY)),lowpoly )$(if $(UFO_DECOR_FAR),decorfar$(UFO_DECOR_FAR) )$(if $(filter 1,$(UFO_NO_DECOR)),nodecor )$(if $(filter 1,$(UFO_NO_SHADOWS)),noshadows )
ifneq ($(UFO_DECOR_FAR),)
CPPFLAGS += -DUFO_DECOR_FAR=$(UFO_DECOR_FAR)
endif
ifeq ($(UFO_NO_DECOR),1)
CPPFLAGS += -DUFO_NO_DECOR=1
endif
ifeq ($(UFO_NO_SHADOWS),1)
CPPFLAGS += -DUFO_NO_SHADOWS=1
endif
ifeq ($(UFO_FLOOR_NEAR),1)
CPPFLAGS += -DUFO_FLOOR_NEAR=1
endif
ifeq ($(UFO_LOWPOLY),1)
CPPFLAGS += -DUFO_LOWPOLY=1
endif
ifneq ($(BOOT),)
CPPFLAGS += -DPS1_BOOT_CATEGORY=$(word 1,$(subst $(comma), ,$(BOOT))) -DPS1_BOOT_INDEX=$(word 2,$(subst $(comma), ,$(BOOT)))
endif
# docs/32 choice 182: the zones run at 30 fps with two logic steps unless Options > Video > Uncapped FPS is Yes. A BOOT=
# build skips the menu (no options loaded) and starts uncapped, as every harness measured before; `CAP=1` starts capped.
ifneq ($(and $(BOOT),$(CAP)),)
CPPFLAGS += -DPS1_BOOT_CAP=1
endif
# `make PS1_LOGS=1` keeps the game's LogHelpers prints (off in every other build: 6 KB of resident RAM)
ifneq ($(PS1_LOGS),)
CPPFLAGS += -DPS1_GAME_LOGS=1
endif
# docs/32 13b: `FSKIP=1` starts a BOOT= build with Options > Video > Frameskip on.
ifneq ($(and $(BOOT),$(FSKIP)),)
CPPFLAGS += -DPS1_BOOT_FSKIP=1
endif
# `make BOOT=c,i FLIPAB=1 [FLIPAB_OFF=1]`: the flip-rectangle A/B harness (ps1/gpu_detect.hh, ../tests/tools/mania_flip_ab.py):
# a scripted pad, framebuffer CRCs over the TTY port; FLIPAB_OFF=1 starts with the flip path off. Never in `make dist`.
ifneq ($(and $(BOOT),$(FLIPAB)),)
CPPFLAGS += -DPS1_FLIP_AB=1
ifneq ($(FLIPAB_OFF),)
CPPFLAGS += -DPS1_FLIP_AB_OFF=1
endif
endif
ifneq ($(DEVMENU),)
CPPFLAGS += -DPS1_DEVMENU=1
ifneq ($(SAMPLE),)
$(error DEVMENU=1 links at 8 MB: the PC sampler's histogram (0x80400000, ps1/pc_sampler.cpp) would sit inside STG)
endif
endif
ifneq ($(DEBUG_OVERLAY),)
CPPFLAGS += -DPS1_DEBUG_OVERLAY=1
# its heap counters: every block psyqo's allocator hands out or takes back (operator new: ps1/libc/new_guard.cpp)
LDFLAGS  += -Wl,--wrap,libc_malloc -Wl,--wrap,libc_free -Wl,--wrap,libc_realloc
LDFLAGS  += -Wl,--wrap,_ZdlPv -Wl,--wrap,_ZdaPv -Wl,--wrap,_ZdlPvj -Wl,--wrap,_ZdaPvj
endif
# `make EXTRA_CPPFLAGS=-D...`: debug/bisect switches (e.g. -DPS1_NO_IRQ_GUARD=1).
CPPFLAGS += $(EXTRA_CPPFLAGS)
# `make MEASURE_RAM=8M`: a measuring link only (sizes of an over-budget build); the port targets 2 MB.
MEASURE_RAM ?= 2M
ifneq ($(MEASURE_RAM),2M)
CPPFLAGS  += -DPS1_DEBUG_BIGRAM=1 # measuring datasets (RSDKv5/RSDK/Storage/Storage.cpp); never a release setting
endif
# DEVMENU=1 links at 8 MB without PS1_DEBUG_BIGRAM's datasets and audits (STG takes the RAM above 2 MB by itself)
LINK_RAM := $(if $(and $(DEVMENU),$(filter 2M,$(MEASURE_RAM))),8M,$(MEASURE_RAM))
LDFLAGS   += -Wl,--defsym,RAM_SIZE=$(LINK_RAM) -Wl,-Map,rsdkv5.map
# The switches above are defines: a stamp per switch (rewritten only when its value changes) rebuilds just the
# objects that read it, so a BOOT= / MEASURE_RAM= / PROFILE= change can't leave a stale object behind. A change also
# deletes those objects: an object compiled in the same second as the stamp (another make running) looks up to date.
PS1_BOOT_OBJS    := ps1/rsdk5_main.o ps1/gpu_detect.o
PS1_SAMPLE_OBJS  := ps1/rsdk5_main.o ps1/pc_sampler.o
# (Drawing.o: it includes Graphics/PS1/PS1RenderDevice.cpp, which has no object of its own)
PS1_HUD_OBJS     := RSDKv5/RSDK/Graphics/Drawing.o ps1/perf_hud.o
PS1_OVL_OBJS     := RSDKv5/RSDK/Graphics/Drawing.o ps1/gpu_chain.o ps1/debug_overlay.o ps1/libc/new_guard.o \
                    RSDKv5/RSDK/Scene/Scene.o RSDKv5/RSDK/Graphics/Animation.o RSDKv5/RSDK/Audio/Audio.o
PS1_RAM_OBJS     := ps1/vram.o RSDKv5/RSDK/Storage/Storage.o RSDKv5/RSDK/Graphics/Drawing.o RSDKv5/RSDK/Scene/Scene.o
PS1_UFOCUT_OBJS  := RSDKv5/RSDK/Graphics/PS1/PS1Scene3D.o RSDKv5/RSDK/Graphics/PS1/PS1UfoFloor.o \
                    SonicMania/Objects/UFO/UFO_Decoration.o SonicMania/Objects/UFO/UFO_Shadow.o
PS1_PROFILE_OBJS := ps1/rsdk5_main.o RSDKv5/RSDK/Storage/Storage.o RSDKv5/RSDK/Core/RetroEngine.o RSDKv5/RSDK/Scene/Object.o \
                    RSDKv5/RSDK/Scene/Scene.o SonicMania/Objects/Global/Soundboard.o RSDKv5/RSDK/Graphics/Drawing.o
# DEVMENU: every engine and PS1-layer object (the link size changes all of them; no game object includes an engine header)
PS1_DEVMENU_OBJS := ps1/rsdk5_main.o ps1/dev_menu.o $(patsubst %.s,%.o,$(patsubst %.cpp,%.o,$(ENGINE_SRCS) $(PS1_SRCS)))
define ps1_stamp
$(shell mkdir -p build/gen; [ -f build/gen/$(1).txt ] && [ "$$(cat build/gen/$(1).txt)" = "$(2)" ] || { echo "$(2)" > build/gen/$(1).txt; rm -f $(3); })
endef
$(call ps1_stamp,boot,$(BOOT)$(if $(and $(BOOT),$(CAP)), cap)$(if $(and $(BOOT),$(FSKIP)), fskip)$(if $(and $(BOOT),$(FLIPAB)), flipab$(if $(FLIPAB_OFF),off)),$(PS1_BOOT_OBJS))
$(call ps1_stamp,ram,$(MEASURE_RAM),$(PS1_RAM_OBJS))
$(call ps1_stamp,profile,$(PROFILE),$(PS1_PROFILE_OBJS))
$(call ps1_stamp,sample,$(SAMPLE),$(PS1_SAMPLE_OBJS))
$(call ps1_stamp,perfhud,$(PERF_HUD),$(PS1_HUD_OBJS))
$(call ps1_stamp,dbgovl,$(DEBUG_OVERLAY),$(PS1_OVL_OBJS))
$(call ps1_stamp,ufocut,$(UFO_CUTS),$(PS1_UFOCUT_OBJS))
# (a tree that never built DEVMENU=1 gets its empty stamp dated 2000: its natural objects are kept, not all rebuilt)
$(shell mkdir -p build/gen; [ -f build/gen/devmenu.txt ] || [ -n "$(DEVMENU)" ] || { echo > build/gen/devmenu.txt; touch -t 200001010000 build/gen/devmenu.txt; })
$(call ps1_stamp,devmenu,$(DEVMENU),$(PS1_DEVMENU_OBJS))
$(PS1_DEVMENU_OBJS): build/gen/devmenu.txt
$(PS1_BOOT_OBJS): build/gen/boot.txt
$(PS1_RAM_OBJS): build/gen/ram.txt
$(PS1_PROFILE_OBJS): build/gen/profile.txt
$(PS1_SAMPLE_OBJS): build/gen/sample.txt
$(PS1_HUD_OBJS): build/gen/perfhud.txt
$(PS1_OVL_OBJS): build/gen/dbgovl.txt
$(PS1_UFOCUT_OBJS): build/gen/ufocut.txt
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
RSDKv5/RSDK/Graphics/PS1/PS1Scene3D.o RSDKv5/RSDK/Graphics/PS1/PS1UfoFloor.o ps1/gte3d.o RSDKv5/RSDK/Scene/PS1/PS1RotoPage.o: CPPFLAGS += -O2
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
# Their header dependencies too (nugget's common.mk makes .dep files for SRCS only): without them 193 overlay objects kept
# GlobalVariables' offsets from before 7b.3 (atlEntityData 0x1000 -> 0xA40 words) and the Menu read its load statuses
# 5,888 bytes off, waiting forever behind its fade (found in phase 8, 2026-10-02)
OVL_DEPS    = $(patsubst %.o,%.dep,$(OVL_OBJS))
ifneq ($(MAKECMDGOALS), clean)
-include $(OVL_DEPS)
endif
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
STAGES ?= Logos,Title,Menu,DAGarden,Puyo,AIZ,GHZCutscene,GHZ,CPZ,SPZ1,SPZ2,FBZ,PSZ1,PSZ2,SSZ1,SSZ2,HCZ,MSZ,OOZ1,OOZ2,LRZ1,LRZ2,LRZ3,MMZ,TMZ1,TMZ2,TMZ3,ERZ,MSZCutscene,Ending,Credits,TimeTravel,Summary,Thanks,SpecialBS,UFO1,UFO2,UFO3,UFO4,UFO5,UFO6,UFO7,Pinball
objects:
	/usr/bin/python3 tools/gen_overlays.py plan --stages $(STAGES)

# Build-time graphics (docs/32 3.1): each scene of the STAGES folders -> build/gfx/<Folder>/Scene<N>.ptl (tile images)
# and, for the scenes with a sprite manifest (tools/gfx/manifests), Scene<N>.atl (the sprite atlas; it reads the
# disc's sprite files: run tools/data_disc.sh first)
# phase 11: MANIA_DATA (the builder's data), as rsdk5_formats.data_root()
GAME_DATA ?= $(or $(MANIA_DATA),../SonicMania_Extracted/Data)
GFX_CHARACTERS ?= sonic tails knux mighty ray # 9b.1: an atlas per character (Scene<N>.atl, _tails, _knux; 9g: _mighty, _ray; presentation scenes: one)
# (round 3: an atlas that does not fit stops the build; `| head -1 || exit 1` took head's status and kept a stale atlas)
gfx:
	for f in $(subst $(comma), ,$(STAGES)); do \
	  ids=$$(cd $(GAME_DATA)/Stages/$$f && ls Scene*.bin | sed 's/Scene\(.*\)\.bin/\1/'); \
	  /usr/bin/python3 tools/gfx/build_tiles.py $(GAME_DATA) build/gfx $$f $$ids || exit 1; \
	  for i in $$ids; do \
	    if [ -f tools/gfx/manifests/$$f$$i.txt ]; then \
	      for c in $(GFX_CHARACTERS); do \
	        out=$$(/usr/bin/python3 tools/gfx/build_atlas.py $(GAME_DATA) build/iso/Data build/gfx $$f $$i $$c 2>&1) || { echo "$$out"; exit 1; }; \
	        echo "$$out" | head -1; \
	      done; \
	    fi; \
	  done; \
	done

# Build-time SFX (docs/32 5.1): one SPU-ADPCM bank per scene of the STAGES folders -> build/sfx/Stages/<Folder>/PS1Sfx<N>.bin
# (tools/audio/sfx_sets.py picks what each scene's objects play; rates in build/sfx_rates.txt)
# 9g: SFX_CHARACTERS (Mighty, Ray: the Plus characters setting) get bank variants with their own sounds where a scene's
# set changes (PS1Sfx<N>_<char>.bin)
SFX_CHARACTERS ?= mighty ray
sfx:
	/usr/bin/python3 tools/audio/build_sfx.py $(GAME_DATA) build/sfx $(subst $(comma), ,$(STAGES))
	for c in $(SFX_CHARACTERS); do /usr/bin/python3 tools/audio/build_sfx.py --char $$c $(GAME_DATA) build/sfx $(subst $(comma), ,$(STAGES)) || exit 1; done

# Build-time music (docs/32 5.2): Data/Music/*.ogg -> build/music/Music/Music.xa (CD-XA, 8 channels) + Music.bin
music:
	/usr/bin/python3 tools/audio/build_music.py $(GAME_DATA) build/music

# Build-time video (docs/32 6.1): the video sequences with their music -> build/video/Video/<Seq>.str + .sti, PS1Video.bin
video:
	/usr/bin/python3 tools/fmv/build_str.py $(GAME_DATA) build/video

# Build-time meshes (docs/32 7.2): the Dreamcast port's mesh optimiser, then the PS1 PMD5 format -> build/meshes/Meshes
meshes:
	/usr/bin/python3 tools/mesh/build_meshes.py $(GAME_DATA) build/meshes

# docs/32 9g: PLUS=1 puts Mighty and Ray on the disc (their atlases, SFX banks and UFO meshes, built by `make gfx sfx meshes`
# with the default GFX_CHARACTERS / SFX_CHARACTERS) and writes Data/PS1Config.txt (plus_characters=1), which turns them on
# at boot (ps1/rsdk5_main.cpp PS1ReadConfig). On by default for this repository's builds (user 2026-10-09); PLUS=0 for a
# disc without them. The public builder (phase 11) keeps them OFF by default, an option there: the engine's license wants
# DLC off by default in pre-built executables.
PLUS ?= 1

# Test disc (phase 0): a hand-picked subset of ../SonicMania_Extracted/Data (tools/smoke_disc.sh),
# then mkpsxiso + the byte-level verifier. The real asset pipeline arrives in phase 1.
disc:
	rm -rf build/iso/Data/PS1Ovl && mkdir -p build/iso/Data/PS1Ovl
	for f in $(OVL_UNITS); do cp build/ovl/$$f.bin build/iso/Data/PS1Ovl/$$f.bin; done
	for d in build/gfx/*/; do f=$$(basename $$d); mkdir -p build/iso/Data/Stages/$$f && cp $$d*.ptl $$d*.atl $$d*.pda build/iso/Data/Stages/$$f/ 2>/dev/null; done; true
	for d in build/sfx/Stages/*/; do f=$$(basename $$d); mkdir -p build/iso/Data/Stages/$$f && cp $$d*.bin build/iso/Data/Stages/$$f/; done; true
	if [ -d build/meshes/Meshes ]; then cp -R build/meshes/Meshes/ build/iso/Data/Meshes/; fi
	rm -f build/iso/Data/PS1Config.txt
	if [ "$(PLUS)" = 1 ]; then printf 'plus_characters=1\n' > build/iso/Data/PS1Config.txt; \
	else rm -f build/iso/Data/Stages/*/*_mighty.* build/iso/Data/Stages/*/*_ray.* build/iso/Data/Stages/*/PS1Sfx*_mighty.bin \
	build/iso/Data/Stages/*/PS1Sfx*_ray.bin build/iso/Data/Meshes/Special/Mighty*.bin build/iso/Data/Meshes/Special/Ray*.bin; fi
	if [ -d build/video/Video ]; then mkdir -p build/iso/Data/Video && cp build/video/Video/*.str build/video/Video/*.sti build/video/Video/PS1Video.bin build/iso/Data/Video/; fi
	if [ -d build/music/Music ]; then mkdir -p build/iso/Data/Music && cp build/music/Music/Music.xa build/music/Music/Music.bin build/iso/Data/Music/; fi
	rm -rf build/iso/Data/Stages/*/PS1Local
	/usr/bin/python3 tools/disc/stage_local.py build/iso
	/usr/bin/python3 tools/disc/build_disc.py build/iso rsdkv5.ps-exe none build/game
	/usr/bin/python3 tools/disc/verify_disc.py build/game.bin build/iso rsdkv5.ps-exe none

# Phase 10: the distribution disc (licensed with the workspace's ../LICENSE.DAT, never copied into the repo; LICENSE=none
# for an unlicensed one) from the natural 2 MB build only: build/dist/SonicMania-PS1.bin/.cue + SHA256SUMS. Run
# `make && make disc` first (the disc tree, overlays, graphics, SFX, music and video come from there).
LICENSE ?= ../LICENSE.DAT
DIST    ?= build/dist/SonicMania-PS1
dist:
	@if [ -n "$$(cat build/gen/boot.txt)" ]; then echo "dist: rsdkv5.ps-exe is a BOOT= test build"; exit 1; fi
	@if [ "$$(cat build/gen/ram.txt)" != "2M" ]; then echo "dist: rsdkv5.ps-exe is a MEASURE_RAM= link"; exit 1; fi
	@if [ -n "$$(cat build/gen/profile.txt)$$(cat build/gen/sample.txt)$$(cat build/gen/perfhud.txt)" ]; then echo "dist: a PROFILE= / SAMPLE= / PERF_HUD= build"; exit 1; fi
	@if [ -n "$$(cat build/gen/dbgovl.txt 2>/dev/null)" ] || mipsel-none-elf-nm rsdkv5.elf | grep -q g_ps1DbgVisible; then echo "dist: a DEBUG_OVERLAY= build"; exit 1; fi
	@if ! mipsel-none-elf-nm rsdkv5.elf | grep -q g_ps1InitDone; then echo "dist: rsdkv5.ps-exe is not the game (PLATFORM_TEST?)"; exit 1; fi
	@# the DEVMENU debug build (8 MB): its stamp, its marker symbol and the EXE header's stack pointer, each on its own
	@if [ -n "$$(cat build/gen/devmenu.txt 2>/dev/null)" ]; then echo "dist: a DEVMENU= build"; exit 1; fi
	@if mipsel-none-elf-nm rsdkv5.elf | grep -q g_ps1DevMenuBuild; then echo "dist: rsdkv5.elf holds the dev menu (DEVMENU=)"; exit 1; fi
	@if [ "$$(od -An -tx4 -j48 -N4 rsdkv5.ps-exe | tr -d ' ')" != "801fff00" ]; then echo "dist: rsdkv5.ps-exe's stack is not the 2 MB one"; exit 1; fi
	# 9l (2026-10-09): build/iso's overlays are linked against the EXE that made them; a dist after another link's
	# make disc (an 8 MB picture run) shipped 8 MB overlays with the 2 MB EXE (black screen at the SEGA logo): the disc
	# tree is always rebuilt from this EXE first
	$(MAKE) disc
	mkdir -p build/dist
	/usr/bin/python3 tools/disc/build_disc.py build/iso rsdkv5.ps-exe $(LICENSE) $(DIST)
	/usr/bin/python3 tools/disc/verify_disc.py $(DIST).bin build/iso rsdkv5.ps-exe $(LICENSE)
	cd build/dist && shasum -a 256 SonicMania-PS1.bin SonicMania-PS1.cue > SHA256SUMS && cat SHA256SUMS

.PHONY: disc dist objects overlays gfx sfx music video meshes
