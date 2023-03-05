/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2023 Amol Surati */

#ifndef PRV_PARSER_H
#define PRV_PARSER_H

#include <pub/parser.h>

#include <prv/scanner.h>

#include <pub/list.h>

#define GP_SEP_POS				0
#define GP_YIELD_POS			1
#define GP_AWAIT_POS			2
#define GP_TAGGED_POS			3
#define GP_IN_POS				4
#define GP_RETURN_POS			5
#define GP_DEFAULT_POS			6
#define GP_N_POS				7
#define GP_UNICODE_MODE_POS		8

#define GP_SEP_BITS				1
#define GP_YIELD_BITS			1
#define GP_AWAIT_BITS			1
#define GP_TAGGED_BITS			1
#define GP_IN_BITS				1
#define GP_RETURN_BITS			1
#define GP_DEFAULT_BITS			1
#define GP_N_BITS				1
#define GP_UNICODE_MODE_BITS	1

#define PARSER_SIGN			\
	struct parser *this,	\
	int flags,				\
	size_t *q_pos,			\
	struct parse_node **out

struct parse_node {
	struct list_entry	entry;
	struct list_entry	nodes;
	const char16_t		*cooked;
	size_t				cooked_len;
	enum token_type		type;
};

int	parse_node_new(enum token_type type,
				   struct parse_node **out);
int	parse_node_delete(struct parse_node *this);

static inline
void parse_node_add_child(struct parse_node *this,
						  struct parse_node *child)
{
	list_add_tail(&this->nodes, &child->entry);
}

static inline
enum token_type parse_node_type(const struct parse_node *this)
{
	return this->type;
}

static inline
void parse_node_set_cooked(struct parse_node *this,
						   const char16_t *cooked,
						   size_t cooked_len)
{
	this->cooked = cooked;
	this->cooked_len = cooked_len;
}

struct parser {
	struct scanner		*scanner;
	const struct token	**tokens;
	size_t				num_tokens;
	struct parse_node	*root;
};
#endif
