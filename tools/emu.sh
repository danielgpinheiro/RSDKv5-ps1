# Sourced by the GDB harness (tools/play_test.sh, tools/boot_test.sh): starts an emulator on build/game.cue for
# rsdkv5.elf and stops it again.
#   EMU=redux (default)  PCSX-Redux, GDB port 3333, the EXE given with -exe (the disc's overlays and data)
#                        EMU_SLOT=N (1-4): our own portable instance instead (../../emu/pcsx-redux-maniaN, a copy of the default
#                        settings made once; GDB port 3400+N, the suite's workers use 3350-3397), so parallel sessions in
#                        their own worktrees each run one; a slot whose port is taken waits up to 10 minutes
#   EMU=ds               DuckStation with the retail BIOS, GDB port 2345 (docs/31 §6), booted from the disc
# Both emulators are shared with other sessions (2026-09-28, user: this port uses PCSX-Redux; DuckStation belongs to
# another session). emu_start refuses to start an emulator that already runs; emu_stop kills only our own PID; the
# settings a run changes are backed up and restored (pcsx.json / settings.ini).
# An 8 MB measuring link (header SP 0x807FFF00, make MEASURE_RAM=8M) runs with 8 MB RAM: PCSX-Redux -8mb (it saves the
# setting in pcsx.json, hence the backup) or DuckStation's Enable8MBRAM; any other EXE runs with -2mb.
EMU=${EMU:-redux}
EMU_PCSX=/Applications/PCSX-Redux.app/Contents/MacOS/PCSX-Redux
EMU_PCSX_CFG="$HOME/.config/pcsx-redux/pcsx.json"
EMU_DS=/Applications/DuckStation.app/Contents/MacOS/DuckStation
EMU_DS_SET="$HOME/Library/Application Support/DuckStation/settings.ini"
EMU_RAM=${EMU_RAM:-}   # 8mb: run any EXE with 8 MB (the PC sampler's counters live above 2 MB; a measuring link only)
                       # 2mb: any EXE with 2 MB (an 8 MB link included: the DEVMENU build's 2 MB guard)
EMU_SLOT=${EMU_SLOT:-}
# the workspace root (2026-10-26 layout): the first parent with tools/workspace_links.txt (ports/<repo>,
# work/worktrees/<name> and a run's build/boss/snaps/<name> copy alike)
EMU_WS=$PWD
while [ "$EMU_WS" != / ] && [ ! -f "$EMU_WS/tools/workspace_links.txt" ]; do EMU_WS=$(dirname "$EMU_WS"); done
EMU_SLOT_DIR=$EMU_WS/emu/pcsx-redux-mania$EMU_SLOT
EMU_SLOT_PORT=$((3400 + ${EMU_SLOT:-0}))
EMU_SP=$(python3 -c "import struct;print(hex(struct.unpack_from('<I',open('rsdkv5.ps-exe','rb').read(0x34),0x30)[0]))")
EMU_PID=
EMU_CFG=
EMU_BAK=
# The user's settings.ini while a harness run holds DuckStation (every harness that edits it, 2026-10-23: a run killed
# before its restore left a throwaway memory card and StartPaused in the user's settings for days). A run that finds it
# at start puts it back first (no DuckStation runs then: its holder was killed); the restore removes it.
EMU_DS_HELD="$EMU_DS_SET.harness"

# The GDB script's first lines (symbols, then the emulator's stub)
emu_gdb_header() {
  echo "set pagination off"; echo "set confirm off"; echo "file rsdkv5.elf"; echo "set architecture mips:3000"
  if [ "$EMU" = ds ]; then echo "target remote localhost:2345"
  elif [ -n "$EMU_SLOT" ]; then echo "target remote localhost:$EMU_SLOT_PORT"
  else echo "target remote localhost:3333"; fi
}

emu_restore() {
  [ -n "$EMU_BAK" ] || return 0
  if [ "$EMU" = ds ] && [ -f "$EMU_DS_HELD" ]; then cp "$EMU_DS_HELD" "$EMU_CFG" && rm -f "$EMU_DS_HELD"
  elif [ -f "$EMU_BAK" ]; then cp "$EMU_BAK" "$EMU_CFG"; fi
  EMU_BAK=
}

