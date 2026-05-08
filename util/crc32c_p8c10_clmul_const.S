// Dummy file for pre-computed constants used in crc32c polynomial multiplication on AArch64.
// Referenced by crc32c_p8c10.S via GOT.

.section .rodata
.balign 16

.globl crc32c_p8c10_clmul_const
crc32c_p8c10_clmul_const:
    // Placeholder constants - replace with real pmull constants
    .quad 0x0000000000000000
    .quad 0x0000000000000000

