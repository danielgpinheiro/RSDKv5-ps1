# docs/32 9f (UFO item 3c): RSDK/Graphics/PS1/PS1UfoFloor.cpp's real-tile quads in assembly: one tile row's drawable
# tiles (gathered by the C pass: column, tile, image) into 10-word GPU textured quads in the open chain bulk, every value
# of the loop in a register (GCC spilled a dozen of them to the stack per quad). The same decisions and words as the C:
#   return 0: all tiles done; 1: the bulk is full or not open (w == wend: C closes / opens one, then calls again);
#   2: the tile's CLUT copy for its bank is not set up this frame (C closes the bulk, PS1FloorClutFill, calls again).
# PS1FloorAsmCtx (PS1UfoFloor.cpp), byte offsets:
#   0 j (in / out), 4 nn, 8 col (u8), 12 tiles (u16), 16 imgs (u16), 20 tXY (u32), 24 bXY (u32), 28 tZ (u16), 32 bZ (u16),
#   36 check (corners flagged: test each tile's), 40 fd0, 44 fStep, 48 images (PS1TileImage, 6 bytes), 52 imgClut (u8),
#   56 attr (u16 [k][8]), 60 w (in / out), 64 wend
        .set noreorder
        .set noat
        .text
        .align 2
        .globl PS1FloorQuadsAsm
        .type PS1FloorQuadsAsm, @function
PS1FloorQuadsAsm:
        addiu   $sp, $sp, -40
        sw      $s0, 0($sp)
        sw      $s1, 4($sp)
        sw      $s2, 8($sp)
        sw      $s3, 12($sp)
        sw      $s4, 16($sp)
        sw      $s5, 20($sp)
        sw      $s6, 24($sp)
        sw      $s7, 28($sp)
        sw      $fp, 32($sp)
        lw      $s0, 0($a0)             # j
        lw      $s1, 4($a0)             # nn
        lw      $s2, 8($a0)             # col
        lw      $s3, 12($a0)            # tiles
        lw      $s4, 16($a0)            # imgs
        lw      $s5, 20($a0)            # tXY
        lw      $s6, 24($a0)            # bXY
        lw      $s7, 48($a0)            # images
        lw      $fp, 52($a0)            # imgClut
        lw      $t8, 56($a0)            # attr
        lw      $t9, 60($a0)            # w
        lw      $v1, 64($a0)            # wend
        lw      $a1, 40($a0)            # fd0
        lw      $a2, 44($a0)            # fStep
        lw      $a3, 36($a0)            # check
        lui     $v0, 0x2C80
        ori     $v0, $v0, 0x8080        # GP0 2Ch: textured quad, opaque, colour 808080

.Lq:
        beq     $s0, $s1, .Ldone
        addu    $t0, $s2, $s0           # (delay)
        lbu     $t0, 0($t0)             # i: the tile's column
        sll     $t1, $s0, 1
        addu    $t2, $s3, $t1
        lhu     $t2, 0($t2)             # t: the tile (flip bits 0x400 / 0x800)
        addu    $t3, $s4, $t1
        bnez    $a3, .Lcheck
        lhu     $t3, 0($t3)             # (delay) img
.Lchecked:
        mult    $t0, $a2                # i * fStep (i < 96 as rs: the fast multiply)
        sll     $t4, $t3, 2
        sll     $t5, $t3, 1
        addu    $t4, $t4, $t5
        addu    $t4, $s7, $t4           # &images[img] (6-byte records)
        addu    $t5, $fp, $t3
        lbu     $t5, 0($t5)             # k = imgClut[img]
        mflo    $t6
        addu    $t6, $a1, $t6           # fd = fd0 + fStep * i
        sra     $t6, $t6, 19            # bank, clamped to 0..7
        bgez    $t6, 1f
        nop
        move    $t6, $zero
1:      slti    $at, $t6, 8
        bnez    $at, 2f
        nop
        li      $t6, 7