# emu_start BACKUP_PREFIX: exits 2 when the emulator already runs (another session)
emu_start() {
  if [ "$EMU" = ds ]; then
    # the executable by name (-x): -f matched other sessions' shells polling with that path in their own command line
    pgrep -x DuckStation >/dev/null && { echo "DuckStation is already running (another session?): not starting it" >&2; exit 2; }
    if [ -f "$EMU_DS_HELD" ]; then
      echo "DuckStation settings: the user's put back from $EMU_DS_HELD (a killed run left them)" >&2
      cp "$EMU_DS_HELD" "$EMU_DS_SET"
    fi
    cp "$EMU_DS_SET" "$EMU_DS_HELD"
    # the exit trap stops our emulator before restoring: DuckStation writes its settings (the run's) when it quits
    EMU_CFG=$EMU_DS_SET; EMU_BAK="$1.settings.bak"; cp "$EMU_CFG" "$EMU_BAK"; trap emu_stop EXIT
    # docs/32 9f: never the user's own card (memcards/<title>_1.mcd): slot 1 is a non-persistent card (formatted fresh
    # at every boot), or EMU_DS_CARD=FILE (absolute path; a missing file is created formatted), for this run only
    /usr/bin/python3 - "$EMU_CFG" "${EMU_DS_CARD:-}" <<'PYEOF'
import re, sys
p, card = sys.argv[1], sys.argv[2]
s = open(p).read()
m = re.search(r'^\[MemoryCards\]\n((?:(?!\[).*\n)*)', s, re.M)
body = [l for l in m.group(1).splitlines() if not l.startswith(('Card1Type', 'Card1Path'))]
body = (['Card1Type = Shared', 'Card1Path = ' + card] if card else ['Card1Type = NonPersistent']) + body
s = s[:m.start(1)] + '\n'.join(body) + '\n' + s[m.end(1):]
open(p, 'w').write(s)
PYEOF
    # (user 2026-10-09: test runs are silent; the settings come back after the run)
    if grep -q '^OutputMuted = ' "$EMU_CFG"; then sed -i '' 's/^OutputMuted = .*/OutputMuted = true/' "$EMU_CFG"
    else sed -i '' 's/^\[Audio\]$/[Audio]\
OutputMuted = true/' "$EMU_CFG"; fi
    if { [ "$EMU_SP" != "0x801fff00" ] || [ "$EMU_RAM" = 8mb ]; } && [ "$EMU_RAM" != 2mb ]; then
      sed -i '' 's/^Enable8MBRAM = false/Enable8MBRAM = true/' "$EMU_CFG"
      echo "8 MB RAM (SP $EMU_SP, EMU_RAM=$EMU_RAM): DuckStation Enable8MBRAM on for this run"
    fi
    # EMU_DS_FASTBOOT=0: through the BIOS's boot screens, as a player starts it (docs/32 9f)
    # (started paused: the BIOS boot is over before the GDB stub is up; GDB's first continue starts it)
    local fb=-fastboot
    [ "${EMU_DS_FASTBOOT:-1}" = 0 ] && fb= && sed -i '' 's/^StartPaused = false/StartPaused = true/' "$EMU_CFG"
    # (user 2026-10-22: runs must not take the focus from the user's own full-screen apps) DuckStation has no headless
    # mode: open -g starts it in the background (not activated); its PID is the DuckStation whose command line has our
    # disc. EMU_UI=1: started as before (in front)
    if [ "${EMU_UI:-0}" = 1 ]; then
      "$EMU_DS" -batch $fb -nofullscreen -- "$PWD/build/game.cue" >/dev/null 2>&1 &
      EMU_PID=$!
    else
      open -g -n -a "${EMU_DS%/Contents/MacOS/*}" --args -batch $fb -nofullscreen -- "$PWD/build/game.cue"
      EMU_PID=
      for _ in $(seq 1 20); do
        for p in $(pgrep -x DuckStation); do ps -o command= -p $p | grep -qF -- "$PWD/build/game.cue" && EMU_PID=$p; done
        [ -n "$EMU_PID" ] && break; sleep 0.5
      done
    fi
    # its GDB stub listens once the app is up: slower while another emulator runs (a fixed 6 s missed it, 9b.2)
    sleep 4
    for _ in $(seq 1 26); do nc -z localhost 2345 2>/dev/null && break; sleep 1; done
  else
    # 4.2e: other sessions run isolated copies (-portable config, their own GDB port): only an instance on the default
    # config (this harness's pcsx.json and GDB port 3333) blocks this run
    # (the processes named PCSX-Redux only: a grep over every command line matched shells waiting on that path)
    local portable= gport=3333
    if [ -n "$EMU_SLOT" ]; then
      case "$EMU_SLOT" in [1-4]) ;; *) echo "EMU_SLOT: 1-4" >&2; exit 1;; esac
      local w
      for w in $(seq 1 120); do nc -z localhost $EMU_SLOT_PORT 2>/dev/null || break; sleep 5; done
      nc -z localhost $EMU_SLOT_PORT 2>/dev/null && { echo "slot $EMU_SLOT: port $EMU_SLOT_PORT still busy" >&2; exit 2; }
      mkdir -p "$EMU_SLOT_DIR"
      [ -f "$EMU_SLOT_DIR/pcsx.json" ] || cp "$EMU_PCSX_CFG" "$EMU_SLOT_DIR/pcsx.json"
      EMU_CFG=$EMU_SLOT_DIR/pcsx.json; trap emu_stop EXIT
      portable="-portable $EMU_SLOT_DIR"; gport=$EMU_SLOT_PORT
    else
      for p in $(pgrep -x PCSX-Redux); do ps -o command= -p $p; done | grep -qv -- "-portable" && { echo "PCSX-Redux is already running on the default config (another session?): not starting it" >&2; exit 2; }
      EMU_CFG=$EMU_PCSX_CFG; EMU_BAK="$1.pcsx.json.bak"; cp "$EMU_CFG" "$EMU_BAK"; trap emu_stop EXIT
    fi
    # (user 2026-10-09 / 10-11: test runs are silent: SPU.Mute for this run; the settings come back after it)
    /usr/bin/python3 - "$EMU_CFG" <<'PYEOF'
