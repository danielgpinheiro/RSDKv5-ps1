# The debug overlay's heap count against the allocator itself (docs/ports/mania/32 "Debug overlay"): at frames 600 and 1200
# of a `make DEBUG_OVERLAY=1 BOOT=c,s` build, walk nugget's free list (common/libc/alloc.c s_heap_metadata: head, then
# each empty block's next / size, ending at its marker of size 0) and compare its free bytes with the overlay's
# (heap span - g_ps1DbgHeapLive). tools/play_test.sh tools/play/overlay_heap.gdb OUT; H lines.
define ps1_heap
  set $meta = (unsigned *)&s_heap_metadata
  set $blk = (unsigned *)$meta[0]
  set $free = 0
  set $big = 0
  set $n = 0
  while $blk != 0 && $blk[1] != 0 && $n < 4000
    set $free = $free + $blk[1]
    if $blk[1] > $big
      set $big = $blk[1]
    end
    set $n = $n + 1
    set $blk = (unsigned *)$blk[0]
  end
  set $start = ((unsigned)&__heap_start + 7) & ~7
  set $span = ((unsigned)&__stack_start - $start - 8 + 7) & ~7
  printf "H f=%u freelist=%u blocks=%u largest=%u overlay_free=%u diff=%d live=%u\n", *(unsigned *)&::g_ps1PerfFrames, $free, $n, $big, $span - *(unsigned *)&::g_ps1DbgHeapLive, (int)($span - *(unsigned *)&::g_ps1DbgHeapLive) - (int)$free, *(unsigned *)&::g_ps1DbgHeapLive
end
tbreak main
commands
silent
set var g_ps1TestStops[0] = 600
set var g_ps1TestStops[1] = 1200
continue
end
break ps1TestStop
commands
silent
ps1_heap
if *(unsigned *)&::g_ps1PerfFrames >= 1200
  quit
end
continue
end
continue
