//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).
//
// Pre-computed constants for crc32c polynomial multiplication on AArch64.
// Referenced by crc32c_p8c10.S via GOT.

.section .rodata
.balign 16

.globl crc32c_p8c10_clmul_const
crc32c_p8c10_clmul_const:
    // Placeholder constants - replace with real pmull constants
    .quad 0x0000000000000000
    .quad 0x0000000000000000

