# docs/32 9u.15: RSDK/Scene/PS1/PS1Layers.cpp's fast band loop (DrawBandIntoT<true>: a whole 16-line tile row on palette
# bank 0, in a scene whose flipped tiles have no copies) in assembly, every value in a register (GCC spilled the command,
# the size word, the row's Y word and the images' base: four stack reloads a piece). The same walk, decisions and words as
# the C loop: the chunk walk (PS1ChunkIdx / PS1ChunkTiles, wrapping at the layer's width), sprite pieces (a texpage word
# where the page changes), flipped tiles as flip rectangles (GPUs that draw them) or as 9-word quads (PS1_EMIT_FLIPPED's
# words, computed from the packed u | v as 9u.9's C), the fragment closing of EmitFrag. Left to the C body (return 1 with
# the tile in `t`, the walk already past it, sx at its column): a tile with no image (9v.8: 8-bit tiles stay here), a
# quad whose edge texel leaves the page (EmitFlipped), a piece that needs a new span. Return 0: the band is done.
# PS1BandAsmCtx (PS1Layers.cpp), byte offsets:
#   0 w, 4 lim, 8 end, 12 page, 16 chunk (u16 *), 20 sub, 24 tx, 28 xsize, 32 chunkRow, 36 rowSel, 40 sx, 44 yWord,
#   48 tileImage (u16 *), 52 images (6-byte records), 56 flipLive, 60 pieces, 64 frects, 68 ps1SegPool, 72 ps1RowPool,
#   76 t (out), 80 flipCopies (9v.10: the scene has a copy of every flipped tile: a flipped tile goes to the C body)
        .set noreorder
        .set noat
        .text
        .align 2
        .globl PS1BandFastAsm
        .type PS1BandFastAsm, @function
PS1BandFastAsm:
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
        lw      $s0, 0($a0)             # w
        lw      $s1, 4($a0)             # lim
        lw      $s2, 8($a0)             # end
        lw      $s3, 12($a0)            # page
        lw      $s4, 16($a0)            # chunk
        lw      $s5, 20($a0)            # sub
        lw      $s6, 24($a0)            # tx
        lw      $s7, 28($a0)            # xsize
        lw      $t8, 32($a0)            # chunkRow
        lw      $t9, 36($a0)            # rowSel
        lw      $fp, 40($a0)            # sx
        lw      $a1, 44($a0)            # yWord
        lw      $a2, 48($a0)            # tileImage
        lw      $a3, 52($a0)            # images
        lw      $t4, 56($a0)            # flipLive
        lw      $v1, 60($a0)            # pieces
        li      $t5, 0xFFFF
        lui     $t6, 0x0010
        ori     $t6, $t6, 0x0010        # the size word: 16 x 16
        lui     $t7, 0x7C80
        ori     $t7, $t7, 0x8080        # GP0 7Ch: a 16 x 16 textured sprite, colour 808080

.Lcol:
        slti    $t0, $fp, 320
        beqz    $t0, .Ldone
        sll     $t0, $s5, 1             # (delay)
        addu    $t0, $s4, $t0
        lhu     $t1, 0($t0)             # t = chunk[sub]
        addiu   $s6, $s6, 1             # ++tx
        bne     $s6, $s7, 1f
        addiu   $s5, $s5, 1             # (delay) ++sub
        move    $s6, $zero              # the layer repeats: tx = 0, sub = 0, the chunk of column 0
        b       .Lnewchunk
        move    $s5, $zero              # (delay)
1:      xori    $t0, $s5, 4
        bnez    $t0, .Lgot
        nop
        move    $s5, $zero              # sub = 0, the next chunk
.Lnewchunk:                             # chunk = ps1RowPool + rowSel[segPool[row[cx >> 3] * 8 + (cx & 7)] * 4] * 4
        srl     $t0, $s6, 2             # cx = tx >> 2
        srl     $t2, $t0, 3
        sll     $t2, $t2, 1
        addu    $t2, $t8, $t2
        lhu     $t2, 0($t2)             # chunkRow[cx >> 3]: the segment
        andi    $t0, $t0, 7
        sll     $t2, $t2, 3
        addu    $t2, $t2, $t0
        lw      $t3, 68($a0)            # ps1SegPool
        sll     $t2, $t2, 1
        addu    $t2, $t3, $t2
        lhu     $t2, 0($t2)             # the chunk index
        lw      $t3, 72($a0)            # ps1RowPool
        sll     $t2, $t2, 3
        addu    $t2, $t9, $t2
        lhu     $t2, 0($t2)             # rowSel[chunk << 2]: the row's id
        nop
        sll     $t2, $t2, 3
        addu    $s4, $t3, $t2           # its 4 tiles
