# Sourced by the GDB harness (tools/play_test.sh, tools/boot_test.sh): starts an emulator on build/game.cue for
# rsdkv5.elf and stops it again.
#   EMU=redux (default)  PCSX-Redux, GDB port 3333, the EXE given with -exe (the disc's overlays and data)
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
EMU_RAM=${EMU_RAM:-}   # 8mb: run any EXE with 8 MB (the PC sampler's counters live above 2 MB)
EMU_SP=$(python3 -c "import struct;print(hex(struct.unpack_from('<I',open('rsdkv5.ps-exe','rb').read(0x34),0x30)[0]))")
EMU_PID=
EMU_CFG=
EMU_BAK=

# The GDB script's first lines (symbols, then the emulator's stub)
emu_gdb_header() {
  echo "set pagination off"; echo "set confirm off"; echo "file rsdkv5.elf"; echo "set architecture mips:3000"
  if [ "$EMU" = ds ]; then echo "target remote localhost:2345"; else echo "target remote localhost:3333"; fi
}

emu_restore() { [ -n "$EMU_BAK" ] && [ -f "$EMU_BAK" ] && cp "$EMU_BAK" "$EMU_CFG"; }

# emu_start BACKUP_PREFIX: exits 2 when the emulator already runs (another session)
emu_start() {
  if [ "$EMU" = ds ]; then
    pgrep -f "DuckStation.app/Contents/MacOS" >/dev/null && { echo "DuckStation is already running (another session?): not starting it" >&2; exit 2; }
    EMU_CFG=$EMU_DS_SET; EMU_BAK="$1.settings.bak"; cp "$EMU_CFG" "$EMU_BAK"; trap emu_restore EXIT
    if [ "$EMU_SP" != "0x801fff00" ]; then
      sed -i '' 's/^Enable8MBRAM = false/Enable8MBRAM = true/' "$EMU_CFG"
      echo "8 MB debug EXE (SP $EMU_SP): DuckStation Enable8MBRAM on for this run"
    fi
    "$EMU_DS" -batch -fastboot -nofullscreen -- "$PWD/build/game.cue" >/dev/null 2>&1 &
    EMU_PID=$!
    sleep 6
  else
    # 4.2e: other sessions run isolated copies (-portable config, their own GDB port): only an instance on the default
    # config (this harness's pcsx.json and GDB port 3333) blocks this run
    ps -Ao command | grep "[P]CSX-Redux.app/Contents/MacOS/PCSX-Redux" | grep -qv -- "-portable" && { echo "PCSX-Redux is already running on the default config (another session?): not starting it" >&2; exit 2; }
    EMU_CFG=$EMU_PCSX_CFG; EMU_BAK="$1.pcsx.json.bak"; cp "$EMU_CFG" "$EMU_BAK"; trap emu_restore EXIT
    local ram=-2mb
    if [ "$EMU_SP" != "0x801fff00" ]; then ram=-8mb; echo "8 MB debug EXE (SP $EMU_SP): PCSX-Redux -8mb for this run"; fi
    [ "$EMU_RAM" = 8mb ] && ram=-8mb
    "$EMU_PCSX" -fastboot -interpreter -debugger -gdb -gdb-port 3333 $ram -iso build/game.cue -exe rsdkv5.ps-exe \
      >/dev/null 2>&1 &
    EMU_PID=$!
    sleep 4
  fi
}

emu_stop() { [ -n "$EMU_PID" ] && { kill $EMU_PID 2>/dev/null; wait $EMU_PID 2>/dev/null; }; emu_restore; }
