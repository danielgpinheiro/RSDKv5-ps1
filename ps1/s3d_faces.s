# docs/32 9f (UFO 60 fps plan, step B): PS1Scene3D.cpp's PS1S3DFaces<3, false> (gouraud triangles) in assembly: every
# value of the loop in a register (GCC spilled seven of them, reloaded from the stack each face). The same decisions,
# keys, buckets and words as the C function; the rare cases go back to it:
#   return 0: all faces done; 1: the reserved words are full (C refills, then calls again);
#   2: the current face has a corner outside the GPU-limit band and the model is not wholly on screen (C runs its exact
#      span test on that one face, then calls again with the next).
# PS1S3DAsmCtx (PS1Scene3D.cpp), word offsets:
#   0 idx (u16 x 3 per face), 4 cols (u32 x 3), 8 idxEnd, 12 xyp (u32 per vertex), 16 szp (u16), 20 ocp (u8: outcodes),
#   24 k (key shift), 28 cull (-1 / 0 / 1), 32 sideMask (15, 0 when wholly on screen), 36 inside, 40 w, 44 wend,
#   48 kmin, 52 kmax, 56 cmd, 60 shift (bucket), 64 ot
# idx, cols and w are written back.
        .set noreorder
        .set noat
        .text
        .align 2
        .globl PS1S3DFacesTriAsm
        .type PS1S3DFacesTriAsm, @function
PS1S3DFacesTriAsm:
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
        sw      $ra, 36($sp)
        lw      $s0, 0($a0)             # idx
        lw      $s1, 4($a0)             # cols
        lw      $s2, 8($a0)             # idxEnd
        lw      $s3, 12($a0)            # xyp
        lw      $s4, 16($a0)            # szp
        lw      $s5, 20($a0)            # ocp
        lw      $a3, 24($a0)            # k
        lw      $v1, 28($a0)            # cull
        lw      $ra, 32($a0)            # sideMask
        lw      $s6, 40($a0)            # w
        lw      $s7, 44($a0)            # wend
        lw      $t8, 48($a0)            # kmin
        lw      $t9, 52($a0)            # kmax
        lw      $a1, 56($a0)            # cmd
        lw      $a2, 60($a0)            # shift
        lw      $fp, 64($a0)            # ot

.Lloop:
        beq     $s0, $s2, .Ldone
        nop
        lhu     $t0, 0($s0)             # i0, i1, i2
        lhu     $t1, 2($s0)
        lhu     $t2, 4($s0)
        addu    $t3, $s5, $t0           # outcodes
        lbu     $t3, 0($t3)
        addu    $t4, $s5, $t1
        lbu     $t4, 0($t4)
        addu    $t5, $s5, $t2
        lbu     $t5, 0($t5)
        and     $v0, $t3, $t4
        and     $v0, $v0, $t5
        and     $v0, $v0, $ra           # all corners off one side (not wholly on screen)
        or      $t3, $t3, $t4
        or      $t3, $t3, $t5           # any corner
        andi    $t4, $t3, 0x10          # a corner near
        or      $v0, $v0, $t4
        bnez    $v0, .Lnext
        andi    $t4, $t3, 0x20          # (delay) a corner past the band
        bnez    $t4, .Lfar
        nop