import json, sys
p = sys.argv[1]
d = json.load(open(p))
d.setdefault('SPU', {})['Mute'] = True
# 2026-10-22: the pads read the keyboard only (PadType 2), never a host gamepad: with Auto (0) the user's own controller,
# in use in another app, drove pad 1 (a run paused by its Start); with -no-ui no key reaches the pad either
for pad in d.get('pads', []):
    pad['PadType'] = 2
json.dump(d, open(p, 'w'), indent=4)
PYEOF
    local ram=-2mb
    if [ "$EMU_SP" != "0x801fff00" ] && [ "$EMU_RAM" != 2mb ]; then ram=-8mb; echo "8 MB debug EXE (SP $EMU_SP): PCSX-Redux -8mb for this run"; fi
    [ "$EMU_RAM" = 8mb ] && ram=-8mb # (EMU_RAM=2mb: an 8 MB link on a 2 MB console, the DEVMENU build's red screen)
    # phase 8: the game writes the memory card: every run gets its own formatted card images (never the shared
    # memcard1/2.mcd of the default config); EMU_MC1=FILE keeps a card across runs (made if missing)
    local mc1=${EMU_MC1:-$1.mc1.mcd} mc2=$1.mc2.mcd
    [ -f "$mc1" ] || /usr/bin/python3 tools/memcard/mcd.py format "$mc1" >/dev/null
    /usr/bin/python3 tools/memcard/mcd.py format "$mc2" >/dev/null
    # docs/32 9d: EMU_RETAIL=1 boots the disc through the user's retail BIOS with its intro (the Sony / PlayStation
    # screens stay in VRAM unless the game clears it), not the EXE straight into OpenBIOS
    local boot="-fastboot -exe rsdkv5.ps-exe"
    [ "${EMU_RETAIL:-0}" = 1 ] && boot="-bios ${EMU_BIOS:-$EMU_WS/data/bios/SCPH101.BIN}"
    # -NSAppSleepDisabled YES: macOS App Nap throttled a background run to ~20 % CPU once the display slept (2026-10-21)
    # -no-ui (user 2026-10-22: runs must not take the focus from the user's own full-screen apps): no window at all; the
    # emulation, GDB stub and VRAM are the same (timings are emulated). EMU_UI=1: with its window, as before
    local ui=-no-ui
    [ "${EMU_UI:-0}" = 1 ] && ui=
    "$EMU_PCSX" $ui -NSAppSleepDisabled YES $boot -interpreter -debugger -gdb -gdb-port $gport $portable $ram -memcard1 "$mc1" -memcard2 "$mc2" \
      -iso build/game.cue >/dev/null 2>&1 < /dev/null &
    EMU_PID=$!
    sleep 4
  fi
}

# a TERM sent while GDB detaches can be lost (DuckStation idled 25 min in the 9q sweep): TERM again each second, KILL
# after 15 s
emu_stop() {
  if [ -n "$EMU_PID" ]; then
    for i in $(seq 1 15); do kill $EMU_PID 2>/dev/null || break; sleep 1; done
    kill -9 $EMU_PID 2>/dev/null
    wait $EMU_PID 2>/dev/null
    EMU_PID=
  fi
  emu_restore
}
