# Boss-ending recipes (phase 9b.2b, docs/ports/mania/32)

Drives a boss to its real defeat through the test-only hook in `SonicMania/Objects/Global/Zone.c`
(`g_ps1TestBoss*`, set from GDB), then lets the game's own code run the capsule / sign post, outro and next load.
Each run logs guards and sprite misses per scene. Output (generated GDB scripts, logs, build snapshots) goes to
`build/boss/` (`BOSS_WORK=` to move it).

| File | Role |
|---|---|
| `runrec.sh EMU NAME...` | per recipe: `make BOOT=` + `make disc` under a tree lock, snapshot, run on `pcsx` or `ds`, print the `L` lines. `NOBUILD=1` reuses the snapshot. EMU is `pcsx` (the stock PCSX-Redux app, `tools/emu.sh`'s default) or `ds` only: no `r16` path |
| `boss.sh OUT CLASS FN HITS DX DY WAIT PRISON` | writes and runs the GDB script (env knobs below) through `tools/play_test.sh`, whose GDB is `tools/gdbw` (the gdb 18 relay; `GDB=` overrides) |
| `fight.sh NAME BOOT OVL STATE_EXPR X Y FRAMES` | 9i: a boss fight's frame timing phase by phase (player warped to X Y at frame 120 and kept unhurt; the boss's state and running timing every 60 frames); `build/boss/fight_NAME.log` |
| `fight_sheets.py OUT` | 9i: the sprite sheets a boss fight draws (a `-DPS1_COUNT_SHEETS=1` build, `boss.sh` with `SHEETS=1`): 0 = a sheet the fight never asks for (choice 181's boss share) |
| `fight_summary.py NAME...` | 9i: one line per boss run from `bs_NAME.log`: the fight's timing, the hits, the guards, whether the next scene loaded |
| `perins.py FILE PER` | puts a PER snippet into the generated GDB script at `@PER@` (used by `boss.sh`) |
| `recipes.txt` | `name|boot|ovl|class|hitfn|hits|dx|dy|wait|env`; GDB expressions use runtime slots |
| `slotmap.py FOLDER ID NAME...` | scene slot → runtime slot (stock InitObjects compaction: 64 + earlier records passing Mania mode) |
| `snap.sh NAME` | frozen copy of the build (EXE, ELF, disc, overlay ELFs, tools) so other builds don't change a running test |
| `snap.sh -d NAME...` | deletes snapshots (~0.7 GB each). Every run script deletes the one it made when its run ends; `KEEP_SNAP=1` keeps it for later `NOBUILD=1` runs, which never delete it. Making a snapshot also removes any older than `SNAP_DAYS` (3) days |
| `p_*.gdb` (PER) | lines run every tick: `p_rings` (50 rings), `p_air` (no drowning), `p_hurtless` (no damage), `p_nofall` (no death boundary), `p_tornado` (MSZ1's Tornado ride: puts the player back on the next node when the plane flies away) |
| `x_gen.gdb` (EXTRA) | the overlay's symbols + a relocation trace |
| `x_tornado.gdb` (EXTRA) | MSZ1: the overlay's symbols; a death on the Tornado ride puts the player back on the plane |
| `shot_psz1.gdb` | a `tools/play/shot.sh` PRE file: PSZ1's Shiversaw on screen through the boss hook (`BOOT=1,8`), for pictures |

Phases: 1 warp (`WARPMODE` 0 none, 1 relative to the boss, 2 absolute before the search) → 10 wait `READY` → 2 hits
(`HITS`, `PIN`/`NTH` select the entity) → 11 `READY2` + `HITS2` → `DONEACTION` → 3 capsule (`PRISON=1`, `PREADY`,
`PSEEN`, `PWARP`, `PPAD`) → 4/5 wait for the next scene (`EPAD`, `EJUMP`), `MAXF` caps the run.
Pads: `PAD` until the hit frame, then `HOLDPAD`; bit 3 = Right, bit 4 = A (jump); `FJUMP=1` jumps every `TICK`.
`ACTION` runs at READY, `WARPACTION` after the warp.

Recipe lessons: walk into arenas (warping past them breaks bound markers and mid-bosses); hit only in a vulnerable
state; a boss whose final blow is handled inside its own state (EggPistonsMKII's PinchMode) needs `DONEACTION` to
reach that state; underwater fights need `p_air`. Emulators are shared: one DuckStation at a time; wait on PIDs, never
on `pgrep` patterns that can match the waiter's own command line.
