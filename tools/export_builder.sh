#!/bin/bash
# docs/ports/mania/32 phase 11: refresh the public Sonic-Mania-PS1-ISO-Builder from this port: the prebuilt executable and code
# overlays (bin/), the build-time converters tools/build_assets.py runs (builder/tools/), the in-repo sprite manifests,
# the decompilation's object sources the atlas tools read (builder/SonicMania/Objects, with their license) and the
# Dreamcast port's mesh optimiser (MIT, built on the user's machine). No game data. The builder's own files
# (build_iso.py, README.md, LICENSE.md, licenses/, requirements.txt) live in its repository and are not touched here.
# The executable must be the natural game (`make`, not a BOOT= / PROFILE / SAMPLE / PERF_HUD / DEVMENU / 8 MB link).
# Usage: tools/export_builder.sh DEST   (e.g. ../Sonic-Mania-PS1-ISO-Builder)
set -e
DEST=${1:?usage: tools/export_builder.sh DEST}
mkdir -p "$DEST" && DEST=$(cd "$DEST" && pwd)
cd "$(dirname "$0")/.."
if [ -n "$(cat build/gen/boot.txt)" ]; then echo "rsdkv5.ps-exe is a BOOT= test build: run make first"; exit 1; fi
if [ "$(cat build/gen/ram.txt)" != "2M" ]; then echo "rsdkv5.ps-exe is a MEASURE_RAM= link: run make first"; exit 1; fi
if [ -n "$(cat build/gen/profile.txt)$(cat build/gen/sample.txt)$(cat build/gen/perfhud.txt)" ]; then
  echo "a PROFILE= / SAMPLE= / PERF_HUD= build: run make first"; exit 1; fi
if ! mipsel-none-elf-nm rsdkv5.elf | grep -q g_ps1InitDone; then echo "rsdkv5.ps-exe is not the game: run make first"; exit 1; fi
# the DEVMENU debug build (8 MB, docs/ports/mania/32 "Stage Select dev menu"): its stamp, its marker, the EXE's stack, each alone
if [ -n "$(cat build/gen/devmenu.txt 2>/dev/null)" ]; then echo "a DEVMENU= build: run make first"; exit 1; fi
if mipsel-none-elf-nm rsdkv5.elf | grep -q g_ps1DevMenuBuild; then echo "rsdkv5.elf holds the dev menu (DEVMENU=): run make first"; exit 1; fi
if [ "$(od -An -tx4 -j48 -N4 rsdkv5.ps-exe | tr -d ' ')" != "801fff00" ]; then echo "rsdkv5.ps-exe's stack is not the 2 MB one: run make first"; exit 1; fi
OPTSRC=${DC_MESH_OPT_SRC:-../../Dreamcast/mania-mesh-optimizer}
[ -f "$OPTSRC/CMakeLists.txt" ] || { echo "the mesh optimiser's sources are not at $OPTSRC (DC_MESH_OPT_SRC=)"; exit 1; }
B=$DEST/builder
rm -rf "$B" "$DEST/bin"
mkdir -p "$B/tools" "$DEST/bin/ovl"
for f in build_assets.py extract_rsdk.py rsdkv5_filelist.txt rsdk5_formats.py data_disc.py gen_version.py; do cp tools/$f "$B/tools/"; done
sed -n 's/^STAGES ?= //p' Makefile | tr ',' '\n' > "$B/tools/stages.txt"
mkdir -p "$B/tools/gfx" "$B/tools/audio" "$B/tools/fmv" "$B/tools/mesh" "$B/tools/disc"
cp tools/gfx/build_atlas.py tools/gfx/build_tiles.py tools/gfx/vram_plan.py tools/gfx/scene_usage.py tools/gfx/ps1_text.py "$B/tools/gfx/"
cp -R tools/gfx/manifests "$B/tools/gfx/"
cp tools/audio/build_sfx.py tools/audio/sfx_sets.py tools/audio/build_music.py tools/audio/xa_adpcm.py "$B/tools/audio/"
cp tools/fmv/build_str.py "$B/tools/fmv/"
cp tools/mesh/build_meshes.py "$B/tools/mesh/"
cp tools/disc/build_disc.py tools/disc/stage_local.py tools/disc/verify_disc.py tools/disc/load_order.txt "$B/tools/disc/"
# the mesh optimiser (michael-fadely/mania-mesh-optimizer, MIT, with meshoptimizer, MIT): sources only
mkdir -p "$B/tools/mesh/optimizer"
( cd "$OPTSRC" && tar cf - --exclude='.git' --exclude='.gitmodules' --exclude='cmake-build-*' CMakeLists.txt LICENSE source dependencies ) | ( cd "$B/tools/mesh/optimizer" && tar xf - )
# the decompilation's object sources (the atlas tools read which animations each object sets) and its license
( find SonicMania/Objects -name '*.c' -o -name '*.h' ) | tar cf - -T - | ( cd "$B" && tar xf - )
cp LICENSE.md "$B/SonicMania/LICENSE.md"
cp rsdkv5.ps-exe "$DEST/bin/SonicMania-PS1.exe"
for u in $(cut -d' ' -f1 build/gen/ovl/units.txt); do cp build/ovl/$u.bin "$DEST/bin/ovl/"; done
cp build/gen/ovl/units.txt "$DEST/bin/ovl/units.txt"
( cd "$DEST/bin" && shasum -a 256 SonicMania-PS1.exe ovl/*.bin ovl/units.txt > SHA256SUMS )
# bin/VERSION.TXT: this port's commit, put on the builder's discs (build_disc.py) and shown on the title screen
VERSION=$(/usr/bin/python3 tools/gen_version.py "$DEST/bin/VERSION.TXT" .)
printf '# bin/\n\n`SonicMania-PS1.exe` and `ovl/`: the PS1 executable and its code overlays (one per stage folder or act,\n`units.txt` lists them), built from the RSDKv5-ps1 port at commit `%s` (`make`, `RSDK_AUTOBUILD=1`: the Plus\ncharacters stay off unless the disc carries `Data/PS1Config.txt`, see the README). SHA-256 in `SHA256SUMS`.\n\n`VERSION.TXT` (`%s`) goes on the disc; the title screen shows it bottom-left.\n' \
  "$(git rev-parse --short HEAD)" "$VERSION" > "$DEST/bin/README.md"
echo "exported $(git rev-parse --short HEAD) (version $VERSION) -> $DEST ($(find "$B" -type f | wc -l | tr -d ' ') builder files, $(ls "$DEST/bin/ovl" | wc -l | tr -d ' ') overlay files)"
