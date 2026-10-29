# ps1-ufo2: RSDK/Scene/PS1/PS1ScanSleep.cpp's scan (the UFO stages' update scan with sleeping groups) in assembly: the
# same decisions, writes and return values, the one camera kept in registers, no register saved on the common paths
# (GCC's version saved nine and reloaded the camera per slot; ~100 calls and ~400 slots a frame). Linked into the
# overlays with the C file (gen_overlays EXTRAS 'scansleep'); PS1ScanSleepLink picks it (PS1_SCAN_SLEEP_C=1: the C).
#   EntityBase **PS1ScanSleepAsm(EntityBase **p, EntityBase **pend)
# RSDK::ps1Scan (PS1ScanShared.hpp) byte offsets: box 0 (minX 0, w 4, vx 8, minY 12, h 16, vy 20), camX 44, camY 48,
# camOX 52, camOY 56, sleep 60, sleepOX 64, sleepOY 68, grp 72, grpLive 76, grpOk 80 (u8).
# EntityBase: position.x 0, position.y 4, updateRange.x 24, updateRange.y 28, classID 54 (u16), inRange 56, active 76 (u8).
# A sleeping group (PS1SleepGroup, 8 bytes): x 0, y 2 (s16, the camera in whole pixels), margin 4 (u16).
        .set noreorder
        .set noat
        .text
        .align 2
        .globl PS1ScanSleepAsm
        .type PS1ScanSleepAsm, @function
# registers: a0 p, a1 pend, a2 s (slot), a3 send, t9 &ps1Scan, t8 camX, t7 camY, t6 camOX, v1 live bits, v0 sleep bits
PS1ScanSleepAsm:
        lui     $t9, %hi(_ZN4RSDK7ps1ScanE)
        addiu   $t9, $t9, %lo(_ZN4RSDK7ps1ScanE)
        lw      $t6, 52($t9)            # camOX
        lw      $t0, 64($t9)            # sleepOX
        lw      $t1, 56($t9)            # camOY
        lw      $t2, 68($t9)            # sleepOY
        lui     $v0, %hi(_ZN4RSDK12ps1SleepBitsE)
        bne     $t6, $t0, .Lreset
        addiu   $v0, $v0, %lo(_ZN4RSDK12ps1SleepBitsE)
        bne     $t1, $t2, .Lreset
        nop
.Lgo:
        lui     $t0, %hi(_ZN4RSDK15objectEntityPtrE)
        addiu   $t0, $t0, %lo(_ZN4RSDK15objectEntityPtrE)
        subu    $a2, $a0, $t0
        sra     $a2, $a2, 2             # s
        subu    $a3, $a1, $t0
        sra     $a3, $a3, 2             # send
        lw      $t8, 44($t9)            # camX
        lw      $t7, 48($t9)            # camY
        lui     $v1, %hi(_ZN4RSDK11ps1LiveBitsE)
        addiu   $v1, $v1, %lo(_ZN4RSDK11ps1LiveBitsE)

.Lloop:
        sltu    $at, $a0, $a1
        beqz    $at, .Lretp
        andi    $t0, $a2, 31            # (delay)
        bnez    $t0, .Lnotword
        srl     $t1, $a2, 5             # (delay)
        sll     $t1, $t1, 2
        addu    $t1, $v1, $t1
        lw      $t1, 0($t1)             # this word's live bits
        subu    $t2, $a1, $a0           # (load delay) pend - p, bytes
        bnez    $t1, .Lnotword
        slti    $t2, $t2, 129           # (delay) pend - p <= 32 slots
        bnez    $t2, .Lretpend
        addiu   $a0, $a0, 128           # (delay) the whole word: the next one
        b       .Lloop
        addiu   $a2, $a2, 32            # (delay)

.Lnotword:
        andi    $t0, $a2, 7
        bnez    $t0, .Lslot
        srl     $t1, $a2, 3             # (delay) g
        srl     $t2, $t1, 5
        sll     $t2, $t2, 2
        addu    $t2, $v0, $t2           # &ps1SleepBits[g >> 5]
        lw      $t3, 0($t2)
        andi    $t4, $t1, 31            # (load delay)
        srlv    $t5, $t3, $t4
        andi    $t5, $t5, 1
        beqz    $t5, .Lgrpstart
        addiu   $t5, $a2, 8             # (delay)
        sltu    $t5, $a3, $t5           # send < s + 8: the group is cut off
        bnez    $t5, .Lwake
        lw      $t5, 60($t9)            # (delay) sleep groups
        sll     $t0, $t1, 3
        addu    $t5, $t5, $t0           # &sleep[g]
        lh      $t0, 0($t5)             # its camera x
        lh      $at, 46($t9)            # camX >> 16
        lhu     $t5, 4($t5)             # (load delay) margin
        subu    $t0, $at, $t0
        sra     $at, $t0, 31
        xor     $t0, $t0, $at
        subu    $t0, $t0, $at           # |mx|
        addiu   $t0, $t0, 1
        sltu    $at, $t5, $t0           # margin < |mx| + 1
        bnez    $at, .Lwake
        lw      $t0, 60($t9)            # (delay)
        sll     $at, $t1, 3
        addu    $t0, $t0, $at
        lh      $t0, 2($t0)             # its camera y
        lh      $at, 50($t9)            # camY >> 16
        nop
        subu    $t0, $at, $t0
        sra     $at, $t0, 31
        xor     $t0, $t0, $at
        subu    $t0, $t0, $at           # |my|
        addiu   $t0, $t0, 1
        sltu    $at, $t5, $t0
        bnez    $at, .Lwake
        nop
        addiu   $a0, $a0, 32            # asleep and valid: the group passed over
        b       .Lloop
        addiu   $a2, $a2, 8             # (delay)
