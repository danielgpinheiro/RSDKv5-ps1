# docs/ports/mania/32 9w.1: the update scan (RSDK/Scene/Object.cpp PS1UpdateScan) for one camera box without sleeping
# groups, in assembly: the same decisions as the C loop, every value in a register (GCC's loop kept the sleeping-group
# state live and tested the live bits and the range end on every slot: ~53 instructions a settled slot, here 24). Walks
# the live bits from slot s; a blank slot gets inRange = 0 and its live bit cleared; a live slot is settled
# (inRange = 0) when stock would find it out of range for every camera:
#   ACTIVE_BOUNDS (4): x outside the union box on x, or inside on x and outside on y (both with their validity bounds);
#   ACTIVE_XBOUNDS (5): x outside the union box on x; ACTIVE_NEVER (0) / ACTIVE_PAUSED (3): always.
# 9w.4: x more than 2^30 from camera 0 (x + vx < 0: past the box's validity) is still settled when every camera is within
# 2^28 of camera 0 on x (box +36) and rx is valid: for x - origin in [2^30, 7 * 2^28) or [-7 * 2^28, -2^30), stock's
# x - camera (the same value less the camera's offset from the origin, < 2^28) cannot wrap and exceeds 3 * 2^28, more
# than any rx + ox (< 2^29); HCZ2's second half had every entity left of x 11,776 back in the pass's camera loop.
# Anything else (and an invalid position or range) is a slot to decide: returned to the caller, or (9w.8) handed to the
# update pass's helper (box +40, Object.cpp PS1UpdateOne: stock's decision and the update), which returns the range's
# end (a callback may move it) or 0 when the camera union no longer holds (the scan then returns the next slot to the
# pass); after it the scan reloads the box (a callback may move the cameras) and the live bits (it may create entities),
# keeping nothing else across it. An ACTIVE_BOUNDS slot gets inRange = 0 as soon as it is outside on x: if its y then
# makes it a slot to decide, stock's ACTIVE_BOUNDS decision writes inRange before anything reads it.
# EntityBase **PS1ScanFastAsm(EntityBase **p, EntityBase **pend, uint32 s, const uint32 *box); p < pend; returns the
# slot to decide, pend, or (helper) the slot after the one that ended the union. box (Object.cpp ps1ScanBox), byte
# offsets: 0 minX, 4 w, 8 vx, 12 minY, 16 h, 20 vy, 24 &ps1LiveBits[0], 28 the De Bruijn table, 32 &objectEntityPtr[0],
# 36 cameras near camera 0 on x, 40 the helper or 0. Entity offsets: position 0 / 4, updateRange 24 / 28, classID 54,
# inRange 56, active 76. Frame: o32's 16-byte argument area, s0-s7, ra, p and the box across the helper call.
        .set noreorder
        .set noat
        .text
        .align 2
        .globl PS1ScanFastAsm
        .type PS1ScanFastAsm, @function
PS1ScanFastAsm:
        addiu   $sp, $sp, -64
        sw      $s0, 16($sp)
        sw      $s1, 20($sp)
        sw      $s2, 24($sp)
        sw      $s3, 28($sp)
        sw      $s4, 32($sp)
        sw      $s5, 36($sp)
        sw      $s6, 40($sp)
        sw      $s7, 44($sp)
        sw      $ra, 48($sp)
        move    $s6, $a3                # the box
        sw      $a3, 56($sp)
