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

#ifdef __cplusplus
extern "C" {
#endif

struct ScopeParams;
struct ScopePvt;

/* Check if JSON support is available
 * RETURN: 0 if supported, -ENOTSUP if
 *         unsupported.
 */
int
scope_json_supported();

/* Save current settings to JSON file.
 * RETURN 0 on success, negative error status on error.
 */
int
scope_json_save(ScopePvt *pvt, const char * filename, const ScopeParams *settings);

/* Load current settings from JSON file.
 * RETURN 0 on success, negative error status on error.
 */
int
scope_json_load(ScopePvt *pvt, const char * filename, ScopeParams *settings);

#ifdef __cplusplus
};
#endif


