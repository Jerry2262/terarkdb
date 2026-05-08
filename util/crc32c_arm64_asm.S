//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).
//
// AArch64 CRC32C implementation using ARMv8 CRC32 instructions.
//
// uint32_t crc32c_arm64(uint32_t crc, const unsigned char *buf,
//                       unsigned long len);
// x0 = crc, x1 = buf, x2 = len

.text
.globl crc32c_arm64
.type crc32c_arm64, @function
.balign 16

crc32c_arm64:
    mvn     w0, w0                // crc ^= 0xffffffff
    cbz     x2, .Ldone

    // Align buf to 8 bytes for the main doubleword loop
    tst     x1, #7
    b.eq    .Laligned_8

    // Byte-by-byte until 8-byte aligned
.Lalign:
    ldrb    w3, [x1]
    crc32cb w0, w0, w3
    add     x1, x1, #1
    sub     x2, x2, #1
    cbz     x2, .Ldone
    tst     x1, #7
    b.ne    .Lalign

.Laligned_8:
    // Process 64 bytes (8 x 8) per loop iteration
    cmp     x2, #64
    b.lo    .Lless_than_64

.Lloop64:
    ldp     x3, x4, [x1]
    ldp     x5, x6, [x1, #16]
    crc32cx w0, w0, x3
    ldp     x3, x4, [x1, #32]
    crc32cx w0, w0, x4
    crc32cx w0, w0, x5
    crc32cx w0, w0, x6
    ldp     x5, x6, [x1, #48]
    crc32cx w0, w0, x3
    crc32cx w0, w0, x4
    crc32cx w0, w0, x5
    crc32cx w0, w0, x6
    add     x1, x1, #64
    sub     x2, x2, #64
    cmp     x2, #64
    b.hs    .Lloop64

.Lless_than_64:
    // Process 8 bytes at a time
.Lloop8:
    cmp     x2, #8
    b.lo    .Ltail
    ldr     x3, [x1]
    crc32cx w0, w0, x3
    add     x1, x1, #8
    sub     x2, x2, #8
    b       .Lloop8

.Ltail:
    // Process remaining bytes
    cbz     x2, .Ldone

    // Process word (4 bytes)
    tbz     x2, #2, .Ltail_halfword
    ldr     w3, [x1]
    crc32cw w0, w0, w3
    add     x1, x1, #4

.Ltail_halfword:
    // Process halfword (2 bytes)
    tbz     x2, #1, .Ltail_byte
    ldrh    w3, [x1]
    crc32ch w0, w0, w3
    add     x1, x1, #2

.Ltail_byte:
    // Process byte (1 byte)
    tbz     x2, #0, .Ldone
    ldrb    w3, [x1]
    crc32cb w0, w0, w3

.Ldone:
    mvn     w0, w0                // crc ^= 0xffffffff
    ret

.size crc32c_arm64, .-crc32c_arm64
