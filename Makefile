# RSDKv5 (Sonic Mania) PS1 port — Makefile
#
# Builds a .ps-exe via the psyqo toolchain (mipsel-none-elf, C++20), as the RSDKv2/v3/v4 ports
# (../RSDKv3-ps1/Makefile: the flags below come from there). Plan: ../docs/32.
#
# The RSDKv5 engine (dependencies/RSDKv5/RSDKv5/, upstream submodule) and the Mania game code
# (SonicMania/) join in phase 2 behind RETRO_PLATFORM == RETRO_PS1, on the PS1 platform layer (ps1/,
# copied from RSDKv3-ps1 at 8d7c1cd). Until then `make PLATFORM_TEST=1` builds the phase 0 smoke test
# (ps1/platform_test.cpp) on that layer.
TARGET = rsdkv5
TYPE = ps-exe

# Engine + game sources: phase 2 (per-zone overlays; docs/32).
ENGINE_SRCS =

PS1_SRCS = \
ps1/libc/string.cpp \
ps1/libc/stdlib.cpp \
ps1/libc/math.cpp \
ps1/libc/stdio.cpp \
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
ps1/render.cpp \
ps1/memcard.cpp \

SRCS = $(ENGINE_SRCS) $(PS1_SRCS)

# The smoke test has no engine: ps1/render.cpp (which includes the engine's headers) stays out.
ifeq ($(PLATFORM_TEST),1)
SRCS := ps1/platform_test.cpp $(filter-out ps1/render.cpp,$(PS1_SRCS))
endif

# ps1/libc comes FIRST: its C/C++ shim headers (<string>, <cmath>, <stdio.h>, ...) shadow any
# system headers the freestanding toolchain might otherwise surface.
CPPFLAGS = -I ps1/libc -I dependencies/RSDKv5/RSDKv5 -I SonicMania -DRETRO_USE_MOD_LOADER=0

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
LDFLAGS   += -Wl,--defsym,RAM_SIZE=2M
# nugget's hardware-measured memcpy/memset (8-word / 4-word blocks).
LDFLAGS   += -Wl,-wrap,memcpy -Wl,-wrap,memset

# The FMV Huffman (VLC) decoder is the hot loop of video playback: optimise it for speed.
ps1/mdec.o: CPPFLAGS += -O2
# Renderer hot paths (tile bands, chain appends): speed over size.
ps1/render.o ps1/gpu_chain.o: CPPFLAGS += -O2

# Cancel make's built-in `%: %.o` link rule: the extensionless libc shim headers
# (ps1/libc/string, vector, ...) listed in .dep files must not be rebuilt from `string.o`.
%: %.o

# Test disc (phase 0): a hand-picked subset of ../SonicMania_Extracted/Data (tools/smoke_disc.sh),
# then mkpsxiso + the byte-level verifier. The real asset pipeline arrives in phase 1.
disc:
	/usr/bin/python3 tools/disc/build_disc.py build/iso rsdkv5.ps-exe none build/game
	/usr/bin/python3 tools/disc/verify_disc.py build/game.bin build/iso rsdkv5.ps-exe none

.PHONY: disc
