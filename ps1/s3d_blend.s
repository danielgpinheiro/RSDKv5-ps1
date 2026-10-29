# docs/32 9f (UFO item 1): PS1Scene3D.cpp's keyframe blend in assembly. The PMD6 frames are 4-aligned with whole-word
# position counts (tools/mesh/build_meshes.py), so each keyframe is read a word at a time (2 or 4 components a RAM read
# instead of one), and the multiply has the blend factor t (0..256) as rs: always its 6-cycle form (psx-spx CPU
# specifications: the time depends on rs). The results are the C loops' exactly:
#   PS1S3DBlend16(a, b, out, words, t): 16-bit positions, out[i] = (int16)(a[i] + (((b[i] - a[i]) * t) >> 8))
#   PS1S3DBlend8(a, b, out, words, t):  8-bit positions,  out[i] = (int16)((a[i] << 7) + (((b[i] - a[i]) * t) >> 1))
# words: the input words of one keyframe (2 / 4 components each); out is int16, 4-aligned. At least two instructions
# between an mflo and the next mult (psx-spx: hi / lo are not to be touched in the 2 cycles after an access).
        .set noreorder
        .text
        .align 2
        .globl PS1S3DBlend16
        .type PS1S3DBlend16, @function
PS1S3DBlend16:
        lw      $t9, 16($sp)            # t
        beqz    $a3, 2f
        nop
1:      lw      $t0, 0($a0)             # a: x | y << 16 (or z | x' << 16: any two components)
        lw      $t1, 0($a1)             # b
        sll     $t2, $t0, 16
        sra     $t2, $t2, 16            # a lo
        sll     $t3, $t1, 16
        sra     $t3, $t3, 16            # b lo
        subu    $t3, $t3, $t2
        mult    $t9, $t3                # t * (b - a), lo
        sra     $t4, $t0, 16            # a hi
        sra     $t5, $t1, 16            # b hi
        subu    $t5, $t5, $t4
        addiu   $a0, $a0, 4
        addiu   $a1, $a1, 4
        addiu   $a3, $a3, -1
        mflo    $t6
        sra     $t6, $t6, 8
        addu    $t6, $t6, $t2
        mult    $t9, $t5                # t * (b - a), hi
        andi    $t6, $t6, 0xFFFF
        mflo    $t7
        sra     $t7, $t7, 8
        addu    $t7, $t7, $t4
        sll     $t7, $t7, 16
        or      $t6, $t6, $t7
        sw      $t6, 0($a2)
        bnez    $a3, 1b
        addiu   $a2, $a2, 4             # (delay)
2:      jr      $ra
        nop
        .size PS1S3DBlend16, .-PS1S3DBlend16

        .globl PS1S3DBlend8
        .type PS1S3DBlend8, @function
PS1S3DBlend8:
        lw      $t9, 16($sp)            # t
        beqz    $a3, 2f
        nop
1:      lw      $t0, 0($a0)             # a: 4 components
        lw      $t1, 0($a1)             # b
        sll     $t2, $t0, 24
        sra     $t2, $t2, 24            # a0
        sll     $t3, $t1, 24
        sra     $t3, $t3, 24            # b0
        subu    $t3, $t3, $t2
        mult    $t9, $t3                # component 0
        sll     $t4, $t0, 16
        sra     $t4, $t4, 24            # a1
        sll     $t5, $t1, 16
        sra     $t5, $t5, 24            # b1
        subu    $t5, $t5, $t4
        sll     $t2, $t2, 7
        mflo    $t6
        sra     $t6, $t6, 1
        addu    $t6, $t6, $t2
        mult    $t9, $t5                # component 1
        andi    $t6, $t6, 0xFFFF
        sll     $t2, $t0, 8
        sra     $t2, $t2, 24            # a2
        sll     $t3, $t1, 8
        sra     $t3, $t3, 24            # b2
        subu    $t3, $t3, $t2
        sll     $t4, $t4, 7
        mflo    $t7
        sra     $t7, $t7, 1
        addu    $t7, $t7, $t4
        mult    $t9, $t3                # component 2
        sll     $t7, $t7, 16
        or      $t6, $t6, $t7
        sw      $t6, 0($a2)
        sra     $t4, $t0, 24            # a3
        sra     $t5, $t1, 24            # b3
        subu    $t5, $t5, $t4
        sll     $t2, $t2, 7
        addiu   $a0, $a0, 4
        addiu   $a1, $a1, 4
        mflo    $t6
        sra     $t6, $t6, 1
        addu    $t6, $t6, $t2
        mult    $t9, $t5                # component 3
        andi    $t6, $t6, 0xFFFF
        sll     $t4, $t4, 7
        addiu   $a3, $a3, -1
        mflo    $t7
        sra     $t7, $t7, 1
        addu    $t7, $t7, $t4
        sll     $t7, $t7, 16
        or      $t6, $t6, $t7
        sw      $t6, 4($a2)
        bnez    $a3, 1b
        addiu   $a2, $a2, 8             # (delay)
