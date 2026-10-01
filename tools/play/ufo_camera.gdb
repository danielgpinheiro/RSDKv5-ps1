# UFO stage camera + palettes at frame 299 (docs/32 7.5): for tools/gfx/ufo_floor_reference.py (the PC's per-line
# playfield render to compare with the framebuffer). Replace OVL / OUT before running (sed); pairs with a VRAM dump.
add-symbol-file build/gen/ovl/OVL/OVL.elf
break perfTick if g_ps1PerfFrames == 299
commands
silent
set $c = (struct EntityUFO_Camera *)RSDK::objectEntityPtr[10]
printf "L CAMERA angle=%d angleX=%d x=%d y=%d height=%d clipY=%d\n", $c->angle, $c->angleX, $c->position.x, $c->position.y, $c->height, $c->clipY
dump binary memory OUT_pal.bin RSDK::fullPalette ((char*)RSDK::fullPalette)+4096
continue
end
