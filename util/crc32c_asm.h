//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under both the GPLv2 (found in the
//  COPYING file in the root directory) and Apache 2.0 License
//  (found in the LICENSE.Apache file in the root directory).

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

extern uint32_t crc32c_fusion_p8_c10_asm(const char* BUF, uint64_t LEN,
                                          uint32_t wCRC);

#ifdef __cplusplus
}
#endif