.Lwake:
        li      $t5, 1
        sllv    $t5, $t5, $t4
        nor     $t5, $zero, $t5
        and     $t3, $t3, $t5
        sw      $t3, 0($t2)             # its sleep bit cleared
.Lgrpstart:
        li      $t5, 1
        sw      $t1, 72($t9)            # grp = g
        sw      $zero, 76($t9)          # grpLive = 0
        sb      $t5, 80($t9)            # grpOk = true

.Lslot:
        lw      $t0, 0($a0)             # ent
        nop
        lhu     $t1, 54($t0)            # classID
        lbu     $t2, 76($t0)            # active
        lw      $t3, 0($t0)             # position.x
        beqz    $t1, .Lsettle
        lw      $t4, 24($t0)            # (delay) updateRange.x
        li      $at, 7
        bne     $t2, $at, .Lnotr
        li      $at, 4                  # (delay)
        # ACTIVE_RBOUNDS: stock's test for the one camera, its int32 arithmetic
        lw      $t5, 4($t0)             # position.y
        subu    $t3, $t3, $t8           # dx
        sra     $at, $t3, 31
        xor     $t3, $t3, $at
        subu    $t3, $t3, $at
        sra     $t3, $t3, 16            # sx
        mult    $t3, $t3
        subu    $t5, $t5, $t7           # dy
        sra     $at, $t5, 31
        xor     $t5, $t5, $at
        subu    $t5, $t5, $at
        sra     $t5, $t5, 16            # sy
        mflo    $t3
        addu    $t4, $t4, $t6           # rx + camOX
        nop                             # (mult not within two instructions of mflo)
        mult    $t5, $t5
        mflo    $t5
        addu    $t3, $t3, $t5           # sx * sx + sy * sy
        slt     $at, $t4, $t3
        beqz    $at, .Lin               # in range
        nop
        b       .Lsettle
        nop
.Lnotr:
        bne     $t2, $at, .Lnotb
        li      $at, 5                  # (delay)
        # ACTIVE_BOUNDS
        lw      $t5, 8($t9)             # vx
        lui     $at, 0x1000
        addu    $t5, $t3, $t5
        bltz    $t5, .Lin               # x + vx < 0: stock decides (in)
        addu    $t5, $t4, $at           # (delay) rx + 0x10000000
        lui     $at, 0x2000
        sltu    $at, $t5, $at
        beqz    $at, .Lin
        lw      $t5, 0($t9)             # (delay) minX
        nop
        subu    $t3, $t3, $t5
        lw      $t5, 4($t9)             # w
        addu    $t3, $t3, $t4           # x - minX + rx
        sll     $t4, $t4, 1
        addu    $t4, $t4, $t5           # w + 2 rx
        sltu    $at, $t4, $t3
        bnez    $at, .Lsettle           # outside on x: out of range whatever y is
        nop
        lw      $t3, 4($t0)             # position.y
        lw      $t5, 20($t9)            # vy
        lw      $t4, 28($t0)            # updateRange.y
        addu    $t5, $t3, $t5
        bltz    $t5, .Lin
        lui     $at, 0x1000             # (delay)
        addu    $t5, $t4, $at
        lui     $at, 0x2000
        sltu    $at, $t5, $at
        beqz    $at, .Lin
        lw      $t5, 12($t9)            # (delay) minY
        nop
        subu    $t3, $t3, $t5
        lw      $t5, 16($t9)            # h
        addu    $t3, $t3, $t4           # y - minY + ry
        sll     $t4, $t4, 1
        addu    $t4, $t4, $t5           # h + 2 ry
        sltu    $at, $t4, $t3
        beqz    $at, .Lin
        nop
        b       .Lsettle
        nop
