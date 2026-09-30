# UFO1 (docs/32 7.4): which meshes the 3D time goes to. `make BOOT=3,0`, make disc, then tools/play_test.sh this OUT:
# L MESH lines (LoadMesh's file names in load order = model slots from 0), then per model at frame 300 the faces looked
# at and the draws since frame 60 (g_ps1S3DModelFaces / Draws, in the UFO overlay).
add-symbol-file build/gen/ovl/UFO1/UFO1.elf
break RSDK::LoadMesh
commands
silent
printf "L MESH %s\n", $a0
continue
end
break perfTick if g_ps1PerfFrames == 60 || g_ps1PerfFrames == 300
commands
silent
if g_ps1PerfFrames == 60
  set $i = 0
  while $i < 32
    set var g_ps1S3DModelFaces[$i] = 0
    set var g_ps1S3DModelDraws[$i] = 0
    set $i = $i + 1
  end
else
  set $i = 0
  while $i < 32
    if g_ps1S3DModelDraws[$i]
      printf "L MODEL %d faces=%u draws=%u perFrame=%u\n", $i, g_ps1S3DModelFaces[$i], g_ps1S3DModelDraws[$i], g_ps1S3DModelFaces[$i] / 240
    end
    set $i = $i + 1
  end
  quit
end
continue
end
continue
