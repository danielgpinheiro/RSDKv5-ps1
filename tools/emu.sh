# Sourced by the GDB harness (tools/play_test.sh, tools/boot_test.sh): starts an emulator on build/game.cue for
# rsdkv5.elf and stops it again.
#   EMU=redux (default)  PCSX-Redux, GDB port 3333, the EXE given with -exe (the disc's overlays and data)
#   EMU=ds               DuckStation with the retail BIOS, GDB port 2345 (docs/31 §6), booted from the disc
#   EMU=r16              docs/32 phase 13: the local 16 MB PCSX-Redux copy (../pcsx-redux-16mb/redux16mb, docs/34 §11 / §20)
#                        on its own portable settings (../pcsx-redux-maniamods) and GDB port 3344, OpenBIOS, the EXE with
#                        -exe: EMU_RAM=2mb (default) / 8mb / 16mb, EMU_VRAM=2mb for the 2 MB VRAM mod
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
EMU_RAM=${EMU_RAM:-}   # 8mb: run any EXE with 8 MB (the PC sampler's counters live above 2 MB; phase 13's modded consoles)
EMU_VRAM=${EMU_VRAM:-} # 2mb: EMU=r16 with the 2 MB VRAM mod
EMU_R16=$HOME/Files/Projetos_Infaliveis/PS1/pcsx-redux-16mb/redux16mb
EMU_R16_DIR=$HOME/Files/Projetos_Infaliveis/PS1/pcsx-redux-maniamods
EMU_R16_PORT=3344
EMU_SP=$(python3 -c "import struct;print(hex(struct.unpack_from('<I',open('rsdkv5.ps-exe','rb').read(0x34),0x30)[0]))")
EMU_PID=
EMU_CFG=
EMU_BAK=

# The GDB script's first lines (symbols, then the emulator's stub)
emu_gdb_header() {
  echo "set pagination off"; echo "set confirm off"; echo "file rsdkv5.elf"; echo "set architecture mips:3000"
  if [ "$EMU" = ds ]; then echo "target remote localhost:2345"
  elif [ "$EMU" = r16 ]; then echo "target remote localhost:$EMU_R16_PORT"
  else echo "target remote localhost:3333"; fi
}

emu_restore() { [ -n "$EMU_BAK" ] && [ -f "$EMU_BAK" ] && cp "$EMU_BAK" "$EMU_CFG"; }

