/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2023 Amol Surati */

#ifndef PUB_PARSER_H
#define PUB_PARSER_H

#include <uchar.h>

struct parser;

int	parser_new(const char16_t *src,
			   size_t src_len,
			   struct parser **out);
int	parser_delete(struct parser *this);
int	parser_parse_script(struct parser *this);
int	parser_parse_module(struct parser *this);
#endif