.Lafterfar:
        sll     $t3, $t0, 2             # the corners' screen positions
        addu    $t3, $s3, $t3
        lw      $t3, 0($t3)
        sll     $t4, $t1, 2
        addu    $t4, $s3, $t4
        lw      $t4, 0($t4)
        sll     $t5, $t2, 2
        addu    $t5, $s3, $t5
        beqz    $v1, .Lkey
        lw      $t5, 0($t5)             # (delay)
        # turned away: (bx - ax) * (cy - ay) - (cx - ax) * (by - ay), as int32
        sll     $v0, $t3, 16
        sra     $v0, $v0, 16            # ax
        sll     $t6, $t4, 16
        sra     $t6, $t6, 16
        subu    $t6, $t6, $v0           # bx - ax
        sll     $t7, $t5, 16
        sra     $t7, $t7, 16
        subu    $t7, $t7, $v0           # cx - ax
        sra     $v0, $t3, 16            # ay
        sra     $at, $t5, 16
        subu    $at, $at, $v0           # cy - ay
        mult    $t6, $at
        sra     $at, $t4, 16
        subu    $at, $at, $v0           # by - ay
        mflo    $t6
        nop
        nop
        mult    $t7, $at
        mflo    $t7
        nop
        subu    $t6, $t6, $t7           # cross
        bltz    $v1, .Lcullneg
        nop
        bltz    $t6, .Lnext             # cull > 0: front faces have cross >= 0
        nop
        b       .Lkey
        nop
.Lcullneg:
        bgtz    $t6, .Lnext             # cull < 0: front faces have cross <= 0
        nop
.Lkey:
        sll     $t0, $t0, 1             # depths
        addu    $t0, $s4, $t0
        lhu     $t0, 0($t0)
        sll     $t1, $t1, 1
        addu    $t1, $s4, $t1
        lhu     $t1, 0($t1)
        sll     $t2, $t2, 1
        addu    $t2, $s4, $t2
        lhu     $t2, 0($t2)
        addu    $t0, $t0, $t1
        addu    $t0, $t0, $t2
        sllv    $t0, $t0, $a3
        sll     $t0, $t0, 2             # ((z0 + z1 + z2) << k) * 4
        sltu    $at, $t0, $t8
        beqz    $at, 1f
        nop
        move    $t0, $t8                # below kmin
1:      sltu    $at, $t9, $t0
        beqz    $at, 2f
        nop
        move    $t0, $t9                # above kmax
2:      subu    $t0, $t0, $t8
        srlv    $t0, $t0, $a2
        sll     $t0, $t0, 2
        addu    $t0, $fp, $t0           # the bucket's node
        addiu   $at, $s6, 28
        sltu    $at, $s7, $at           # wend < w + 7 words: refill
        bnez    $at, .Lrefill
        nop
        lw      $t1, 0($s1)             # the corners' colours
        lw      $t2, 4($s1)
        lw      $v0, 8($s1)
        or      $t1, $t1, $a1           # cmd | c0
        sw      $t1, 4($s6)
        sw      $t3, 8($s6)
        sw      $t2, 12($s6)
        sw      $t4, 16($s6)
        sw      $v0, 20($s6)
        sw      $t5, 24($s6)
        lw      $t1, 0($t0)             # the bucket's list
        lui     $at, 0x0600             # 6 words
        or      $t1, $t1, $at
        sw      $t1, 0($s6)
        sll     $t1, $s6, 8
        srl     $t1, $t1, 8
        sw      $t1, 0($t0)
        addiu   $s6, $s6, 28
.Lnext:
        addiu   $s0, $s0, 6
        b       .Lloop
        addiu   $s1, $s1, 12            # (delay)

.Lfar:
        lw      $at, 36($a0)            # inside: no span test
        nop
        bnez    $at, .Lafterfar
        nop
        b       .Lout
        li      $v0, 2                  # (delay)
.Lrefill:
        b       .Lout
        li      $v0, 1                  # (delay)
.Ldone:
        li      $v0, 0
.Lout:
        sw      $s0, 0($a0)
        sw      $s1, 4($a0)
        sw      $s6, 40($a0)
        lw      $s0, 0($sp)
        lw      $s1, 4($sp)
        lw      $s2, 8($sp)
        lw      $s3, 12($sp)
        lw      $s4, 16($sp)
        lw      $s5, 20($sp)
        lw      $s6, 24($sp)
        lw      $s7, 28($sp)
        lw      $fp, 32($sp)
        lw      $ra, 36($sp)
        nop                             # (the load's delay slot)
        jr      $ra
        addiu   $sp, $sp, 40            # (delay)
        .size PS1S3DFacesTriAsm, .-PS1S3DFacesTriAsm
