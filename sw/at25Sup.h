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

#ifdef __cplusplus
extern "C" {
#endif

struct AT25Flash;
struct FWInfo;

typedef struct AT25Flash AT25Flash;
typedef struct FWInfo    FWInfo;

AT25Flash *
at25_open(FWInfo *fw, unsigned instance);

/* Alternative 'open' method which returns 0 on success and
 * negative errno-status on error.
 */
int
at25_open1(FWInfo *fw, AT25Flash **flashp, unsigned instance);

void
at25_close(AT25Flash *flash);

size_t
at25_get_block_size(AT25Flash *flash);

size_t
at25_get_size_bytes(AT25Flash *flash);

int
at25_spi_read(AT25Flash *flash, unsigned addr, uint8_t *rbuf, size_t len);

int
at25_print_id(AT25Flash *flash);

int64_t
at25_id(AT25Flash *flash);

#define AT25_ST_BUSY       0x01
#define AT25_ST_WEL        0x02
#define AT25_ST_EPE        0x20

int
at25_status(AT25Flash *flash);

int
at25_cmd_2(AT25Flash *flash, uint8_t cmd, int arg);

int
at25_cmd_1(AT25Flash *flash, uint8_t cmd);

int
at25_write_ena(AT25Flash *flash);

int
at25_write_dis(AT25Flash *flash);

int
at25_global_unlock(AT25Flash *flash);

int
at25_global_lock(AT25Flash *flash);

int
at25_status_poll(AT25Flash *flash);

int
at25_block_erase(AT25Flash *flash, unsigned addr, size_t sz);

/* 'check' flags for at25_prog */
#define AT25_CHECK_ERASED 1
#define AT25_CHECK_VERIFY 2
#define AT25_EXEC_PROG    4
/* 'erase' is set by at25_area_erase when executing the AT25Progress callback */
#define AT25_ERASE        8

/* Return nonzero to abort operation.
 * The 'progress' call back is called
 * once before each operation and then
 * after successfully processing each 
 * block.
 */

typedef int (*AT25Progress)(AT25Flash *flash, void *closure, int flag, unsigned addr, unsigned remain);

int
at25_area_erase(AT25Flash *flash, unsigned flashAddr, size_t flashSize, AT25Progress progress, void *userData);

int
at25_prog(AT25Flash *flash, unsigned addr, const uint8_t *data, size_t len, int flags, AT25Progress progress, void *userData);

/* Send soft reset sequence */
int
at25_reset(AT25Flash *flash);

/* Send wakeup from deep sleep */
int
at25_resume_updwn(AT25Flash *flash);

#ifdef __cplusplus
}
#endif