.Lgot:
        beq     $t1, $t5, .Lnext        # empty
        andi    $t2, $t1, 0x3FF         # (delay)
        sll     $t2, $t2, 1
        addu    $t2, $a2, $t2
        lhu     $t2, 0($t2)             # img = tileImage[t & 0x3FF]
        nop
        beq     $t2, $t5, .Lbail        # no image: the C body counts the miss
        sll     $t3, $t2, 1             # (delay)
        sll     $t2, $t2, 2
        addu    $t2, $t2, $t3
        addu    $t2, $a3, $t2           # &images[img]
        lhu     $t3, 4($t2)             # clut
        lhu     $t0, 0($t2)             # u | v << 8
        lhu     $v0, 2($t2)             # tpage
        bne     $t3, $t5, 3f
        nop
        li      $t3, 0x3C00             # 9v.8: an 8-bit tile: the bank's 256-entry CLUT, bank 0 here (C's bankClut, y 240)
3:      sll     $t3, $t3, 16
        or      $t0, $t0, $t3           # tex = u | v << 8 | clut << 16
        andi    $t3, $t1, 0xC00
        bnez    $t3, .Lflip
        addiu   $v1, $v1, 1             # (delay) ++pieces
.Lpiece:                                # PS1_EMIT_PIECE(cmd, yWord | sx, tex, wh, 3, tpage)
        sltu    $t3, $s1, $s0           # w > lim
        bnez    $t3, .Lfrag
        nop
.Lroom:
        beq     $v0, $s3, 2f
        andi    $t3, $fp, 0xFFFF        # (delay)
        lui     $at, 0xE100
        or      $at, $v0, $at
        sw      $at, 0($s0)             # GP0 E1h: the tile's texpage
        addiu   $s0, $s0, 4
        move    $s3, $v0
2:      or      $t3, $t3, $a1
        sw      $t7, 0($s0)
        sw      $t3, 4($s0)
        sw      $t0, 8($s0)
        sw      $t6, 12($s0)            # (the size word lands on the next piece's first word: inside the fragment)
        addiu   $s0, $s0, 12
.Lnext:
        b       .Lcol
        addiu   $fp, $fp, 16            # (delay)

.Lfrag:                                 # EmitFrag: fewer than 15 words left -> a new span (the C body)
        addiu   $t3, $s0, 60
        sltu    $t3, $s2, $t3
        bnez    $t3, .Lbailpiece
        addiu   $t2, $s1, -40           # (delay) the fragment's header = lim - 10
        subu    $t3, $s0, $t2
        srl     $t3, $t3, 2
        addiu   $t3, $t3, -1
        sll     $t3, $t3, 24            # (w - hdr - 1) << 24
        sll     $at, $s0, 8
        srl     $at, $at, 8             # w & 0xFFFFFF
        or      $t3, $t3, $at
        sw      $t3, 0($t2)
        addiu   $s1, $s0, 40            # lim = w + 10
        b       .Lroom
        addiu   $s0, $s0, 4             # (delay) ++w

.Lflip:                                 # t3 = t & 0xC00
        lw      $at, 80($a0)            # 9v.10: flip copies: the C body draws the tile's copy (before the flip rectangle)
        nop
        bnez    $at, .Lbailpiece
        nop
        bnez    $t4, .Llive
        andi    $t2, $t0, 0xFF          # (delay) u
        andi    $t3, $t1, 0x400
        beqz    $t3, 3f
        addiu   $at, $t2, 16            # (delay) uR = u + 16
        addiu   $at, $t2, -1            # H flip: uR = u - 1
3:      sltiu   $at, $at, 256
        beqz    $at, .Lbailpiece        # the edge texel leaves the page: EmitFlipped (the C body)
        srl     $t2, $t0, 8             # (delay)
        andi    $t2, $t2, 0xFF          # v
        andi    $t3, $t1, 0x800
        beqz    $t3, 4f
        addiu   $at, $t2, 16            # (delay) vB = v + 16
        addiu   $at, $t2, -1            # V flip: vB = v - 1
4:      sltiu   $at, $at, 256
        beqz    $at, .Lbailpiece
        addiu   $t3, $s1, -16           # (delay) lim - 4
        sltu    $t3, $t3, $s0           # w > lim - 4
        bnez    $t3, .Lfragq
        nop
