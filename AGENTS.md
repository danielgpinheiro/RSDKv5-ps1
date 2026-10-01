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
- Phase + status in `../docs/32` (and the `rsdkv5-ps1-port` skill).
- Game data: `../SonicMania_Extracted/` (the user's Mania **Plus** `Data.rsdk`, 208 MB, SHA-256 `77a90438…`,
  extracted `Data/`). Never commit game data.
- `../nugget` and `dependencies/RSDKv5` stay plain upstream.
- Emulators: **PCSX-Redux** (user, 2026-09-28: DuckStation belongs to another session). The harness (`tools/emu.sh`)
  uses PCSX-Redux by default, refuses to start while a PCSX-Redux on the default config runs (other sessions'
  `-portable` copies on their own GDB ports run alongside: `../docs/32` choice 80), kills only its own PID and
  restores the settings it changes (`pcsx.json`: `-8mb`); never `pkill`. DuckStation with the user's SCPH-101 BIOS
  (`EMU=ds`, `*_ds.sh`, `../docs/31` §6) only when the user allows it again.

## User decisions (2026-09-26)
- Fresh upstream clone (`9dc6994`, submodule `bd59396`) + private fork; the `ps1/` layer and tools come from
  `RSDKv3-ps1` (`8d7c1cd`, with the real-hardware fixes).
- References: the Dreamcast port (`../../Dreamcast/RSDKv5-Decompilation`, `Sonic-Mania-Decompilation`,
  michael-fadely forks) and our own PS1 ports. Not the Xbox v5 port.
- **Scope: base Mania** (decided after phase 1): Mania mode, Sonic/Tails/Knuckles; no Encore, Mighty, Ray,
  Competition, replays, Puyo (reversible build/data switches).
- **Graphics (phase 3.1, 2026-09-26)**: single characters (no Sonic & Tails: VRAM); character art 15 colours per frame;
  bosses / big effects / UI art at half resolution where a scene needs it (least visible first, HUD last).
- Icons: `../Memcard icons/sonic-mania-loading.gif` (loading, quantised to 15 colours), `mania-mc-icon.png`
  (save); credit to be given by the user before the public README.
- Carried over: 320 px, 2 MB, golden rule, **no frame skip**, byte-exact evidence per step, plans approved
  before implementing (autonomous while the user sleeps: recommended choices, logged in `../docs/32`).

**Resumed 2026-09-28** (Sonic 2 is done): `../docs/32` "Resumed 2026-09-28" (the review, Sonic 2's lessons per phase),
then "Where we stopped" for the next steps.

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
TIMEOUT=120 tools/boot_test.sh [OUT] [END_FRAME]  # headless boot (PCSX-Redux; EMU=ds DuckStation): scene loads, datasets, guards
TIMEOUT=900 tools/play_test.sh tools/play/ghz1_run.gdb OUT   # GHZ1 scripted run (BOOT=1,0): compare OUT.txt with
                                # tools/play/ghz1_run.ref.txt (the F lines without hbl=)
tools/smoke_disc.sh             # phase 0 test disc (GHZ subset + Jump.vag) + make disc
tools/smoke_test_ds.sh [OUT]    # phase 0 smoke test in DuckStation (file CRCs, SPU, loading icon, fps)
tools/smoke_test.sh [OUT]       # the same in PCSX-Redux (+ VRAM icon pixels, pad), when it is free
python3 tools/loading/build_icon.py "../Memcard icons/sonic-mania-loading.gif" ps1/loading_icon.h --quantize 15
```
Emulator runs in the background with a timeout. GDB for evidence (`../docs/21`; PCSX-Redux's stub on port 3333,
DuckStation's on 2345, `../docs/31` §6: `file` the ELF before `target remote`, `set architecture mips:3000`).

## Layout
| Path | Role |
|---|---|
| `SonicMania/` | the game (upstream): `Objects/<zone>/*.c` (610 files), `Game.h`, `GameLink.h` |
| `dependencies/RSDKv5/RSDKv5/` | the engine (upstream submodule, read-only): `RSDK/{Core,Scene,Graphics,Audio,Storage,Input,User}` |
| `RSDKv5/RSDK/` | **the engine the port builds**: vendored from the submodule at `bd59396`, PS1 changes behind `RETRO_PLATFORM == RETRO_PS1` (diff against the submodule to see them); PS1 backends in `{Graphics,Audio,Input}/PS1` |
| `ps1/rsdk5_main.cpp`, `ps1/ps1_limits.h` | the psyqo app (RunRetroEngine's init + loop); limits shared by engine and game (entity slots) |
| `tools/gfx/` (`vram_plan.py`, `build_tiles.py`, `build_atlas.py`, `scene_usage.py`, `manifests/`), `RSDK/Scene/PS1/PS1Tiles.*`, `RSDK/Graphics/PS1/PS1Atlas.*`, `ps1/vram.*` | phase 3 graphics: the VRAM plan; scene tile images (`.ptl`) and sprite atlases (`.atl`: per-scene frame selection, player / event groups, 15-colour characters, half-res big sprites as needed), all self-checked; their loaders, frame bindings, group swaps; VRAM upload + GDB readback (`g_ps1VramDumpReq`, 8 MB link) |
| `tools/gen_overlays.py`, `ps1/overlay.cpp`, `tools/trig/` | code overlays: resident set, per-folder overlays (`build/ovl/<folder>.bin` on the disc as `Data/PS1Ovl/`), object pointers + jump stubs in the resident; the loader; build-time trig tables |
| `ps1/` | PS1 platform layer (see `ps1/README.md`), from Sonic CD: CD stream + file index, XA music, SPU, MDEC/STR (CPU-driven), IRQ guard, loading icon, memory card, chained GPU renderer (joins in phase 3) |
| `tools/extract_rsdk.py`, `tools/rsdkv5_filelist.txt` | RSDKv5 datapack extractor (md5 names + the Dreamcast port's file list) |
| `tools/disc/`, `tools/loading/`, `tools/memcard/`, `tools/audio/` | disc builder + verifier, icon builders, memory card images, SFX/XA converters (from Sonic CD; adapted per phase) |
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
- **Golden rule**: no runtime asset decoding; converters decode their output back and compare.
- Per-frame drawing is **DMA-chained** (`ps1/gpu_chain.hh`); fragments ≤ 14 words; immediate VRAM writes
  go to the other buffer (`../docs/29` §3). Clip primitives beyond the GPU limits. **One 60 KB arena** (3.3b; 50 → 60 KB in 7.1): a
  frame's first write waits for the last chain's DMA (`g_ps1ChainIdleWaits`); watch `g_ps1ChainHighWater` / overflow.
- Tile layers (`RSDK/Scene/PS1/PS1Layers.cpp`, `../docs/32` 3.3b): bands written straight into the arena, bands of one
  tile row from a scratchpad column table, one-colour runs as rectangles (mask bit set: exact pixels). The `.ptl` is
  PTL2: tiles + one-colour line tables + H-flipped copies for per-line rows; `SetTile` / `DrawAniTile` on their tiles
  switch those off. The scratchpad (1 KB) belongs to the layer drawer. After `make gfx`, rebuild the disc before
  comparing with `layer_reference.sh` (the host reads `build/gfx`).
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