2:      jr      $ra
        nop
        .size PS1S3DBlend8, .-PS1S3DBlend8

# ps1-ufo: the same blends on the GTE, three components an INTPL (psx-spx "GTE General Purpose Calculation Commands":
# with sf = 1, lm = 0: IR = FC - IR, then IR = IR_old + (IR * IR0) >> 12, saturated to 16 bits), no CPU multiply:
#   PS1S3DBlendGte16(a, b, out, comps, t): IR = a, FC = b, IR0 = t << 4: a + floor((b - a) * t / 256), the C's exactly
#     while |b - a| < 32768 (the keyframes of one vertex: PS1_CHECK_BLEND compares every component with the C)
#   PS1S3DBlendGte8(a, b, out, comps, t):  IR = a << 7, FC = b << 7: (a << 7) + floor((b - a) * t / 2), always exact
# comps: a multiple of 3 (whole vertices); a, b, out need no alignment. GTE registers it writes: IR0-IR3, RFC-BFC (the
# projection's own are set before every model's: PS1Gte3DSetup).
        .globl PS1S3DBlendGte16
        .type PS1S3DBlendGte16, @function
PS1S3DBlendGte16:
        lw      $t9, 16($sp)            # t
        beqz    $a3, 2f
        sll     $t9, $t9, 4             # (delay) IR0 = t * 16 (4.12)
        mtc2    $t9, $8
1:      lh      $t0, 0($a0)
        lh      $t1, 2($a0)
        lh      $t2, 4($a0)
        lh      $t3, 0($a1)
        lh      $t4, 2($a1)
        lh      $t5, 4($a1)
        mtc2    $t0, $9                 # IR1-3 = a
        mtc2    $t1, $10
        mtc2    $t2, $11
        ctc2    $t3, $21                # RFC, GFC, BFC = b
        ctc2    $t4, $22
        ctc2    $t5, $23
        addiu   $a0, $a0, 6
        addiu   $a1, $a1, 6
        cop2    0x0980011               # INTPL
        addiu   $a3, $a3, -3
        mfc2    $t0, $9
        mfc2    $t1, $10
        mfc2    $t2, $11
        sh      $t0, 0($a2)
        sh      $t1, 2($a2)
        sh      $t2, 4($a2)
        bnez    $a3, 1b
        addiu   $a2, $a2, 6             # (delay)
2:      jr      $ra
        nop
        .size PS1S3DBlendGte16, .-PS1S3DBlendGte16

        .globl PS1S3DBlendGte8
        .type PS1S3DBlendGte8, @function
PS1S3DBlendGte8:
        lw      $t9, 16($sp)            # t
        beqz    $a3, 2f
        sll     $t9, $t9, 4             # (delay)
        mtc2    $t9, $8
1:      lb      $t0, 0($a0)
        lb      $t1, 1($a0)
        lb      $t2, 2($a0)
        lb      $t3, 0($a1)
        lb      $t4, 1($a1)
        lb      $t5, 2($a1)
        sll     $t0, $t0, 7
        sll     $t1, $t1, 7
        sll     $t2, $t2, 7
        sll     $t3, $t3, 7
        sll     $t4, $t4, 7
        sll     $t5, $t5, 7
        mtc2    $t0, $9
        mtc2    $t1, $10
        mtc2    $t2, $11
        ctc2    $t3, $21
        ctc2    $t4, $22
        ctc2    $t5, $23
        addiu   $a0, $a0, 3
        addiu   $a1, $a1, 3
        cop2    0x0980011               # INTPL
        addiu   $a3, $a3, -3
        mfc2    $t0, $9
        mfc2    $t1, $10
        mfc2    $t2, $11
        sh      $t0, 0($a2)
        sh      $t1, 2($a2)
        sh      $t2, 4($a2)
        bnez    $a3, 1b
        addiu   $a2, $a2, 6             # (delay)
2:      jr      $ra
        nop
        .size PS1S3DBlendGte8, .-PS1S3DBlendGte8