# emu_start BACKUP_PREFIX: exits 2 when the emulator already runs (another session)
emu_start() {
  if [ "$EMU" = ds ]; then
    # the executable by name (-x): -f matched other sessions' shells polling with that path in their own command line
    pgrep -x DuckStation >/dev/null && { echo "DuckStation is already running (another session?): not starting it" >&2; exit 2; }
    EMU_CFG=$EMU_DS_SET; EMU_BAK="$1.settings.bak"; cp "$EMU_CFG" "$EMU_BAK"; trap emu_restore EXIT
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
    if [ "$EMU_SP" != "0x801fff00" ] || [ "$EMU_RAM" = 8mb ]; then
      sed -i '' 's/^Enable8MBRAM = false/Enable8MBRAM = true/' "$EMU_CFG"
      echo "8 MB RAM (SP $EMU_SP, EMU_RAM=$EMU_RAM): DuckStation Enable8MBRAM on for this run"
    fi
    # EMU_DS_FASTBOOT=0: through the BIOS's boot screens, as a player starts it (docs/32 9f)
    # (started paused: the BIOS boot is over before the GDB stub is up; GDB's first continue starts it)
    local fb=-fastboot
    [ "${EMU_DS_FASTBOOT:-1}" = 0 ] && fb= && sed -i '' 's/^StartPaused = false/StartPaused = true/' "$EMU_CFG"
    "$EMU_DS" -batch $fb -nofullscreen -- "$PWD/build/game.cue" >/dev/null 2>&1 &
    EMU_PID=$!
    # its GDB stub listens once the app is up: slower while another emulator runs (a fixed 6 s missed it, 9b.2)
    sleep 4
    for _ in $(seq 1 26); do nc -z localhost 2345 2>/dev/null && break; sleep 1; done
  elif [ "$EMU" = r16 ]; then
    # phase 13: our own portable instance (settings, cards and GDB port apart from every other session's emulator)
    nc -z localhost $EMU_R16_PORT 2>/dev/null && { echo "port $EMU_R16_PORT busy: our redux16mb already runs?" >&2; exit 2; }
    mkdir -p $EMU_R16_DIR
    [ -f $EMU_R16_DIR/pcsx.json ] || echo '{}' > $EMU_R16_DIR/pcsx.json
    # muted; the debugger's first-chance breaks without DBE (bit 7): phase 13's 16 MB probe takes a bus error on purpose
    # (ps1/hwdetect.cpp catches it; the stock config would stop GDB there)
    /usr/bin/python3 -c "import json,sys;p=sys.argv[1];d=json.load(open(p));d.setdefault('SPU',{})['Mute']=True;e=d.setdefault('emulator',{}).setdefault('Debug',{});e['FirstChanceException']=e.get('FirstChanceException',7408)&~0x80;json.dump(d,open(p,'w'),indent=4)" $EMU_R16_DIR/pcsx.json
    local mc1=${EMU_MC1:-$1.mc1.mcd} mc2=$1.mc2.mcd
    [ -f "$mc1" ] || /usr/bin/python3 tools/memcard/mcd.py format "$mc1" >/dev/null
    /usr/bin/python3 tools/memcard/mcd.py format "$mc2" >/dev/null
    local mem=-2mb
    case "$EMU_RAM" in 8mb) mem=-8mb;; 16mb) mem=-16mb;; esac
    [ "$EMU_SP" != "0x801fff00" ] && [ "$mem" = -2mb ] && mem=-8mb
    [ "$EMU_VRAM" = 2mb ] && mem="$mem -2mbvram"
    echo "redux16mb $mem (port $EMU_R16_PORT)"
    $EMU_R16 -portable $EMU_R16_DIR -bios $HOME/Files/Projetos_Infaliveis/PS1/pcsx-redux-16mb/openbios.bin -fastboot -exe rsdkv5.ps-exe -interpreter \
      -debugger -gdb -gdb-port $EMU_R16_PORT $mem -memcard1 "$mc1" -memcard2 "$mc2" -iso build/game.cue >/dev/null 2>&1 &
    EMU_PID=$!
    sleep 4
    for _ in $(seq 1 20); do nc -z localhost $EMU_R16_PORT 2>/dev/null && break; sleep 1; done
  else
    # 4.2e: other sessions run isolated copies (-portable config, their own GDB port): only an instance on the default
    # config (this harness's pcsx.json and GDB port 3333) blocks this run
    # (the processes named PCSX-Redux only: a grep over every command line matched shells waiting on that path)
    for p in $(pgrep -x PCSX-Redux); do ps -o command= -p $p; done | grep -qv -- "-portable" && { echo "PCSX-Redux is already running on the default config (another session?): not starting it" >&2; exit 2; }
    EMU_CFG=$EMU_PCSX_CFG; EMU_BAK="$1.pcsx.json.bak"; cp "$EMU_CFG" "$EMU_BAK"; trap emu_restore EXIT
    # (user 2026-10-09 / 10-11: test runs are silent: SPU.Mute for this run; the settings come back after it)
    /usr/bin/python3 - "$EMU_CFG" <<'PYEOF'
import json, sys
p = sys.argv[1]
d = json.load(open(p))
d.setdefault('SPU', {})['Mute'] = True
json.dump(d, open(p, 'w'), indent=4)
PYEOF
    local ram=-2mb
    if [ "$EMU_SP" != "0x801fff00" ]; then ram=-8mb; echo "8 MB debug EXE (SP $EMU_SP): PCSX-Redux -8mb for this run"; fi
    [ "$EMU_RAM" = 8mb ] && ram=-8mb
    # phase 8: the game writes the memory card: every run gets its own formatted card images (never the shared
    # memcard1/2.mcd of the default config); EMU_MC1=FILE keeps a card across runs (made if missing)
    local mc1=${EMU_MC1:-$1.mc1.mcd} mc2=$1.mc2.mcd
    [ -f "$mc1" ] || /usr/bin/python3 tools/memcard/mcd.py format "$mc1" >/dev/null
    /usr/bin/python3 tools/memcard/mcd.py format "$mc2" >/dev/null
    # docs/32 9d: EMU_RETAIL=1 boots the disc through the user's retail BIOS with its intro (the Sony / PlayStation
    # screens stay in VRAM unless the game clears it), not the EXE straight into OpenBIOS
    local boot="-fastboot -exe rsdkv5.ps-exe"
    [ "${EMU_RETAIL:-0}" = 1 ] && boot="-bios ${EMU_BIOS:-$HOME/Files/Projetos_Infaliveis/PS1/SCPH101.BIN}"
    "$EMU_PCSX" $boot -interpreter -debugger -gdb -gdb-port 3333 $ram -memcard1 "$mc1" -memcard2 "$mc2" \
      -iso build/game.cue >/dev/null 2>&1 &
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
  fi
  emu_restore
}
