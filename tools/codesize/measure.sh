#!/bin/bash
# Phase 1.3: code size of every Mania object compiled for MIPS (-Os, function sections), text/data/bss.
cd "$(dirname "$0")/../.."
OUT=build/codesize/sizes.txt; : > $OUT; FAIL=build/codesize/failed.txt; : > $FAIL
find SonicMania/Objects -name "*.c" | sort | while read f; do
  o=build/codesize/$(echo "$f" | tr '/' '_').o
  if mipsel-none-elf-gcc -Os -c -I ps1/libc -I SonicMania -I SonicMania/Objects -DRETRO_REVISION=3 -DRETRO_USE_MOD_LOADER=0 -DGAME_INCLUDE_EDITOR=0 -DMANIA_PREPLUS=0 -DMANIA_FIRST_RELEASE=0 -msoft-float -ffunction-sections -fdata-sections -o "$o" "$f" 2>>$FAIL.log; then
    mipsel-none-elf-size "$o" | tail -1 | awk -v f="$f" '{print f, $1, $2, $3}' >> $OUT
  else
    echo "$f" >> $FAIL
  fi
done
echo "done: $(wc -l < $OUT) compiled, $(wc -l < $FAIL) failed"