.Lqroom:                                # the quad (corners top-left, top-right, bottom-left, bottom-right): t0 tex, v0 tpage
        lui     $at, 0x2C80
        ori     $at, $at, 0x8080
        sw      $at, 0($s0)             # GP0 2Ch
        andi    $t2, $fp, 0xFFFF        # xl
        or      $at, $t2, $a1
        sw      $at, 4($s0)             # yt | xl
        addiu   $t3, $fp, 16
        andi    $t3, $t3, 0xFFFF        # xr
        or      $at, $t3, $a1
        sw      $at, 12($s0)            # yt | xr
        lui     $at, 0x0010
        addu    $at, $at, $a1           # yb = yWord + (16 << 16)
        or      $t2, $t2, $at
        sw      $t2, 20($s0)            # yb | xl
        or      $t3, $t3, $at
        sw      $t3, 28($s0)            # yb | xr
        sll     $v0, $v0, 16            # tpage << 16 (s3 = page is set from it at the end)
        andi    $at, $t1, 0x400         # x offsets: H flip (uL, uR) = (u + 15, u - 1), else (u, u + 16)
        li      $t2, 0                  # ax
        beqz    $at, 5f
        li      $t3, 16                 # (delay) bx
        li      $t2, 15
        li      $t3, -1
5:      andi    $at, $t1, 0x800         # y offsets: V flip (vT, vB) = (v + 15, v - 1), else (v, v + 16)
        li      $t1, 0                  # ay
        beqz    $at, 6f
        li      $at, 0x1000             # (delay) by
        li      $t1, 0xF00
        li      $at, -0x100
6:      addu    $s3, $t0, $t2           # (no carry between the bytes once the edge test passed)
        addu    $s3, $s3, $t1
        sw      $s3, 8($s0)             # clut << 16 | vT << 8 | uL = tex + ax + ay
        andi    $t0, $t0, 0xFFFF        # uv
        addu    $s3, $t0, $t3
        addu    $s3, $s3, $t1
        or      $s3, $s3, $v0
        sw      $s3, 16($s0)            # tpage << 16 | vT << 8 | uR
        addu    $s3, $t0, $t2
        addu    $s3, $s3, $at
        sw      $s3, 24($s0)            # vB << 8 | uL
        addu    $s3, $t0, $t3
        addu    $s3, $s3, $at
        sw      $s3, 32($s0)            # vB << 8 | uR
        srl     $s3, $v0, 16            # page = tpage
        b       .Lnext
        addiu   $s0, $s0, 36            # (delay)

.Lfragq:                                # EmitFrag before a quad
        addiu   $t3, $s0, 60
        sltu    $t3, $s2, $t3
        bnez    $t3, .Lbailpiece
        addiu   $t2, $s1, -40           # (delay)
        subu    $t3, $s0, $t2
        srl     $t3, $t3, 2
        addiu   $t3, $t3, -1
        sll     $t3, $t3, 24
        sll     $at, $s0, 8
        srl     $at, $at, 8
        or      $t3, $t3, $at
        sw      $t3, 0($t2)
        addiu   $s1, $s0, 40
        b       .Lqroom
        addiu   $s0, $s0, 4             # (delay)

.Llive:                                 # a flip rectangle: the tile's own sprite from its last column / row
        sltu    $at, $s1, $s0           # (a piece that needs a new span goes to the C body before anything is counted)
        beqz    $at, 11f
        addiu   $at, $s0, 60            # (delay)
        sltu    $at, $s2, $at
        bnez    $at, .Lbailpiece
        nop
11:     andi    $at, $t1, 0x400
        beqz    $at, 9f
        nop
        addiu   $t0, $t0, 15
9:      andi    $at, $t1, 0x800
        beqz    $at, 10f
        nop
        addiu   $t0, $t0, 0xF00         # vFlip1 for v0 = 0: 15 << 8
10:     sll     $at, $t3, 2
        or      $v0, $v0, $at           # tpage |= flips << 2 (GP0 E1h bits 12 / 13)
        lw      $at, 64($a0)
        nop
        addiu   $at, $at, 1
        b       .Lpiece
        sw      $at, 64($a0)            # (delay) ++frects

.Lbailpiece:
        addiu   $v1, $v1, -1            # the C body counts it
.Lbail:
        sw      $t1, 76($a0)            # the tile left to the C body
        b       .Lout
        li      $v0, 1                  # (delay)
.Ldone:
        li      $v0, 0
.Lout:
        sw      $s0, 0($a0)
        sw      $s1, 4($a0)
        sw      $s3, 12($a0)
        sw      $s4, 16($a0)
        sw      $s5, 20($a0)
        sw      $s6, 24($a0)
        sw      $fp, 40($a0)
        sw      $v1, 60($a0)
        lw      $s0, 0($sp)
        lw      $s1, 4($sp)
        lw      $s2, 8($sp)
        lw      $s3, 12($sp)
        lw      $s4, 16($sp)
        lw      $s5, 20($sp)
        lw      $s6, 24($sp)
        lw      $s7, 28($sp)
        lw      $fp, 32($sp)
        jr      $ra
        addiu   $sp, $sp, 40            # (delay)
        .size PS1BandFastAsm, .-PS1BandFastAsm
