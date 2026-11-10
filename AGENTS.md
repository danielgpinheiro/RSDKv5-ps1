# AGENTS.md — RSDKv5-ps1 (Sonic Mania on PlayStation)

Operating manual for this repo. Workspace rules: `../AGENTS.md`. Knowledge base: `../docs/` (start at
`../docs/README.md`). **Plan and phase table: `../docs/32-rsdkv5-mania-port-plan.md`. Lessons from the
earlier ports: `../docs/29` (Sonic CD; §9 Sonic 2) and `../docs/31` (real hardware).**

## What this is
`RSDKModding/Sonic-Mania-Decompilation` (the game, C, `SonicMania/`) with its `RSDKv5-Decompilation`
submodule (the engine, `dependencies/RSDKv5/`) being ported to the PS1 with **psyqo** (C++20) on branch
`ps1`. Remotes: `origin` = upstream RSDKModding (read-only), `fork` = github.com/danielgpinheiro/RSDKv5-ps1
(**private**; push here). The submodule stays upstream (read-only, like `../nugget`); the engine the port builds is the vendored copy
`RSDKv5/RSDK` (phase 2.1).

## First checks (every session)
- `git status` / `git log --oneline -10` (uncommitted work may be another agent's).
- Phase + status in `../docs/32` ("Status (2026-10-16)" and "Where we are and how to resume (2026-10-16)") and the
  `rsdkv5-ps1-port` skill. Speed work: `../docs/42` (Mania's limits), `../docs/43` (the v3 / v4 study).
- Game data: `../SonicMania_Extracted/` (the user's Mania **Plus** `Data.rsdk`, 208 MB, SHA-256 `77a90438…`,
  extracted `Data/`). Never commit game data.
- `../nugget` and `dependencies/RSDKv5` stay plain upstream.
- Emulators (`tools/emu.sh`; all shared with other sessions: `pgrep` first, never a second instance, kill only your
  own PIDs, never `pkill`; runs are muted; the harness restores the settings it changes):
  - **`EMU=redux`** (the default; user 2026-10-16: "Use stock pcsx-redux instead of fork"): the stock PCSX-Redux app
    (`/Applications/PCSX-Redux.app`, GDB 3333); `EMU_SLOT=1-4` (2026-10-26) gives a parallel session its own portable
    instance instead (`../pcsx-redux-maniaN`, GDB 3400+N). The perf scripts take `EMUS=`.
  - `EMU=ds`: DuckStation with the user's SCPH-101 BIOS (accurate settings, GDB 2345, `../docs/31` §6), allowed for
    Mania whenever no other session runs it (the same rule for every RSDK port, user 2026-10-16).
  - The Bash tool is zsh (`$REDUX_ARGS` doesn't word-split): launch emulators from bash scripts, i.e. the harness.
- GDB: Homebrew gdb 18.1 (2026-10-16) can't attach to PCSX-Redux's stub ("invalid remote ptid: p1.t1",
  `../pcsx-redux/src/core/gdb-server.cc:503`). Every harness script runs `tools/gdbw` (+ `tools/gdbrelay.py`,
  `1539b3b2`; `GDB=` overrides), which rewrites only a `-x` script's exact `target remote localhost:N` line.

## User decisions (2026-09-26)
- Fresh upstream clone (`9dc6994`, submodule `bd59396`) + private fork; the `ps1/` layer and tools come from
  `RSDKv3-ps1` (`8d7c1cd`, with the real-hardware fixes).
- References: the Dreamcast port (`../../Dreamcast/RSDKv5-Decompilation`, `Sonic-Mania-Decompilation`,
  michael-fadely forks) and our own PS1 ports. Not the Xbox v5 port.
- **Scope: base Mania** (decided after phase 1): Mania mode, Sonic/Tails/Knuckles; no Encore, Mighty, Ray,
  Competition, replays, Puyo (reversible build/data switches).
  *Updated 2026-10-26*: Sonic, Tails, Knuckles alone (Sonic & Tails was modded-console only, now out of scope); Mighty and Ray on
  this repo's discs (`PLUS ?= 1`, user 2026-10-09; the builder keeps `--plus-characters` off by default: the engine's
  license); Puyo in (9f, `1017bebe`); Pinball in (9k, 2026-10-30: the star post with every gold medal; its table layers
  are not drawn yet); no Encore, Competition, replays.
- **Graphics (phase 3.1, 2026-09-26)**: single characters (no Sonic & Tails: VRAM); character art 15 colours per frame;
  bosses / big effects / UI art at half resolution where a scene needs it (least visible first, HUD last).
- Icons: `../Memcard icons/sonic-mania-loading.gif` (loading, quantised to 15 colours), `mania-mc-icon.png`
  (save); credit to be given by the user before the public README. *Updated 2026-10-16*: the builder README credits
  them "author unknown" (the other builders' wording); confirm with the user before publishing.
- Carried over: 320 px, 2 MB, golden rule, **no frame skip**, byte-exact evidence per step, plans approved
  before implementing (autonomous while the user sleeps: recommended choices, logged in `../docs/32`).
  *Updated 2026-10-19 (phase 9t)*: frame rate: **the target is 60 fps at one logic step in every act and the UFO
  stages, no frameskip, on a stock PS1**; every speed number is judged against 262 hblanks a frame at one step.
  *Updated 2026-10-30*: the UFO stages run at 30 fps with two logic steps always; the Uncapped FPS option is gone
  (`g_ps1Uncapped` a test knob: `make BOOT=c,i CAP=1` caps the zones); opt-in Frameskip (default Off, 13b `649b3d49`)
  is the Video page's only row. Mode: **fully autonomous** since
  2026-10-09 ("Go fully autonomous until finish every phase"): plan → implement → verify with evidence → commit + push
  `fork ps1`; design choices logged in `../docs/32`'s choices table; publishing the builder needs the user's OK.

**Resume from `../docs/32` "Where we are and how to resume (2026-10-16)"** (current state, branch policy, what's next).
History: "Resumed 2026-09-28" (the review after Sonic 2) and the superseded "History: where we stopped on 2026-09-26".

## Branches and builder
- `ps1`: the stock port (2 MB RAM, 1 MB VRAM) and the **only focus** (user 2026-10-15). Push to `fork ps1`.
- Modded consoles (8/16 MB, 2 MB VRAM mod, Sonic & Tails, CD cache, full-res sprites; `ps1-mods` head `eaf7cadf`):
  **out of the plan** since 2026-10-26 (user: "Remove mods consoles phase for now"). No ps1-mods builds, discs, merges or
  builder `mods` exports; the branch stays untouched and unmerged. `build/dist_mods/` was deleted 2026-10-15.
- Builder: private github.com/danielgpinheiro/Sonic-Mania-PS1-ISO-Builder (`../Sonic-Mania-PS1-ISO-Builder`), main
  `13df6ec` pushed 2026-10-31 (executable from `7234a48f`, the title's build stamp; image SHA-256 `3b7c4fb1…`, equal to the port's `make disc PLUS=0` image except the two intro videos re-encoded by FFmpeg 9.0.2; `CHANGELOG.md`); its branch `mods` (`2533da0`) is local, never
  pushed. Refresh with `tools/export_builder.sh` (natural EXE only); publishing needs the user's OK.

## Build / run / debug
```bash
make objects STAGES=Logos,Title,Menu,AIZ,GHZCutscene,GHZ  # resident objects + one code overlay per stage folder (tools/gen_overlays.py plan;
                                # a Presentation folder's overlay also takes the resident functions only it calls, 4.2a)
make gfx [STAGES=...]           # build-time graphics (phase 3): tile images (.ptl) + sprite atlas (.atl, scenes with a manifest)
tools/gfx/capture_manifests.sh END_FRAME   # the sprite files each loaded scene uses -> tools/gfx/manifests/<Folder><N>.txt
tools/gfx/layer_reference.sh GHZ 1 200     # 8 MB BOOT build: host render of frame 200's layers vs the framebuffer (pixels)
make -j8 [MEASURE_RAM=8M] [BOOT=1,0]  # engine + game at 2 MB; 8M = measuring link (rsdkv5.map); BOOT = start at GameConfig category,scene
make -j8 PLATFORM_TEST=1        # phase 0 smoke-test app (ps1/platform_test.cpp) on the PS1 layer; rsdkv5.ps-exe
python3 tools/extract_rsdk.py ../SonicMania_Extracted/Data.rsdk ../SonicMania_Extracted   # RSDKv5 pack -> Data/
tools/data_disc.sh              # phase 2 data disc: Data/ without source media, raw scene/tile blocks, tileset .pal
TIMEOUT=120 tools/boot_test.sh [OUT] [END_FRAME]  # headless boot (EMU=redux default / ds): scene loads, datasets, guards
TIMEOUT=900 tools/play_test.sh tools/play/ghz1_run.gdb OUT   # GHZ1 scripted run (BOOT=1,0): compare OUT.txt with
                                # tools/play/ghz1_run.ref.txt (the F lines without hbl=)
tools/smoke_disc.sh             # phase 0 test disc (GHZ subset + Jump.vag) + make disc
tools/smoke_test_ds.sh [OUT]    # phase 0 smoke test in DuckStation (file CRCs, SPU, loading icon, fps)
tools/smoke_test.sh [OUT]       # the same in PCSX-Redux (+ VRAM icon pixels, pad), when it is free
python3 tools/loading/build_icon.py "../Memcard icons/sonic-mania-loading.gif" ps1/loading_icon.h --quantize 15
make sfx | music | video | meshes # build-time SFX banks, CD-XA music, STR video, PMD6 meshes (build/sfx, music, video, meshes)
make disc [PLUS=0]              # build/game.bin/.cue from build/iso + overlays + build outputs; PLUS=1 (default): Mighty, Ray
make dist                       # licensed disc (../LICENSE.DAT) from the natural 2 MB build only -> build/dist/ (refuses
                                # BOOT= / MEASURE_RAM / PROFILE / SAMPLE / PERF_HUD builds); runs make disc first
make BOOT=1,0 CAP=1 | FSKIP=1   # a BOOT= build starts uncapped: CAP=1 capped at 30 fps, FSKIP=1 Frameskip on
make PS1_LOGS=1 | PERF_HUD=1 | PROFILE=1 | SAMPLE=1   # game logs kept / timing HUD / per-class CPU / PC sampler (EMU_RAM=8mb)
tools/dev_disc.sh [BOOT=c,i]    # the DEVMENU=1 debug disc (8 MB link; Select opens the engine's stage select), built in
                                # ../../work/shadow/mania-devmenu -> build/dev/SonicMania-PS1-DEV.cue; emulator 8 MB RAM on
                                # (docs/ports/mania/32 "Stage Select dev menu"); dist / export refuse it; the shadow is
                                # deleted after (KEEP_SHADOW=1 keeps it for tools/play/devmenu/run.sh, which deletes it)
make DEBUG_OVERLAY=1 [BOOT=c,s] # FPS / RAM / VRAM / SPU top-right, zero VRAM (GP0 60h rectangles), L2 + R2 hides it, a [PERF]
                                # TTY line a second (EMU_LOG=FILE keeps PCSX-Redux's); tools/play/overlay_run.gdb, overlay_ab.sh
                                # (zero-VRAM / cost / toggle A/B), overlay_heap.gdb, overlay_check.py (docs/ports/mania/32 "Debug
                                # overlay"); dist / export refuse it
make UFO_NO_DECOR=0 UFO_NO_SHADOWS=0   # the UFO stages' stock look (both 1 by default)
make STREAM_MUSIC=1             # the Super theme streamed into a 2 KB SPU ring (ps1/music_stream.cpp); off by default:
                                # the module costs 4,248 B of STG, which mania-release-fbz2-fit's stg-room-8k guard fails
                                # (plan 63 M5); make dist and tools/export_builder.sh refuse a build with it on
python3 tools/build_assets.py --rsdk Data.rsdk --exe ... --ovl ... --work W --out O   # the builder's whole pipeline
tools/export_builder.sh ../Sonic-Mania-PS1-ISO-Builder   # refresh the builder (natural EXE only)
# Build stamp: every disc gets VERSION.TXT (tools/gen_version.py: commit, -dirty; the builder: bin/VERSION.TXT from the
# export); the Title overlay draws it bottom-left (ps1/version_text.cpp resident, ps1/version_stamp.cpp gen_overlays EXTRAS
# 'stamp'; VRAM row 511 x 0-255 + CLUT (288, 511)). PS1_VERSION_FILE=path / none
tools/play/perf_scene.sh NAME BOOT   # a scene's frame timing (EMUS="redux ds" default); perf_sweep.sh /
                                # perf_all.sh: act sweeps; perf_gpu.sh + gpu_cost.py: a chain's GPU cost (no screen clip yet)
```
Emulator runs in the background with a timeout (~60 s for quick runs, `TIMEOUT=` for long ones). GDB for evidence
(`../docs/21`) through `tools/gdbw`; stubs: PCSX-Redux 3333, DuckStation 2345 (`../docs/31` §6: `file` the ELF
before `target remote`, `set architecture mips:3000`).

## Layout
| Path | Role |
|---|---|
| `SonicMania/` | the game (upstream): `Objects/<zone>/*.c` (610 files), `Game.h`, `GameLink.h` |
| `dependencies/RSDKv5/RSDKv5/` | the engine (upstream submodule, read-only): `RSDK/{Core,Scene,Graphics,Audio,Storage,Input,User}` |
| `RSDKv5/RSDK/` | **the engine the port builds**: vendored from the submodule at `bd59396`, PS1 changes behind `RETRO_PLATFORM == RETRO_PS1` (diff against the submodule to see them); PS1 backends in `{Graphics,Audio,Input}/PS1` |
| `ps1/rsdk5_main.cpp`, `ps1/ps1_limits.h` | the psyqo app (RunRetroEngine's init + loop); limits shared by engine and game (entity slots) |
| `tools/gfx/` (`vram_plan.py`, `build_tiles.py`, `build_atlas.py`, `scene_usage.py`, `manifests/`), `RSDK/Scene/PS1/PS1Tiles.*`, `RSDK/Graphics/PS1/PS1Atlas.*`, `ps1/vram.*` | phase 3 graphics: the VRAM plan; scene tile images (`.ptl`) and sprite atlases (`.atl`: per-scene frame selection, player / event groups, 15-colour characters, half-res big sprites as needed), all self-checked; their loaders, frame bindings, group swaps; VRAM upload + GDB readback (`g_ps1VramDumpReq`, 8 MB link) |
| `tools/gen_overlays.py`, `ps1/overlay.cpp`, `tools/trig/` | code overlays: resident set, per-folder overlays (`build/ovl/<folder>.bin` on the disc as `Data/PS1Ovl/`), object pointers + jump stubs in the resident; the loader; build-time trig tables |
| `ps1/` | PS1 platform layer (see `ps1/README.md`), from Sonic CD: CD stream + file index, XA music, SPU, MDEC/STR (CPU-driven), IRQ guard, loading icon, memory card, chained GPU renderer, GTE 3D |
| `tools/extract_rsdk.py`, `tools/rsdkv5_filelist.txt` | RSDKv5 datapack extractor (md5 names + the Dreamcast port's file list) |
| `tools/disc/`, `tools/loading/`, `tools/memcard/` | disc builder + verifier + stage-local copies, icon builders, memory card images and tests |
| `tools/audio/` | `sfx_sets.py` + `build_sfx.py` (per-scene SPU banks, `make sfx`), `build_music.py` (CD-XA, `make music`), `xa_adpcm.py` |
| `tools/fmv/build_str.py` | the video sequences with their music -> STR (`make video`) |
| `tools/mesh/build_meshes.py` | the Dreamcast optimiser pass (no stripify) then PMD6 with distance LODs (`make meshes`; `../docs/18`, `../docs/32` 7.2 / 7.4) |
| `tools/build_assets.py`, `tools/export_builder.sh` | the builder's whole pipeline; the builder export |
| `tools/emu.sh`, `tools/gdbw`, `tools/gdbrelay.py`, `tools/boot_test.sh`, `tools/play_test.sh`, `tools/load_profile.sh` | harness core: emulator start / stop (`EMU=`), the gdb 18 wrapper + relay, headless boot, GDB-scripted runs, load profile |
| `tools/play/` | test scripts: GHZ1 run + reference, `perf_*.sh` timing, `shot.sh` pictures, `menus/` (scripted pad), `boss/` (recipes, `snap.sh`; README), `deaths/`, `special/`, `wholegame/`, `ring/`, many `*.gdb` probes |
| `tools/smoke_*.sh`, `vramshot.py`, `pad.lua` | phase 0 harness |

## Invariants (from the Nexus, Sonic CD and Sonic 2 ports)
- **2 MB RAM** (`RAM_SIZE=2M`); track heap high-water incl. load peaks. v5 needs per-zone code overlays and
  per-scene pools (`../docs/32`): nothing "stock" fits.
- **Entity memory (docs/32 7b.2)**: static rings and plane switches go **dormant** out of the cameras' reach (records +
  a pool; game specs `ps1DormantSpecs` in `Objects/Global/Ring.c`); reserved slots are sized by the game's table
  `ps1ReservedSpecs` (`Objects/Global/Zone.c`, class names: a pointer reference would pin a class in every act overlay);
  folders in `tools/gen_overlays.py` SPLIT get one overlay per act (`<folder>_<scene>`; left-out classes counted apart).
  A new class the game puts in a reserved slot, or a new dormant class, goes in those tables; watch `relocations`,
  `ghostWrites`, `g_ps1DormantFail`, `g_ps1EntityCreateOverwrites`, `g_ps1StageObjectsMissing` (all must stay 0).
  A dormant spec needs a review (Create sets only its own fields: no RNG, counters or children; no foreach_all / slot /
  pointer use by other classes, else list them in `unless`); diffs only for state an update leaves (36 B a record).
  Act overlays may leave out classes kept code names only in act-only or pointer-checked code (`FORCE_DROP`, reviewed);
  they never leave out setups or classes whose StageLoad has global effects (`keeps_effects`, `SAFE_DROP` reviewed).
  Review each overlay's left-out list (boot log `ACTOVL`, `build/gen/ovl/<unit>_reg.c`): pictures cannot show a
  missing StageLoad (FBZ1 once ran without FBZSetup). After a build, `tools/ovl_stub_check.py` lists calls an overlay
  makes into functions it lacks (unfilled stubs: `g_ps1UnlinkedCalls`); link real ones with `LINK_ONLY` (LRZ1).
- **Layouts (7b.3)**: chunk index → 8-chunk segments → 4×4 chunks (4 row ids) → 4-tile rows, all deduplicated per scene
  by `tools/data_disc.py` (self-checked); read tiles through `PS1ChunkRow` / `PS1ChunkIdx` / `PS1ChunkTiles` /
  `PS1TilePtr`, write only through `PS1SetTile` (copy-on-write at each level).
- **Load-time memory (7b.4)**: atlas bindings live in DATASET_TMP during the StageLoads; sprite frames share identical
  hitbox sets (frame block trimmed); a group swap stages in STG's tail, else TMP.
- **Golden rule**: no runtime asset decoding; converters decode their output back and compare.
- Per-frame drawing is **DMA-chained** (`ps1/gpu_chain.hh`); fragments ≤ 14 words; immediate VRAM writes
  go to the other buffer (`../docs/29` §3). Clip primitives beyond the GPU limits. **One 60 KB arena** (3.3b; 50 → 60 KB in 7.1): a
  frame's first write waits for the last chain's DMA (`g_ps1ChainIdleWaits`); watch `g_ps1ChainHighWater` / overflow.
- Tile layers (`RSDK/Scene/PS1/PS1Layers.cpp`, `../docs/32` 3.3b): bands written straight into the arena, bands of one
  tile row from a scratchpad column table, one-colour runs as rectangles (mask bit set: exact pixels). The `.ptl` is
  PTL2: tiles + one-colour line tables + H-flipped copies for per-line rows; `SetTile` / `DrawAniTile` on their tiles
  switch those off. The scratchpad (1 KB) belongs to the layer drawer. After `make gfx`, rebuild the disc before
  comparing with `layer_reference.sh` (the host reads `build/gfx`).
- **Overlay objects have header dependencies** (`OVL_DEPS` in the Makefile, phase 8): before it, a header change left
  overlay objects stale (the Menu read globals 5,888 B off). After touching a shared header, `make` must recompile the
  overlay objects; check with the `.dep` files.
- **foreach_all class ranges (9.4a)**: `GetAllEntities` walks only a class's [lo, hi] (`ps1ClassLo/Hi`, a superset kept by
  `PS1ClassNote`). Any new code that writes an entity slot's `classID` outside the engine's create / reset / copy paths
  (game code included) must call `PS1ClassNoteEntity(entity)` after the write; prove a change with
  `make EXTRA_CPPFLAGS=-DPS1_CHECK_FOREACH=1` (touch `RSDKv5/RSDK/Scene/Object.cpp` first: the flag has no stamp) and
  `g_ps1ForeachWrong` = 0 over the boots.
- **Sprite bindings (7b.12)**: a frame is stored full, half or quarter resolution (`PS1_ATLAS_SHIFT`; quarter = half bit +
  CLUT field from 0x140, build_atlas `QUARTER`, for backdrops no plan step fits: TMZ's Monarch). Frames past the GPU's
  primitive size are drawn as their on-screen part (`PS1DrawFrame`).
- **Pads = `psyqo::AdvancedPad`**. **One CD action at a time**. CLUT index 0 = transparent; visible black
  = 0x8000. Hardware access in `start()` / `frame()`, not `prepare()`.
- Real hardware (`../docs/31`): keep `ps1/irq_guard`, the CPU-driven MDEC, bounded CD waits.
- From Sonic 2 (`../docs/29` §9): sprite texel edges (a mirrored / rotated quad never starts an axis at its far edge);
  every drawing path clips to the GPU limits and culls off-screen primitives; fades reach direct-colour primitives;
  optional big arrays allocated only for the scenes that use them, first inside the load bracket; load time = seeks
  first, then bytes; one asset pipeline for the port and the builder; `RSDK_AUTOBUILD=1` (the engine's license).

## Definition of done for a step
Builds cleanly · fits 2 MB · verified in PCSX-Redux with evidence · `../docs/32` status
updated · commit (prefixes `feature:` `fix:` `chore:` `housekeeping:`) and push to `fork ps1` when the user
asks (or per verified step while the user is away).

*Updated 2026-10-16* (fully autonomous: commit + push `fork ps1` per verified step):
- Verified in the stock PCSX-Redux (`EMU=redux`; DuckStation too when free) with evidence; GHZ1 scripted run identical to
  `tools/play/ghz1_run.ref.txt`; guards 0.
- After resident code / BSS growth: boot FBZ2 at 2 MB (`BOOT=1,7`, or `tools/play/boss/runrec.sh pcsx fbz2`) and
  record STG used / limit (room 9,172 B since `72ca5593`); `mipsel-none-elf-size rsdkv5.elf` before / after.
- Snapshot cleanup (`f0a83464`): run scripts delete the build snapshot they made (`tools/play/boss/snap.sh -d NAME`);
  `KEEP_SNAP=1` keeps it for `NOBUILD=1` reuse; making a snapshot prunes any older than `SNAP_DAYS` (3) days. Every new
  run script must do the same (228 GB had piled up).
- Cleanup after runs (user 2026-10-28; `../../AGENTS.md` "Cleanup after runs"): once the results are read, delete what
  the runs made:
  - shadows (`../../work/shadow/<yours>`; `tools/dev_disc.sh` and `tools/play/devmenu/run.sh` delete theirs unless
    `KEEP_SHADOW=1`);
  - worktrees (`git worktree remove ../../work/worktrees/<yours>`), never one with uncommitted work;
  - the emulator state: an `EMU_SLOT` run empties `../../emu/pcsx-redux-maniaN` when it ends (the folder stays), and
    `EMU=redux` restores `pcsx.json` and deletes its backup.

  `../../tests/run.py` deletes its own shadow and `tests/cache/mania/release.*` when it ends (`--keep` before a
  `--no-build` run).
- No Claude / AI attribution in commits or PRs (no `Co-Authored-By` trailer).

## GPU type and flip rectangles (2026-10-19, ledger MA-190)
`ps1/gpu_detect.*`: `PS1DetectGpu` in `start()` before `PS1LoadingBegin` (psx-spx "GPU Detection": GP1(10h) index 4,
then 7; else GP0(E1h) bit 12 -> GPUSTAT bit 12 after E6h = 0): type 0 (the old GPU; PCSX-Redux and the stock
DuckStation) never uses GP0(E1h) flip bits; types 1 / 2 run a 2x1 readback, and flip rectangles are used only when it
comes back mirrored (`g_ps1GpuType`, `g_ps1FlipTest`, GDB `g_ps1FlipRectsOn` / `g_ps1FlipRectsForce`). Then band tiles
(`DrawBandInto`) and 1:1 flipped `PS1TexRect` sprites are flip rectangles (`PS1FlipRectTex`); lake rows keep their `.ptl`
H-flipped copies and V-flip line sprites. Resident cost +1,186 B inside the EXE's 2 KB step (watch the gap between the
last `.data` symbol and `__data_end`: a step costs FBZ2 2 KB of STG room). Check on the DuckStation preview:
`../tests/tools/mania_flip_ab.py --boot 1,0` (harness builds `make BOOT=c,i FLIPAB=1 [FLIPAB_OFF=1]`, never in
`make dist`); `mania-release-gpu-detect` (PCSX-Redux, type 0).
