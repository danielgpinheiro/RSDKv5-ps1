#!/bin/bash
# Phase 0 test disc: a hand-picked subset of ../SonicMania_Extracted/Data + Jump.vag (psxavenc), then
# `make disc` (mkpsxiso + verify_disc, no license). Replaced by the real pipeline in phase 1.
set -eu
cd "$(dirname "$0")/.."
SRC=../SonicMania_Extracted/Data
PSXAVENC=${PSXAVENC:-../psxavenc/build/psxavenc}
rm -rf build/iso && mkdir -p build/iso/Data/Game build/iso/Data/Stages/GHZ build/iso/Data/SoundFX/Global
cp "$SRC/Game/GameConfig.bin" "$SRC/Game/RSDKConfig.bin" build/iso/Data/Game/
for f in StageConfig.bin Scene1.bin Scene2.bin TileConfig.bin; do
  cp "$SRC/Stages/GHZ/$f" build/iso/Data/Stages/GHZ/
done
"$PSXAVENC" -q -t vag -f 44100 "$SRC/SoundFX/Global/Jump.wav" build/iso/Data/SoundFX/Global/Jump.vag
make -s disc
