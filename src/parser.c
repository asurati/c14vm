/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2023 Amol Surati */

#include <prv/parser.h>

#include <pub/system.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static const
enum token_type g_stmt_rhs[] = {
	TOKEN_BLOCK_STMT,
	TOKEN_VAR_STMT,
	TOKEN_EMPTY_STMT,
	TOKEN_EXPR_STMT,
	TOKEN_IF_STMT,
	TOKEN_BREAKABLE_STMT,
	TOKEN_CONTINUE_STMT,
	TOKEN_BREAK_STMT,
	TOKEN_RETURN_STMT,
	TOKEN_WITH_STMT,
	TOKEN_LABELLED_STMT,
	TOKEN_THROW_STMT,
	TOKEN_TRY_STMT,
	TOKEN_DEBUGGER_STMT,
};
/*******************************************************************/
int parse_node_new(enum token_type type,
				   struct parse_node **out)
{
	int err;
	struct parse_node *node;

	err = ERR_NO_MEMORY;
	node = calloc(1, sizeof(*node));
	if (node == NULL)
		goto err0;

	list_init(&node->nodes);
	node->type = type;
	*out = node;
	err = ERR_SUCCESS;
err0:
	return err;
}

int parse_node_delete(struct parse_node *this)
{
	struct list_entry *e;
	struct parse_node *node;

	list_for_each_del(e, &this->nodes) {
		node = list_entry(e, struct parse_node, entry);
		parse_node_delete(node);
	}
	free((void *)this->cooked);
	return ERR_SUCCESS;
}
/*******************************************************************/
int parser_new(const char16_t *src,
			   size_t src_len,
			   struct parser **out)
{
	int err;
	struct parser *parser;
	struct scanner *scanner;

	err = scanner_new(src, src_len, &scanner);
	if (err)
		goto err0;

	err = ERR_NO_MEMORY;
	parser = calloc(1, sizeof(*parser));
	if (parser == NULL)
		goto err1;

	parser->scanner = scanner;
	*out = parser;
	return ERR_SUCCESS;
err1:
	scanner_delete(scanner);
err0:
	return err;
}

int parser_delete(struct parser *this)
{
	size_t i;

	for (i = 0; i < this->num_tokens; ++i)
		token_delete((struct token *)this->tokens[i]);
	free(this->tokens);
	parse_node_delete(this->root);
	scanner_delete(this->scanner);
	free(this);
	return ERR_SUCCESS;
}
/*******************************************************************/
static
int parser_get_token(struct parser *this,
					 size_t *q_pos,
					 const struct token **out)
{
	int err;
	size_t num_tokens, pos;
	const struct token *token, **tokens;

	pos = *q_pos;
	num_tokens = this->num_tokens;
	tokens = this->tokens;

	if (pos > num_tokens)
		return ERR_INVALID_PARAMETER;

	if (pos == num_tokens) {
		err = scanner_get_next_token(this->scanner, &token);
		if (err)
			return err;
		tokens = realloc(tokens, (num_tokens + 1) * sizeof(*tokens));
		if (tokens == NULL)
			return ERR_NO_MEMORY;
		tokens[num_tokens++] = token;
		this->num_tokens = num_tokens;
		this->tokens = tokens;
	}
	*out = this->tokens[pos++];
	*q_pos = pos;
	return ERR_SUCCESS;
}
/*******************************************************************/
static
int parser_parse(struct parser *this,
				 enum token_type type,
				 int flags,
				 size_t *q_pos,
				 struct parse_node **out)
{
	int err;
	size_t i, pos;
	struct parse_node *node, *child;
	const struct token *token;
	const size_t in_pos = *q_pos;
	const int in_flags = flags;

	*out = child = NULL;

	/* Only create a node if the caller requested for it */
	err = parse_node_new(type, &node);
	if (err)
		return err;

