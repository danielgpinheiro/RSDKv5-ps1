# docs/perf/80 (`make CHAINCRC=1`, ps1/chain_crc.cpp): every 256 frames the ring of per-frame GPU-word hashes and word
# counts. "CRCF f" = the ring's last frame; then x/256xw of g_ps1ChainCrc and of g_ps1ChainCrcWords (frames f-255 .. f).
# Appended to a run's GDB script (tools/play/chain_crc.sh): boss recipes through EXTRA=, sweeps and boots directly.
break ps1ChainCrcFull
commands
silent
printf "CRCF %u\n", g_ps1PerfFrames
x/256xw &g_ps1ChainCrc
printf "CRCW\n"
x/256xw &g_ps1ChainCrcWords
printf "CRCE\n"
continue
end
