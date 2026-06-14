/**LB-MIT
 *
 * MIT License
 *
 * Copyright (c) 2026 Till Straumann
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 **LE-MIT*/

#pragma once

#include <sys/types.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

struct FWInfo;
struct Flash;

/* progress state flags */
#define FLASH_CHECK_ERASED 1
#define FLASH_CHECK_VERIFY 2
#define FLASH_PROGRAM      4
#define FLASH_ERASE        8 

typedef int (*FlashProgress)(void *flash, void *closure, int flag, unsigned addr, unsigned remain);

int
flash_write_from_file(struct FWInfo *fw, const char *filename, unsigned flashAddr, FlashProgress progress, void *progressState);

int
flash_read_to_file(struct FWInfo *fw, const char *filename, unsigned flashAddr, unsigned size);

typedef struct {
	int    iter;
	FILE  *fp;
} FlashStdioProgressData;

void
flash_stdio_progress_data_init(FlashStdioProgressData *pd);

int
flash_stdio_progress(void *flash, void *closure, int flag, unsigned addr, unsigned remain);

#ifdef __cplusplus
}
#endif