.Lnotb:
        bne     $t2, $at, .Lnotx
        nop
        # ACTIVE_XBOUNDS
        lw      $t5, 8($t9)             # vx
        lui     $at, 0x1000
        addu    $t5, $t3, $t5
        bltz    $t5, .Lin
        addu    $t5, $t4, $at           # (delay)
        lui     $at, 0x2000
        sltu    $at, $t5, $at
        beqz    $at, .Lin
        lw      $t5, 0($t9)             # (delay) minX
        nop
        subu    $t3, $t3, $t5
        lw      $t5, 4($t9)             # w
        addu    $t3, $t3, $t4
        sll     $t4, $t4, 1
        addu    $t4, $t4, $t5
        sltu    $at, $t4, $t3
        beqz    $at, .Lin
        nop
        b       .Lsettle
        nop
.Lnotx:
        beqz    $t2, .Lsettle           # ACTIVE_NEVER
        li      $at, 3                  # (delay)
        bne     $t2, $at, .Lin          # anything but ACTIVE_PAUSED: in range (the pass decides as stock)
        nop

.Lsettle:                               # out of range: grpOk, the group's sleep, inRange, the live bit
        lw      $t3, 72($t9)            # grp
        srl     $t4, $a2, 3
        bne     $t3, $t4, .Lclear
        nop
        beqz    $t1, .Lgrp7             # (a blank slot leaves grpOk)
        lbu     $t3, 80($t9)            # (delay) grpOk
        beqz    $t3, .Lgrp7             # (already false)
        li      $at, 4                  # (delay) BOUNDS, RBOUNDS, NEVER, PAUSED keep it
        beq     $t2, $at, .Lgrp7
        li      $at, 7                  # (delay)
        beq     $t2, $at, .Lgrp7
        nop
        beqz    $t2, .Lgrp7
        li      $at, 3                  # (delay)
        beq     $t2, $at, .Lgrp7
        nop
        sb      $zero, 80($t9)          # grpOk = false
        move    $t3, $zero
.Lgrp7:
        andi    $at, $a2, 7
        xori    $at, $at, 7
        bnez    $at, .Lclear
        nop
        beqz    $t3, .Lclear
        nop
        # the group's last slot, every slot settled: PS1SleepGroupSet(s >> 3) (a C call: the live registers saved)
        addiu   $sp, $sp, -64
        sw      $ra, 16($sp)
        sw      $a0, 20($sp)
        sw      $a1, 24($sp)
        sw      $a2, 28($sp)
        sw      $a3, 32($sp)
        sw      $v0, 36($sp)
        sw      $v1, 40($sp)
        sw      $t0, 44($sp)
        sw      $t1, 48($sp)
        sw      $t6, 52($sp)
        sw      $t7, 56($sp)
        sw      $t8, 60($sp)
        jal     PS1SleepGroupSetC
        move    $a0, $t4                # (delay) g
        lw      $ra, 16($sp)
        lw      $a0, 20($sp)
        lw      $a1, 24($sp)
        lw      $a2, 28($sp)
        lw      $a3, 32($sp)
        lw      $v0, 36($sp)
        lw      $v1, 40($sp)
        lw      $t0, 44($sp)
        lw      $t1, 48($sp)
        lw      $t6, 52($sp)
        lw      $t7, 56($sp)
        lw      $t8, 60($sp)
        addiu   $sp, $sp, 64
        lui     $t9, %hi(_ZN4RSDK7ps1ScanE)
        addiu   $t9, $t9, %lo(_ZN4RSDK7ps1ScanE)
.Lclear:
        bnez    $t1, .Lnext
        sw      $zero, 56($t0)          # (delay) inRange = false
        srl     $t2, $a2, 5             # a blank slot leaves the live bits
        sll     $t2, $t2, 2
        addu    $t2, $v1, $t2
        lw      $t3, 0($t2)
        li      $t4, 1
        sllv    $t4, $t4, $a2           # (shift by s & 31)
        nor     $t4, $zero, $t4
        and     $t3, $t3, $t4
        sw      $t3, 0($t2)
.Lnext:
        addiu   $a0, $a0, 4
        b       .Lloop
        addiu   $a2, $a2, 1             # (delay)

.Lin:                                   # in range: the pass decides it (PS1_SCAN_RET: grpOk = false)
        sb      $zero, 80($t9)
.Lretp:
        jr      $ra
        move    $v0, $a0                # (delay)
.Lretpend:
        jr      $ra
        move    $v0, $a1                # (delay)

.Lreset:                                # the camera offset changed: every group wakes (C's first lines)
        sw      $zero, 0($v0)
        sw      $zero, 4($v0)
        sw      $zero, 8($v0)
        sw      $zero, 12($v0)
        sw      $zero, 16($v0)
        sw      $zero, 20($v0)
        sw      $zero, 24($v0)
        sw      $zero, 28($v0)
        sw      $zero, 32($v0)
        sw      $zero, 36($v0)
        sw      $t6, 64($t9)
        b       .Lgo
        sw      $t1, 68($t9)            # (delay)
        .size PS1ScanSleepAsm, .-PS1ScanSleepAsm
