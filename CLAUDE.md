@AGENTS.md

Claude Code: the workspace-level `../CLAUDE.md` and skills in `../.claude/skills/` apply
(open Claude from `PS1/` to get them; the `rsdkv5-ps1-port` skill covers this repo).
- Fully autonomous since 2026-10-09: plan → implement → verify with evidence → commit + push `fork ps1`; log design
  choices in `../docs/32`'s choices table; publishing the builder needs the user's OK. Focus: stock PS1, branch `ps1`.
- Target (user 2026-10-19, `../docs/32` phase 9t): 60 fps at one logic step in every act and the UFO stages, no
  frameskip; judge every speed number against 262 hblanks a frame at one step (Uncapped FPS = No and Frameskip are
  test aids).
- Emulators: the stock PCSX-Redux app (`EMU=redux`) by default; DuckStation (`EMU=ds`) whenever no other session runs
  it. `pgrep` first, never a second instance, kill only your own PIDs, muted; launch through the harness (bash scripts).
- GDB through `tools/gdbw` (gdb 18 relay). Runs in the background with a timeout (~60 s, `TIMEOUT=` for long runs).
