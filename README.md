# **SUPPORT THE OFFICIAL RELEASE OF SONIC MANIA (PLUS)**
+ Without assets from the official releases, this decompilation will not run.

+ You can get the official release of Sonic Mania (Plus) from:
  * Windows
    * [Via Steam](https://store.steampowered.com/app/584400/Sonic_Mania/)
    * [Via The Epic Games Store](https://www.epicgames.com/store/en-US/p/sonic-mania)
  * [Switch, via the eShop](https://www.nintendo.com/games/detail/sonic-mania-switch/)
  * [PS4, via the Store](https://store.playstation.com/en-us/product/UP0177-CUSA07023_00-SONICMANIA000000)
  * [Xbox One, via the Store](https://www.xbox.com/en-US/games/store/sonic-mania/BXH46NQT9W4Q/0001)

Even if your platform isn't supported by the above official releases, you **must** buy it for the assets. The Netflix Games version is NOT supported.

## **DO NOT USE THIS DECOMPILATION PROJECT AS A MEANS TO PIRATE SONIC MANIA (PLUS)**
We do not condone using this project as a means for piracy in any form. This project was made with love and care for the source material and was created for purely educational purposes, and would not exist without the work of Sega, Headcannon, and Evening Star.

If you want to transfer your save from the official PC versions, you can just copy your savedata into the folder containing the decompilation!

# Sonic Mania on the PlayStation 1

**This repository is the Sonic Mania → PlayStation 1 port**, on branch **`ps1`** (the branch of record; `master` is
upstream). The Mania decompilation and the decompiled RSDKv5 engine are compiled for the R3000A with
[psyqo](https://github.com/grumpycoders/pcsx-redux/tree/main/src/mips/psyqo) (C++20 on `nugget`), the engine's PS1 code
behind `RETRO_PLATFORM == RETRO_PS1`, and every asset is converted at build time and played off the disc — nothing is
decoded at runtime except by the hardware itself (MDEC video, SPU / CD-XA ADPCM audio).

| | |
|---|---|
| Hardware | stock PS1: 2 MB RAM, 1 MB VRAM, 512 KB SPU RAM, 2× CD, NTSC. No modded console, no memory expansion |
| Scope | Mania Mode; Sonic / Tails / Knuckles (Mighty and Ray on this repo's discs); every zone; the Blue Spheres and UFO stages; menus, saves, music, FMV, Puyo, Pinball. No Encore, Competition or replays |
| Speed target | 60 fps at one logic step in every act; the UFO stages 30 fps with two logic steps; no automatic frame skip (`Options > Video > Frameskip` is an opt-in, default Off) |
| Output | `rsdkv5.ps-exe` (≈ 676 KB) plus one code overlay per stage folder (`build/ovl/*.bin`), mastered into `build/game.bin` / `.cue` |

The phase table, the status and the open fix list live in the workspace knowledge base (`PS1/`, reached from here as
`../docs/`), not in this repo: `../docs/ports/mania/32-rsdkv5-mania-port-plan.md` (plan and status),
`../docs/ports/mania/63-mania-20-fix-items-execution-plan.md` (the 21 open fix items) and
`../docs/perf/42-mania-performance-limits.md` (what a stock PS1 can spend).

## Quick start

```bash
git clone <this repo> RSDKv5-ps1 && cd RSDKv5-ps1   # branch ps1
mkdir -p ../SonicMania_Extracted                                    # your own game data (see below)
/usr/bin/python3 tools/extract_rsdk.py /path/to/Data.rsdk ../SonicMania_Extracted
make objects                                   # the resident object list + the overlay plan
make -j8                                       # rsdkv5.ps-exe/.elf/.map + build/ovl/*.bin
tools/data_disc.sh                             # extracted data -> build/iso/Data, then master the disc
make gfx sfx music video meshes                # build-time graphics, SFX banks, XA music, STR video, meshes
make disc                                      # re-master build/game.bin/.cue with the assets in
tools/boot_test.sh /tmp/maniaboot 900          # headless boot in PCSX-Redux, SCENE / SNAP / GUARD lines via GDB
```

The order matters: `make objects` writes `build/gen/game_srcs.txt` and the overlay plan the link depends on;
`tools/data_disc.sh` needs an `rsdkv5.ps-exe` to master a disc; `make gfx` reads `build/iso/Data`, so it runs after the
data disc.

## Requirements

| Need | What / where |
|---|---|
| Host | developed and verified on macOS arm64 (Homebrew). Only the harness's emulator paths are macOS-specific |
| MIPS toolchain | `mipsel-none-elf-gcc` **16.2.0** + binutils (empty sysroot, no newlib): `brew install nikitabobko/local-tap/mipsel-none-elf-gcc` (pulls `mipsel-none-elf-binutils`). Check: `mipsel-none-elf-gcc --version` |
| psyqo / nugget | the **sibling `../nugget`** — the Makefile includes `../nugget/psyqo/psyqo.mk`. Plain upstream, no patches to psyqo (this port was last built against `a4f9f450`) |
| Python | the scripts call **`/usr/bin/python3`** explicitly and need `Pillow` and `numpy`: `/usr/bin/python3 -m pip install --user Pillow numpy` |
| mkpsxiso | 2.30: sibling `../mkpsxiso-2.30-Darwin/bin/mkpsxiso`, or `MKPSXISO=/path`, else `mkpsxiso` on `PATH` |
| psxavenc | sibling `../psxavenc/build/psxavenc`, or `PSXAVENC=/path`, else on `PATH` |
| ffmpeg / ffprobe | on `PATH` (XA music, STR video); `FFMPEG=` / `FFPROBE=` override |
| Emulator | PCSX-Redux at `/Applications/PCSX-Redux.app` (the harness hardcodes that path); optionally DuckStation at `/Applications/DuckStation.app` with a retail BIOS (`EMU=ds`) for accuracy checks |
| gdb | Homebrew `gdb` 18.x, used **only** through the repo's `tools/gdbw`: gdb 18 rejects PCSX-Redux's stub thread id (`invalid remote ptid: p1.t1`) |
| Sony license data | only for `make dist`, as the sibling **`../LICENSE.DAT`** (`LICENSE=none` builds an unlicensed image). Never committed |

## Sibling directories (the workspace layout)

The build reaches outside the repo with parent-relative paths, so the repo builds unchanged as a git worktree or a
shadow copy. In the canonical workspace (`PS1/`) those names are symlinks created by `tools/shim.sh` from
`tools/workspace_links.txt` — one per name in `ports/`, `work/shadow/` and `work/worktrees/`, which is why a worktree
builds identically. On a standalone clone, put them next to the repo:

```
<parent>/
├── RSDKv5-ps1/                         # this repo, branch ps1
├── nugget/                             # psyqo + the shared build system (plain upstream)
├── SonicMania_Extracted/Data/          # your extracted game data (never committed)
├── mkpsxiso-2.30-Darwin/bin/mkpsxiso   # or MKPSXISO= / PATH
├── psxavenc/build/psxavenc             # or PSXAVENC= / PATH
└── LICENSE.DAT                        # only for make dist
```

`GAME_DATA=` overrides the data root (default `../SonicMania_Extracted/Data`).

## Game data (never committed)

The port needs the `Data.rsdk` of a Sonic Mania **Plus** copy you own (the PC release; the Netflix version is not
supported). `tools/extract_rsdk.py` unpacks it into `../SonicMania_Extracted/Data` as md5-addressed files (the Dreamcast
port's file list), dropping the source media the PS1 build never reads:

```bash
/usr/bin/python3 tools/extract_rsdk.py "/path/to/Sonic Mania/Data.rsdk" ../SonicMania_Extracted
ls ../SonicMania_Extracted/Data/Game/GameConfig.bin ../SonicMania_Extracted/Data/Stages/GHZ/Scene1.bin
```

`Data.rsdk`, `Data/` and `LICENSE.DAT` are never committed (`build/` is gitignored and regenerable).

## Build

`make objects` then `make -j8` (~35 s from a clean tree on a 10-core Mac: 1,972 objects, 51 overlays). The link uses
`-Wl,--defsym,RAM_SIZE=2M`, so an over-budget build is a link error rather than a console crash;
`mipsel-none-elf-size rsdkv5.elf` shows the split (`text` + `data` = the EXE, `bss` = RAM).

| Switch | Effect |
|---|---|
| `make BOOT=1,0` | start at Mania Mode → GHZ1 instead of the logos; `CAP=1` starts capped at 30 fps, `FSKIP=1` with Frameskip on |
| `make STAGES=GHZ,AIZ objects` | build overlays only for those stage folders (faster iteration). `make gfx` / `make sfx` take the same list |
| `make MEASURE_RAM=8M` | a measuring link only (read `rsdkv5.map`); `make dist` refuses it |
| `make PROFILE=1` / `SAMPLE=1` / `PERF_HUD=1` / `DEBUG_OVERLAY=1` | per-class CPU time, PC sampler, corner timings, the on-screen RAM/VRAM/SPU overlay — all debug builds, refused by `make dist` |
| `make PS1_LOGS=1` | keeps the game's own `LogHelpers` prints |
| `make PLATFORM_TEST=1` | the platform-layer smoke test instead of the game |
| `make UFO_NO_DECOR=0 UFO_NO_SHADOWS=0` | the UFO stages' stock look (both cuts are on by default for the 30 fps target) |

After touching a shared header, the overlay objects must recompile too — the Makefile handles it (`OVL_DEPS`); a `.dep`
recording a path that no longer exists is stale, not a missing file: `make clean`, then rebuild.

Building the assets is the slow half: `make gfx` took 7 minutes for GHZ's two scenes on this machine (tile images plus
one atlas per character, every one self-checked), so the full `STAGES` list is an hours-long run — narrow it while
iterating. `make sfx` takes the same list; `make music`, `make video` and `make meshes` convert the whole data set once.

`make disc` also rewrites the tracked `tools/disc/load_order.txt` (the order the game opens the disc's files). A full
`make gfx sfx music video meshes` build reproduces the committed one; a partial `STAGES=` build does not, so do not
commit that diff.

## Run it

The harness (muted, backgrounded, driven through GDB — `EMU=ds` switches to DuckStation, `EMU_SLOT=1-4` gives this
session its own portable emulator instance with GDB 3400+N):

```bash
tools/boot_test.sh /tmp/maniaboot 900                 # SCENE / SNAP / GUARD lines, exit where it crashed
tools/play_test.sh tools/play/ghz1_run.gdb /tmp/ghz1  # scripted GHZ1 run; compare with tools/play/ghz1_run.ref.txt
```

A good run ends with the scene loaded and the `GUARD` counters at zero (`dropped`, `compressed`, `unlinkedCalls`,
`stageObjectsMissing`).

By hand, the same launch the harness uses (the EXE from the repo, the disc for data and overlays):

```bash
/Applications/PCSX-Redux.app/Contents/MacOS/PCSX-Redux -2mb -fastboot -exe rsdkv5.ps-exe \
  -debugger -gdb -gdb-port 3333 -iso build/game.cue
```

Exchanging `-exe` for `-bios <retail BIOS>` boots `MANIA.EXE` from the disc through the retail BIOS — that is the shape of
the final check. Emulators and GDB ports are shared with other sessions: `pgrep -x PCSX-Redux` first, never a second
instance on the same port, mute, and kill only your own PID.

An overlay is linked against exact resident addresses, so if `rsdkv5.ps-exe` or `build/ovl/*.bin` is newer than
`build/game.bin`, re-master the disc (`tools/emu.sh` does it automatically): a stale disc runs an overlay into the wrong
code.

## Discs

```bash
make disc    # build/game.bin/.cue, byte-verified by tools/disc/verify_disc.py; PLUS=1 by default (Mighty, Ray)
make dist    # licensed image for CD-R: build/dist/SonicMania-PS1.bin/.cue + SHA256SUMS
```

`make dist` rebuilds the disc tree from the current EXE and refuses test builds (`BOOT=`, `MEASURE_RAM=`, `PROFILE=`,
`SAMPLE=`, `PERF_HUD=`, `DEBUG_OVERLAY=`, `STREAM_MUSIC=`, `DEVMENU=`). Releases for people without a toolchain come from
one pipeline, `tools/build_assets.py` (+ `tools/export_builder.sh DEST`), so the port's image and the builder's are equal
by construction.

## Repository layout

| Path | Role |
|---|---|
| `SonicMania/` | the game (upstream C): `Objects/<zone>/*.c`, `Game.c`, `GameLink.h` |
| `RSDKv5/RSDK/` | **the engine this port builds**: the decompiled RSDKv5, vendored, PS1 changes behind `RETRO_PLATFORM == RETRO_PS1`; PS1 backends in `{Graphics,Audio,Input,Scene}/PS1` |
| `dependencies/RSDKv5/` | the upstream engine submodule — reference only, never edited. The PS1 build does **not** need it: every include that resolves into it sits behind `RETRO_PLATFORM != RETRO_PS1` (only upstream's PC build wants `--recursive`) |
| `ps1/` | the platform layer: psyqo app, CD stream and file index, XA music, SPU, MDEC/STR video, IRQ guard, DMA-chained GPU renderer, GTE 3D, memory card, overlay loader — `ps1/README.md` lists every file |
| `tools/gen_overlays.py` | the resident / per-folder overlay split, written by `make objects` |
| `tools/gfx/`, `tools/audio/`, `tools/fmv/`, `tools/mesh/` | build-time converters: tile images and sprite atlases, SPU SFX banks, CD-XA music, STR video, PMD6 meshes |
| `tools/disc/` | the disc builder and its byte-level verifier |
| `tools/*.sh`, `tools/gdbw`, `tools/play/` | the emulator / GDB harness: boot test, scripted runs, perf sweeps, pictures |
| `build/` | everything generated (gitignored, regenerable) |

## Ground rules the code follows

- **2 MB RAM and 1 MB VRAM on a stock console.** There is no modded-memory or 2 MB-VRAM path.
- **No runtime asset decoding.** Sprites, tiles, scripts, SFX, music and video are converted at build time, and the
  converters decode their own output back and compare it. MDEC and SPU / CD-XA playback are the only runtime decoders.
- **Never skip drawing frames to gain speed** — make it faster instead.
- **Every frame-rate claim names its emulator.** 60 fps means ≈ 1.00 vbl in DuckStation *and* ≤ 262 CPU hblanks a frame
  at one logic step in PCSX-Redux; the UFO stages are judged against 2 vblanks a frame.
- **A frame is judged by its pixels.** Corrupted geometry, a wrong palette or a missing layer is a bug, not a close call.
- **Nothing that works today may break:** a change to a shared path (atlas, layer, sprite, XA driver) ships with an exact
  A/B, plus a row in the workspace's regression ledger and a test that fails without the fix.

## Where to read next

| File | What |
|---|---|
| `AGENTS.md` (this repo) | the operating manual: commands, invariants, the definition of done for a step |
| `ps1/README.md` | the platform layer, file by file |
| `../docs/ports/mania/32-rsdkv5-mania-port-plan.md` | the plan: phases, status table, how to resume |
| `../docs/ports/mania/63-mania-20-fix-items-execution-plan.md` | the 21 open fix items and the gates each needs |
| `../docs/perf/42-mania-performance-limits.md` | what a stock PS1 can spend: CPU, GPU, DMA, ranked |
| `../docs/ports/rsdk-common/29-rsdk-port-lessons-v3.md` | the lessons every RSDK PS1 port inherits |
| `../docs/toolchain/00-getting-started.md` | the PS1 SDK landscape and the toolchain install, per OS |

Everything under `../docs/` belongs to the workspace repository (`PS1/`), not to this one.

---

**Everything below is upstream's README for the PC / console decompilation** (`master`): CMake, vcpkg, devkitPro and the
mod loader. It does not describe the `ps1` branch's build — use the sections above. It is kept because it is how the
game, the engine and the dependencies are laid out upstream.

# Additional Tweaks
* Added a built-in mod loader and API calls to enable a much smoother modding experience.
* Added support for targeting RSDKv5U rather than standalone RSDKv5
* Added support for building almost all released versions of the game. Including: 1.00 (Console initial release), 1.03 (PC initial release) & 1.06 (Plus update)

# How to Build

This project uses [CMake](https://cmake.org/), a versatile building system that supports many different compilers and platforms. You can download CMake [here](https://cmake.org/download/). **(Make sure to enable the feature to add CMake to the system PATH during the installation if you're on Windows!)**

## Get the source code

**DO NOT** download the source code ZIP archive from GitHub, as they do not include the submodules required to build the decompilation.

Instead, you will need to clone the repository using Git, which you can get [here](https://git-scm.com/downloads).

Clone the repo **recursively**, using:
`git clone --recursive https://github.com/RSDKModding/Sonic-Mania-Decompilation`

If you've already cloned the repo, run this command inside of the repository:
```git submodule update --init --recursive```

## Getting dependencies

This repo includes everything you need for Sonic Mania + RSDKv5(U). If you wish to compile only Sonic Mania, you can skip to the [compilation steps below](#compiling) and compile with the `WITH_RSDK` flag disabled.

### Windows
To handle dependencies, you'll need to install [Visual Studio Community](https://visualstudio.microsoft.com/downloads/) (make sure to install the `Desktop development with C++` package during the installation) and [vcpkg](https://learn.microsoft.com/en-us/vcpkg/get_started/get-started?pivots=shell-cmd#1---set-up-vcpkg) (You only need to follow `1 - Set up vcpkg`).

After installing those, run the following in Command Prompt (make sure to replace `[vcpkg root]` with the path to the vcpkg installation!):
- `[vcpkg root]/vcpkg.exe install libtheora libogg glew glfw3 sdl2 --triplet=x64-windows-static` (If you're compiling a 32-bit build, replace `x64-windows-static` with `x86-windows-static`.)

Finally, follow the [compilation steps below](#compiling) using `-DCMAKE_TOOLCHAIN_FILE=[vcpkg root]/scripts/buildsystems/vcpkg.cmake -DVCPKG_TARGET_TRIPLET=x64-windows-static` as arguments for `cmake -B build`.
  - Make sure to replace `[vcpkg root]` with the path to the vcpkg installation!
  - If you're compiling a 32-bit build, replace `x64-windows-static` with `x86-windows-static`.

### Linux
Install the following dependencies: then follow the [compilation steps below](#compiling):
- **pacman (Arch):** `sudo pacman -S base-devel cmake glew glfw libtheora`
- **apt (Debian/Ubuntu):** `sudo apt install build-essential cmake libglew-dev libglfw3-dev libtheora-dev`
- **rpm (Fedora):** `sudo dnf install make cmake gcc glew-devel glfw-devel libtheora-devel zlib-devel`
- **xbps (Void):** `sudo xbps-install make cmake gcc pkg-config glew-devel glfw-devel libtheora-devel zlib-devel`
- Your favorite package manager here, [make a pull request](https://github.com/RSDKModding/Sonic-Mania-Decompilation/fork) (also update [RSDKv5U](https://github.com/RSDKModding/RSDKv5-Decompilation)!)

#### (make sure to [install GL shaders!](#q-why-arent-videosfilters-working-while-using-gl))

### Switch
[Setup devKitPro](https://devkitpro.org/wiki/Getting_Started), then run the following:
- `(dkp-)pacman -Syuu switch-dev switch-libogg switch-libtheora switch-sdl2 switch-glad`

Finally, follow the [compilation steps below](#compiling) using `-DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake` as arguments for `cmake -B build`.

#### (make sure to [install GL shaders!](#q-why-arent-videosfilters-working-while-using-gl))

### Android
**Ensure you clone the repo recursively,** then follow the RSDK build instructions [here.](https://github.com/RSDKModding/RSDKv5-Decompilation/blob/master/dependencies/android/README.md)

## Compiling

Compiling is as simple as typing the following in the root repository directory:
```
cmake -B build
cmake --build build --config release
```

The resulting build for Mania will be located somewhere in `build/` depending on your system.
If building with RSDKv5(U), the resulting Mania and RSDK executable will likely be in `build/dependencies/RSDKv5/`.

The following cmake arguments are available when compiling:
- Use these by adding `-D[flag-name]=[value]` to the end of the `cmake -B build` command. For example, to build with `RETRO_DISABLE_PLUS` set to on, add `-DRETRO_DISABLE_PLUS=on` to the command.

### RSDKv5 flags
- `RETRO_REVISION`: What revision to compile for. Takes an integer, defaults to `3` (RSDKv5U).
- `RETRO_DISABLE_PLUS`: Whether or not to disable the Plus DLC. Takes a boolean (on/off): build with `on` when compiling for distribution. Defaults to `off`.
- `RETRO_MOD_LOADER`: Enables or disables the mod loader. Takes a boolean, defaults to `on`.
- `RETRO_MOD_LOADER_VER`: Manually sets the mod loader version. Takes an integer, defaults to the current latest version.
- `RETRO_SUBSYSTEM`: *Only change this if you know what you're doing.* Changes the subsystem that RSDKv5 will be built for. Defaults to the most standard subsystem for the platform.

### Sonic Mania flags
- `WITH_RSDK`: Whether or not RSDKv5 is built alongside Sonic Mania. Takes a boolean, defaults to `on`.
  - `GAME_STATIC`: Whether or not to build Sonic Mania into the resulting RSDKv5 executable. Takes a boolean, defaults change depending on the system.
- `MANIA_FIRST_RELEASE`: Whether or not to build the first console release of Sonic Mania. Takes a boolean, defaults to `off`.
- `MANIA_PRE_PLUS`: Whether or not to build a pre-plus version of Sonic Mania. Takes a boolean, defaults to `off`.
- `GAME_INCLUDE_EDITOR`: Whether or not to include functions for use in certain RSDKv5 scene editors. Takes a boolean, defaults to `on`.
- `GAME_VERSION`: Which release version of Sonic Mania to target for. Takes an integer, defaults to `3` when `MANIA_PRE_PLUS` is enabled, and `6` otherwise (last steam release).

### Other Platforms
The only directly supported platforms are those listed above. Since Mania is very easy to build, requiring no additional dependencies, virtually any platform that can run RSDKv5 can compile Mania easily.

However, there are a multitude of ports listed in the **[RSDKv5 repository.](https://github.com/RSDKModding/RSDKv5-Decompilation)**

# FAQ
### Q: Why aren't videos/filters working while using GL?
A: There's a mod for it that you have to make. Refer to the following directions:

Create the following directory structure inside your mods directory:
```
GLShaders/
| Data/
| | ...
| mod.ini
```

Inside `mods/GLShaders/Data/`, copy the `RSDKv5/Shaders` directory, and inside the `mod.ini`, paste this:
```
Name=GLShaders
Description=GL3 shaders
Author=Ducky
Version=1.0.0
TargetVersion=5
```

### Q: I found a bug!
A: Submit an issue in the Issues tab and we might look into it. Keep in mind that this is a decompilation, so bugs that exist in official releases will most likely not be fixed here.

### Q: Will you add support for the Netflix Games version of Mania?
A: No. There are several reasons for this: the APIs introduced in the port that we'd have to deal with, the pressure of having to maintain support for a version that's being frequently updated, concerns over piracy, and a general disinterest in the port. If you want to play the decomp on mobile with touch controls, there's a [mod](https://github.com/RSDKModding/RSDKv5-Example-Mods/tree/master/ManiaTouchControls) for that you can use instead.

### Q: Will you do a decompilation for Sonic CD (2011) and/or Sonic 1/2 (2013)?
A: I already have! You can find Sonic CD [here](https://github.com/RSDKModding/RSDKv3-Decompilation) and Sonic 1/2 [here](https://github.com/RSDKModding/RSDKv4-Decompilation).

### Q: Are there anymore decompilation projects in the works, such as Sonic Origins/Sonic 3 & Knuckles?
A: Absolutely not. This project took about 1 and a half years to do, and doing Sonic 3 & Knuckles would take equally as long, if not longer, as it's not only larger in scope, but Origins' hybrid codebase makes it harder to read. Between our other decompilation projects and this one, we're done with decompiling, at least for the time being. We would also like to expand our horizons beyond Sonic going forward, and we don't wish to spend forever just playing catchup with Sega's official releases. Please do not expect any more decompilations from us, Sonic or otherwise!

# Special Thanks
* [st×tic](https://github.com/stxticOVFL) for leading ModAPI development, porting to other platforms, general decompilation assistance, helping me fix bugs, tweaking up my sometimes sloppy code and generally being really helpful and fun to work with on this project
* Everyone in the [Retro Engine Modding Server](https://dc.railgun.works/retroengine) for being supportive of me and for giving me a place to show off these things that I've found

# Contact:
Join the [Retro Engine Modding Discord Server](https://dc.railgun.works/retroengine) for any extra questions you may need to know about the decompilation or modding it.
