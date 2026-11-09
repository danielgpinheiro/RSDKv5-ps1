# docs/ports/mania/63 M5: the Super theme streamed into the SPU (ps1/music_stream.cpp), not the XA track.
# Same scripted transform as super/transform.gdb (shot.sh PRE file, frames 290,300,313,340,...): 50 rings and the
# emeralds at the first stop, jump held at 300-304, Y (Plus: the Super button = Triangle) in the air at 310-313.
# Everything printed is either an emulator-visible SPU voice register or a counter the stream itself maintains:
#   STREAM  starts/halves/underruns/loops/bytes and the voice's address inside the ring
#   VOICE   voice 17's pitch, envelope (ENVX), current and start addresses - the DSP's own state, not ours
# A playback claim needs both plus the dumped sector compared with Data/Music/Super.adp (tools/play/super/check_stream.py)
define ps1_onstop
  set $p = (struct EntityPlayer *)RSDK::objectEntityPtr[0]
  if !$super_set
    set var $p->rings = 200
    set var ((SaveRAM *)SaveGame->saveRAM)->collectedEmeralds = 0x7F
    set $super_set = 1
  end
  if g_ps1PerfFrames == 2600
    set var $p->rings = 200
  end
  if g_ps1PerfFrames == 300
    set var g_ps1PadTestMask = 0x10
  end
  if g_ps1PerfFrames == 310
    set var g_ps1PadTestMask = 0x100
  end
  if g_ps1PerfFrames == 304 || g_ps1PerfFrames == 313
    set var g_ps1PadTestMask = 0
  end
  set $vb = 0x1F801C00 + 17 * 16
  printf "L STREAM f=%u starts=%u halves=%u und=%u loops=%u bytes=%u pos=%u ring=%u sec=%u sbuf=%u\n", g_ps1PerfFrames, g_ps1MusicStreamStarts, g_ps1MusicStreamHalves, g_ps1MusicStreamUnderruns, g_ps1MusicStreamLoops, g_ps1MusicStreamBytes, g_ps1MusicStreamLastPos, g_ps1MusicStreamRing, g_ps1MusicStreamSrcSector, g_ps1MusicStreamSector
  printf "L VOICE f=%u pitch=%u env=%u cur=%u start=%u volL=%u volR=%u\n", g_ps1PerfFrames, *(unsigned short *)($vb + 4), *(unsigned short *)($vb + 10), *(unsigned short *)($vb + 12), *(unsigned short *)($vb + 14), *(unsigned short *)($vb), *(unsigned short *)($vb + 2)
  printf "L CD f=%u int1=%u restarts=%u discarded=%u xa=%d track=%d ready=%u req=%u refused=%u bank=%u fail=%u phase=%u xarun=%u blocker=%u cdsectors=%u reserved=%u\n", g_ps1PerfFrames, g_ps1StreamInt1, g_ps1StreamRestarts, g_ps1StreamDiscarded, g_ps1XaChannel, g_ps1XaTrack, g_ps1MusicStreamReady, g_ps1MusicStreamRequests, g_ps1MusicStreamRefused, g_ps1SfxBankBytes, g_ps1MusicStreamFail, g_ps1MusicStreamPhase, g_ps1XaRunningFlag, g_ps1MusicStreamBlocker, g_ps1MusicStreamRingSectors, g_ps1MusicStreamReserved
  printf "L XA f=%u starts=%u stops=%u\n", g_ps1PerfFrames, g_ps1XaStarts, g_ps1XaForcedStops
  # NOT $sp: that name is the MIPS stack pointer register (a write to it zeroed the guest's stack and crashed the
  # frame's return; 2026-11-09)
  set $sbuf = (char *)g_ps1MusicStreamSector
  if $sbuf != 0
    dump binary memory /tmp/m5-sector.bin $sbuf $sbuf + 2048
  end
end