.Lenter:                                # a0 = p < a1 = pend
        lui     $s2, 0x1000
        lui     $s4, 0x2000
        lw      $t0, 0($s6)             # minX
        lw      $t1, 4($s6)             # w
        lw      $t2, 8($s6)             # vx
        lw      $t3, 12($s6)            # minY
        lw      $t4, 16($s6)            # h
        lw      $t5, 20($s6)            # vy
        lw      $v1, 24($s6)            # live bits
        lw      $s3, 28($s6)            # De Bruijn table
        lw      $s5, 32($s6)            # objectEntityPtr
        lui     $a2, 0x077C
        subu    $at, $a0, $s5
        srl     $at, $at, 2             # s
        srl     $s0, $at, 5
        sll     $s0, $s0, 2
        addu    $s0, $v1, $s0           # s0 = &live[s >> 5]
        andi    $at, $at, 31
        lw      $a3, 0($s0)
        li      $v1, 32
        subu    $v1, $v1, $at
        sll     $v1, $v1, 2
        addu    $s1, $a0, $v1           # s1 = the word's end, or pend if sooner
        sltu    $v1, $a1, $s1
        beqz    $v1, 1f
        srlv    $a3, $a3, $at           # a3 = the live bits from slot s on
        move    $s1, $a1
1:      ori     $a2, $a2, 0xB531        # the De Bruijn multiplier
# invariant: a0 < s1; a3 = the live bits from a0's slot
.Lnext:
        andi    $v1, $a3, 1
        beqz    $v1, .Lgap
        lw      $t6, 0($a0)             # the entity
.Lslot1:
        li      $v1, 4
        lhu     $t7, 54($t6)            # classID
        lbu     $t8, 76($t6)            # active
        lw      $t9, 0($t6)             # x
        beqz    $t7, .Lblank
        lw      $v0, 24($t6)            # rx
        bne     $t8, $v1, .Lnotbounds
        addu    $at, $t9, $t2           # (int32)(x + vx) < 0: far, or stock decides
        bltz    $at, .Lfarx
        addu    $at, $v0, $s2
        sltu    $at, $at, $s4           # (rx + 0x10000000) < 0x20000000: rx valid
        beqz    $at, .Ldecide
        subu    $v1, $t9, $t0
        addu    $v1, $v1, $v0           # x - minX + rx
        sll     $at, $v0, 1
        addu    $at, $at, $t1           # w + 2 * rx
        sltu    $at, $at, $v1
        beqz    $at, .Linsidex          # x - minX + rx <= w + 2 * rx: inside on x
        sw      $zero, 56($t6)          # inRange = false (see the header)
.Ladvance:
        addiu   $a0, $a0, 4
        bne     $a0, $s1, .Lnext
        srl     $a3, $a3, 1
.Lwordend:                              # a0 >= s1
        beq     $s1, $a1, .Lpend
        addiu   $s0, $s0, 4             # the next word (a0 is its first slot)
        lw      $a3, 0($s0)
        addiu   $s1, $a0, 128
        sltu    $v1, $a1, $s1
        beqz    $v1, .Lnext
        nop
        b       .Lnext
        move    $s1, $a1
.Lgap:                                  # a0's slot is not live
        beqz    $a3, .Lempty
        andi    $v1, $a3, 0xE
        beqz    $v1, .Lfar
        nop
.Lshift:                                # the next live slot within 3: one at a time
        srl     $a3, $a3, 1
        andi    $v1, $a3, 1
        beqz    $v1, .Lshift
        addiu   $a0, $a0, 4
        b       .Lfound
        sltu    $v1, $a0, $s1
.Lfar:                                  # farther: the lowest set bit's index (De Bruijn)
        subu    $v1, $zero, $a3
        and     $v1, $v1, $a3
        multu   $v1, $a2
        mflo    $v1
        srl     $v1, $v1, 27
        addu    $v1, $v1, $s3
        lbu     $v1, 0($v1)
        nop
        srlv    $a3, $a3, $v1
        sll     $v1, $v1, 2
        addu    $a0, $a0, $v1
        sltu    $v1, $a0, $s1
.Lfound:
        bnez    $v1, .Lslot1
        lw      $t6, 0($a0)
        b       .Lwordend
        nop
.Lempty:                                # the rest of the word: no live slot
        b       .Lwordend
        move    $a0, $s1