2:      sll     $t5, $t5, 4             # attr[k][bank] (8 u16 a row)
        sll     $t7, $t6, 1
        addu    $t5, $t5, $t7
        addu    $t5, $t8, $t5
        lhu     $t5, 0($t5)             # clut
        nop
        beqz    $t5, .Lclut
        lhu     $t6, 0($t4)             # (delay) uv = u | v << 8
        lhu     $t7, 2($t4)             # tpage
        addiu   $t1, $t6, 15            # tr
        addiu   $t4, $t6, 0xF00         # bl
        addiu   $t3, $t6, 0xF0F         # br (tl = uv)
        andi    $at, $t2, 0x400
        beqz    $at, 3f
        andi    $at, $t2, 0x800         # (delay) V-flip
        xor     $t6, $t6, $t1           # H-flip: tl <-> tr, bl <-> br
        xor     $t1, $t1, $t6
        xor     $t6, $t6, $t1
        xor     $t4, $t4, $t3
        xor     $t3, $t3, $t4
        xor     $t4, $t4, $t3
3:      beqz    $at, 4f
        nop
        xor     $t6, $t6, $t4           # V-flip: tl <-> bl, tr <-> br
        xor     $t4, $t4, $t6
        xor     $t6, $t6, $t4
        xor     $t1, $t1, $t3
        xor     $t3, $t3, $t1
        xor     $t1, $t1, $t3
4:      beq     $t9, $v1, .Lfull
        sll     $t2, $t0, 2             # (delay) i * 4
        addu    $at, $s5, $t2
        lw      $t2, 0($at)             # tXY[i]
        lw      $at, 4($at)             # tXY[i + 1]
        sw      $v0, 0($t9)
        sw      $t2, 4($t9)
        sll     $t5, $t5, 16
        or      $t5, $t5, $t6
        sw      $t5, 8($t9)             # clut << 16 | tl
        sw      $at, 12($t9)
        sll     $t7, $t7, 16
        or      $t7, $t7, $t1
        sw      $t7, 16($t9)            # tpage << 16 | tr
        sll     $t2, $t0, 2
        addu    $t2, $s6, $t2
        lw      $t5, 0($t2)             # bXY[i]
        lw      $t2, 4($t2)             # bXY[i + 1]
        sw      $t4, 24($t9)            # bl
        sw      $t5, 20($t9)
        sw      $t2, 28($t9)
        sw      $t3, 32($t9)            # br
        addiu   $t9, $t9, 40
        b       .Lq
        addiu   $s0, $s0, 1             # (delay)

.Lcheck:                                # corners flagged: a tile with a flagged corner (depth 0) is not drawn
        lw      $t4, 28($a0)            # tZ
        lw      $t5, 32($a0)            # bZ
        sll     $t6, $t0, 1
        addu    $t4, $t4, $t6
        addu    $t5, $t5, $t6
        lhu     $t6, 0($t4)
        lhu     $t7, 2($t4)
        lhu     $t1, 0($t5)
        lhu     $t4, 2($t5)
        nop
        beqz    $t6, .Lskip
        nop
        beqz    $t7, .Lskip
        nop
        beqz    $t1, .Lskip
        nop
        beqz    $t4, .Lskip
        nop
        b       .Lchecked
        nop
.Lskip:
        b       .Lq
        addiu   $s0, $s0, 1             # (delay)

.Lclut:
        b       .Lout
        li      $v0, 2                  # (delay)
.Lfull:
        b       .Lout
        li      $v0, 1                  # (delay)
.Ldone:
        li      $v0, 0
.Lout:
        sw      $s0, 0($a0)
        sw      $t9, 60($a0)
        lw      $s0, 0($sp)
        lw      $s1, 4($sp)
        lw      $s2, 8($sp)
        lw      $s3, 12($sp)
        lw      $s4, 16($sp)
        lw      $s5, 20($sp)
        lw      $s6, 24($sp)
        lw      $s7, 28($sp)
        lw      $fp, 32($sp)
        nop
        jr      $ra
        addiu   $sp, $sp, 40            # (delay)
        .size PS1FloorQuadsAsm, .-PS1FloorQuadsAsm