	switch (type) {
		/*******************************************************************/
		/* Syntactical Grammar Terminals */
	case TOKEN_NEW_LINE:
		/* Find a line-terminator before a token. */
		pos = *q_pos;
		err = parser_get_token(this, q_pos, &token);
		if (err)
			break;
		else if (token_has_new_line_pfx(token))
			*q_pos = pos;	/* Restore since the token must remain in q */
		else
			err = ERR_NO_MATCH;
		break;
	case TOKEN_LEFT_BRACE:
	case TOKEN_RIGHT_BRACE:
	case TOKEN_SEMI_COLON:
	case TOKEN_VAR:
		/*
		 * If the token doesn't match, the q_pos is reset at the end, becoz
		 * we throw ERR_NO_MATCH.
		 *
		 * All of these are literals.
		 */
		err = parser_get_token(this, q_pos, &token);
		printf("%s: type %d, want %d\n", __func__, token_type(token), type);
		if (!err && token_type(token) == type &&
			!token_has_unc_esc(token) &&
			!token_has_hex_esc(token))
			break;
		else if (!err)
			err = ERR_NO_MATCH;
		break;
		/*******************************************************************/
		/* Syntactical Grammar Non-Terminals */
	case TOKEN_SCRIPT:
		type = TOKEN_SCRIPT_BODY;
		err = parser_parse(this, type, flags, q_pos, &child);
		if (err == ERR_END_OF_FILE)
			err = ERR_SUCCESS;
		break;
	case TOKEN_SCRIPT_BODY:
		type = TOKEN_STMT_LIST;
		flags &= bits_off(GP_YIELD);
		flags &= bits_off(GP_AWAIT);
		flags &= bits_off(GP_RETURN);
		err = parser_parse(this, type, flags, q_pos, &child);
		break;
		/*******************************************************************/
	case TOKEN_STMT_LIST:
		type = TOKEN_STMT_LIST_ITEM;
		while (true) {
			err = parser_parse(this, type, flags, q_pos, &child);
			if (err)
				break;
			parse_node_add_child(node, child);
			child = NULL;
		}
		break;
	case TOKEN_STMT_LIST_ITEM:
		type = TOKEN_STMT;
		err = parser_parse(this, type, flags, q_pos, &child);
		if (err == ERR_NO_MATCH) {
			type = TOKEN_DECL;
			*q_pos = in_pos;
			flags &= bits_off(GP_RETURN);
			err = parser_parse(this, type, flags, q_pos, &child);
		}
		break;
		/*******************************************************************/
	case TOKEN_VAR_STMT:
		type = TOKEN_VAR;
		err = parser_parse(this, type, 0, q_pos, &child);
		if (err)
			break;
		parse_node_delete(child);

		type = TOKEN_VAR_DECL_LIST;
		flags |= bits_on(GP_IN);
		err = parser_parse(this, type, flags, q_pos, &child);
		if (err)
			break;

		parse_node_add_child(node, child);
		child = NULL;

		/*
		 * SEMI_COLON, EOF and NL end the Var_Stmt.
		 *
		 * Check NL first. The reason is:
		 *	var a = 123 <nl> ;
		 * should treat the ; as an empty statement instead of as part of the
		 * var stmt.
		 */

		for (i = 0; i < 2; ++i) {
			static const enum token_type g_ends[] = {
				TOKEN_NEW_LINE,
				TOKEN_SEMI_COLON,
			};
			type = g_ends[i];
			err = parser_parse(this, type, 0, q_pos, &child);
			if (!err) {
				parse_node_delete(child);
				child = NULL;
				break;
			} else if (err == ERR_END_OF_FILE) {
				err = ERR_SUCCESS;
				break;
			} else if (err == ERR_NO_MATCH) {
				continue;
			} else {
				break;
			}
		}
		break;
		/*******************************************************************/
	case TOKEN_BLOCK:
		type = TOKEN_LEFT_BRACE;
		err = parser_parse(this, type, 0, q_pos, &child);
		if (err)
			break;
		parse_node_delete(child);

		/* stmt_list is optional for block_stmt */
		pos = *q_pos;	/* Save pos */
		type = TOKEN_RIGHT_BRACE;
		err = parser_parse(this, type, 0, q_pos, &child);
		if (!err) {
			/* Empty Block Stmt */
			parse_node_delete(child);
			child = NULL;
			break;
		}

		/* Non-Empty Block. */
		*q_pos = pos;
		type = TOKEN_STMT_LIST;
		err = parser_parse(this, type, flags, q_pos, &child);
		break;
	case TOKEN_BLOCK_STMT:
		type = TOKEN_BLOCK;
		err = parser_parse(this, type, flags, q_pos, &child);
		break;
		/*******************************************************************/
	case TOKEN_STMT:
		for (i = 0; i < ARRAY_SIZE(g_stmt_rhs);
			 ++i, *q_pos = in_pos, flags = in_flags) {
			type = g_stmt_rhs[i];

			// check flags before modifying
			if (type == TOKEN_RETURN_STMT && !bits_get(flags, GP_RETURN))
				continue;

			switch (type) {
			case TOKEN_EMPTY_STMT:
			case TOKEN_DEBUGGER_STMT:
				flags = 0;
				break;
			case TOKEN_VAR_STMT:
			case TOKEN_EXPR_STMT:
			case TOKEN_CONTINUE_STMT:
			case TOKEN_BREAK_STMT:
			case TOKEN_RETURN_STMT:
			case TOKEN_THROW_STMT:
				flags &= bits_off(GP_RETURN);
				break;
			default:
				break;
			}

			err = parser_parse(this, type, flags, q_pos, &child);
			if (!err)
				break;
			else if (err == ERR_NO_MATCH)
				continue;
			else
				break;
		}
		break;
	default:
		printf("%s: unsup %d\n", __func__, type);
		if (type >= TOKEN_SCRIPT)
			printf("%s: unsup non-term %d\n", __func__, type - TOKEN_SCRIPT);
		exit(0);
	}

	if (!err) {
		if (child)
			parse_node_add_child(node, child);
		*out = node;
	} else {
		/* child is not inserted into node yet.*/
		if (child)
			parse_node_delete(child);
		parse_node_delete(node);

		/* rest the q_pos upon error. */
		*q_pos = in_pos;
	}
	return err;
}

int parser_parse_script(struct parser *this)
{
	size_t q_pos = 0;
	return parser_parse(this, TOKEN_SCRIPT, 0, &q_pos, &this->root);
}
