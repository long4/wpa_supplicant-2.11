/*
 * SPDX-FileCopyrightText: Copyright (c) 2026-2030 OMNI CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: MIT
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef INCLUDED_OETH_TYPE_H
#define INCLUDED_OETH_TYPE_H
/**
 * @addtogroup typedef related info
 *
 * @brief typedefs that indicate size and signness
 * @{
 */
/* Following added to avoid misraC 4.6
 * Here we are defining intermediate type
 */

#ifndef OETH_TYPES_DEFINED
#define OETH_TYPES_DEFINED
#ifndef u8
typedef unsigned char		u8;
#endif
#ifndef u16
typedef unsigned short		u16;
#endif
#ifndef u32
typedef unsigned int		u32;
#endif
#ifndef u64
typedef uint64_t		u64;
#endif
#ifndef s16
typedef short			s16;
#endif
#ifndef s32
typedef int			s32;
#endif
#ifndef s64
typedef int64_t			s64;
#endif
#endif

/** @} */

#endif /* INCLUDED_OETH_TYPE_H */