.Linsidex:
        lw      $t9, 4($t6)             # y
        lw      $v0, 28($t6)            # ry
        nop
        addu    $at, $t9, $t5
        bltz    $at, .Ldecide
        addu    $at, $v0, $s2
        sltu    $at, $at, $s4
        beqz    $at, .Ldecide
        subu    $v1, $t9, $t3
        addu    $v1, $v1, $v0           # y - minY + ry
        sll     $at, $v0, 1
        addu    $at, $at, $t4           # h + 2 * ry
        sltu    $at, $at, $v1
        bnez    $at, .Ladvance          # outside on y: settled (inRange already 0)
        nop
        b       .Ldecide                # inside on y too: stock decides
        nop
.Lnotbounds:
        li      $v1, 5
        bne     $t8, $v1, .Lnotx
        addu    $at, $t9, $t2
        bltz    $at, .Lfarx
        addu    $at, $v0, $s2
        sltu    $at, $at, $s4
        beqz    $at, .Ldecide
        subu    $v1, $t9, $t0
        addu    $v1, $v1, $v0
        sll     $at, $v0, 1
        addu    $at, $at, $t1
        sltu    $at, $at, $v1
        beqz    $at, .Ldecide           # inside on x: stock decides
        nop
.Lsettle:
        b       .Ladvance
        sw      $zero, 56($t6)          # inRange = false
.Lnotx:
        beqz    $t8, .Lsettle           # ACTIVE_NEVER
        li      $v1, 3
        beq     $t8, $v1, .Lsettle      # ACTIVE_PAUSED
        nop
        b       .Ldecide
        nop
.Lfarx:                                 # 9w.4 (see the header): x + vx < 0
        lw      $at, 36($s6)
        nop
        beqz    $at, .Ldecide           # (a camera 2^28 or more from camera 0 on x: stock decides)
        addu    $at, $v0, $s2
        sltu    $at, $at, $s4           # rx valid
        beqz    $at, .Ldecide
        addu    $at, $t9, $t2           # x + vx, in [0x80000000, 2^32)
        lui     $v1, 0xB000
        sltu    $v1, $at, $v1
        bnez    $v1, .Lsettle           # [0x80000000, 0xB0000000): 2^30 <= x - origin < 7 * 2^28
        lui     $v1, 0xD000
        sltu    $v1, $at, $v1
        beqz    $v1, .Lsettle           # [0xD0000000, 2^32): -7 * 2^28 <= x - origin < -2^30
        nop
.Ldecide:                               # a0's slot: stock decides
        lw      $v1, 40($s6)            # 9w.8: the update pass's helper
        nop
        beqz    $v1, .Lout
        move    $v0, $a0
        sw      $a0, 52($sp)            # (kept in the frame across the call, as the box)
        jalr    $v1                     # PS1UpdateOne(p): stock's decision and the update; the range's end, or 0
        nop
        lw      $s7, 52($sp)
        lw      $s6, 56($sp)
        move    $a1, $v0
        beqz    $a1, .Lout              # the union no longer holds: the pass goes on from p + 1
        addiu   $v0, $s7, 4
        addiu   $a0, $s7, 4
        sltu    $at, $a0, $a1
        bnez    $at, .Lenter            # (the box and the live bits again)
        nop
        b       .Lout
        move    $v0, $a1
.Lblank:                                # inRange = false, and the slot leaves the live bits
        sw      $zero, 56($t6)
        subu    $v1, $a0, $s5
        srl     $v1, $v1, 2
        andi    $v1, $v1, 31
        li      $at, 1
        sllv    $at, $at, $v1
        lw      $v1, 0($s0)
        nor     $at, $zero, $at
        and     $v1, $v1, $at
        b       .Ladvance
        sw      $v1, 0($s0)
.Lpend:
        move    $v0, $a1
.Lout:
        lw      $ra, 48($sp)            # (first: the load delay would hand jr the helper call's ra)
        lw      $s0, 16($sp)
        lw      $s1, 20($sp)
        lw      $s2, 24($sp)
        lw      $s3, 28($sp)
        lw      $s4, 32($sp)
        lw      $s5, 36($sp)
        lw      $s6, 40($sp)
        lw      $s7, 44($sp)
        jr      $ra
        addiu   $sp, $sp, 64
        .size PS1ScanFastAsm, . - PS1ScanFastAsm
