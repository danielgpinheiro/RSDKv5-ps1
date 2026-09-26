#!/bin/bash
# Phase 2 data disc (docs/32 2.2): tools/data_disc.py (../SonicMania_Extracted/Data -> build/iso/Data, raw scene/tile
# blocks, tileset .pal, no source media), then `make disc` (build/game.bin/.cue with the current rsdkv5.ps-exe,
# mkpsxiso + verify_disc, no license).
set -eu
cd "$(dirname "$0")/.."
/usr/bin/python3 tools/data_disc.py
make -s disc
