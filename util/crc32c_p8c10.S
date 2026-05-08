// Dummy file for AArch64 CRC32C implementation using ARMv8 CRC32 + pmull instructions.
//
// uint32_t crc32c_fusion_p8_c10_asm(const char* BUF, uint64_t LEN,
//                                    uint32_t wCRC);
// x0 = BUF, x1 = LEN, x2 = wCRC

.text
.globl crc32c_fusion_p8_c10_asm
.type crc32c_fusion_p8_c10_asm, @function
.balign 16

crc32c_fusion_p8_c10_asm:
    mvn     w0, w2                // crc ^= 0xffffffff
    cbz     x1, .Ldone

    // Byte-by-byte alignment to 8 bytes
    tst     x0, #7
    b.eq    .Laligned_8

.Lalign:
    ldrb    w3, [x0]
    crc32cb w0, w0, w3
    add     x0, x0, #1
    sub     x1, x1, #1
    cbz     x1, .Ldone
    tst     x0, #7
    b.ne    .Lalign

.Laligned_8:
    // Process 64 bytes (8 x 8) per loop
    cmp     x1, #64
    b.lo    .Llt64

.Lloop64:
    ldp     x3, x4, [x0]
    ldp     x5, x6, [x0, #16]
    crc32cx w0, w0, x3
    ldp     x3, x4, [x0, #32]
    crc32cx w0, w0, x4
    crc32cx w0, w0, x5
    crc32cx w0, w0, x6
    ldp     x5, x6, [x0, #48]
    crc32cx w0, w0, x3
    crc32cx w0, w0, x4
    crc32cx w0, w0, x5
    crc32cx w0, w0, x6
    add     x0, x0, #64
    sub     x1, x1, #64
    cmp     x1, #64
    b.hs    .Lloop64

.Llt64:
    // Process 8 bytes at a time
.Lloop8:
    cmp     x1, #8
    b.lo    .Ltail
    ldr     x3, [x0]
    crc32cx w0, w0, x3
    add     x0, x0, #8
    sub     x1, x1, #8
    b       .Lloop8

.Ltail:
    cbz     x1, .Ldone

    tbz     x1, #2, .Ltail_hw
    ldr     w3, [x0]
    crc32cw w0, w0, w3
    add     x0, x0, #4

.Ltail_hw:
    tbz     x1, #1, .Ltail_b
    ldrh    w3, [x0]
    crc32ch w0, w0, w3
    add     x0, x0, #2

.Ltail_b:
    tbz     x1, #0, .Ldone
    ldrb    w3, [x0]
    crc32cb w0, w0, w3

.Ldone:
    mvn     w0, w0                // crc ^= 0xffffffff
    ret

.size crc32c_fusion_p8_c10_asm, .-crc32c_fusion_p8_c10_asm
