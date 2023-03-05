/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2023 Amol Surati */

#ifndef PUB_BITS_H
#define PUB_BITS_H

/*
 * Although these are macros, they are usually used with flags which are also
 * defined as macros. To reduce the all-caps noise, keep these in lower-case.
 */

/* a is a bit position. */
#define align_mask(a)			((1ull << (a)) - 1)
#define is_aligned(v, a)		(((v) & align_mask(a)) == 0)
#define align_down(v, a)		((v) & ~align_mask(a))
#define align_up(v, a)			align_down((v) + align_mask(a), a)

#define bits_mask(f)			((1ull << f##_BITS) - 1)
#define bits_set(f, v)			(((v) & bits_mask(f)) << f##_POS)
#define bits_get(v, f)			(((v) >> f##_POS) & bits_mask(f))
#define bits_push(f, v)			((v) & (bits_mask(f) << f##_POS))
#define bits_pull(v, f)			bits_push(f, v)
#define bits_on(f)				(bits_mask(f) << f##_POS)
#define bits_off(f)				~bits_on(f)
#endif
