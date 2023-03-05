/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2023 Amol Surati */

#include <prv/parser.h>

#include <pub/system.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * on/off wrt the grammar parameters received by the production whose rhs
 * settings are represented by this structure.
 *
 * flags |= on (bits_on() | bits_on() | ...)
 * flags &= off (bits_off() & bits_off() & ...)
 */
#define RHS_ON_NONE		0
#define RHS_ON_ALL		((size_t)-1)
#define RHS_OFF_NONE	((size_t)-1)
#define RHS_OFF_ALL		0

struct rhs_settings {
	enum token_type	type;
	size_t		on;
	size_t		off;
};

static const
struct rhs_settings g_assign_expr_rhs[] = {
	/* Must keep LHS_EXPR first, since that is checked first. */
	{TOKEN_LHS_EXPR,			RHS_ON_NONE, bits_off(GP_IN)},

	{TOKEN_COND_EXPR,			RHS_ON_NONE, RHS_OFF_NONE},
	{TOKEN_YIELD_EXPR,			RHS_ON_NONE, bits_off(GP_YIELD)},
	{TOKEN_ARROW_FUNC,			RHS_ON_NONE, RHS_OFF_NONE},
	{TOKEN_ASYNC_ARROW_FUNC,	RHS_ON_NONE, RHS_OFF_NONE},
};

static const
struct rhs_settings g_script_body_rhs[] = {
	{
		TOKEN_STMT_LIST,
		RHS_ON_NONE,
		bits_off(GP_YIELD) & bits_off(GP_AWAIT) & bits_off(GP_RETURN)
	},
};

static const
struct rhs_settings g_stmt_rhs[] = {
	{TOKEN_BLOCK_STMT,		RHS_ON_NONE, RHS_OFF_NONE},
	{TOKEN_VAR_STMT,		RHS_ON_NONE, bits_off(GP_RETURN)},
	{TOKEN_EMPTY_STMT,		RHS_ON_NONE, RHS_OFF_ALL},
	{TOKEN_EXPR_STMT,		RHS_ON_NONE, bits_off(GP_RETURN)},
	{TOKEN_IF_STMT,			RHS_ON_NONE, RHS_OFF_NONE},
	{TOKEN_BREAKABLE_STMT,	RHS_ON_NONE, RHS_OFF_NONE},
	{TOKEN_CONTINUE_STMT,	RHS_ON_NONE, bits_off(GP_RETURN)},
	{TOKEN_BREAK_STMT,		RHS_ON_NONE, bits_off(GP_RETURN)},
	{TOKEN_RETURN_STMT,		RHS_ON_NONE, bits_off(GP_RETURN)},
	{TOKEN_WITH_STMT,		RHS_ON_NONE, RHS_OFF_NONE},
	{TOKEN_LABELLED_STMT,	RHS_ON_NONE, RHS_OFF_NONE},
	{TOKEN_THROW_STMT,		RHS_ON_NONE, bits_off(GP_RETURN)},
	{TOKEN_TRY_STMT,		RHS_ON_NONE, RHS_OFF_NONE},
	{TOKEN_DEBUGGER_STMT,	RHS_ON_NONE, RHS_OFF_ALL},
};
/*******************************************************************/
int parse_node_new(enum token_type type,
				   struct parse_node **out)
{
	int err;
	struct parse_node *node;

	*out = NULL;
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

	if (this == NULL)
		return ERR_SUCCESS;

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
/*
 * If it returns an error, *out is guaranteed to be NULL.
 * Explicit node deletion is needed when the success status is ignored.
 * If success is converted to err and returned, the child is deleted by the
 * function.
 */
static
int parser_parse(struct parser *this,
				 enum token_type type,
				 size_t flags,
				 size_t *q_pos,
				 struct parse_node **out)
{
	int err;
	size_t i, pos, cooked_len;
	struct parse_node *node, *child;
	const struct rhs_settings *rhs;
	const struct token *token;
	const char16_t *cooked;
	const size_t in_pos = *q_pos;
	const int in_flags = flags;

	*out = child = NULL;

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
	case TOKEN_VAR:
		/* These are all reserved literals. */
		err = parser_get_token(this, q_pos, &token);
		if (err)
			break;

		err = ERR_NO_MATCH;
		if (token_type(token) != type ||
			!token_is_reserved_literal(token))
			break;
		err = ERR_SUCCESS;
		break;
	case TOKEN_LEFT_BRACE:
	case TOKEN_RIGHT_BRACE:
	case TOKEN_SEMI_COLON:
	case TOKEN_COMMA:
	case TOKEN_EQUALS:
		/*
		 * Punctuations, etc. Always literals.
		 * If the token doesn't match, the q_pos is reset at the end, becoz
		 * we throw ERR_NO_MATCH.
		 */
		err = parser_get_token(this, q_pos, &token);
		if (err)
			break;

		if (token_type(token) != type)
			err = ERR_NO_MATCH;
		break;
		/*******************************************************************/
		/* Syntactical Grammar Non-Terminals */
	case TOKEN_IDENTIFIER_NAME:
		err = parser_get_token(this, q_pos, &token);
		if (err)
			break;

		err = ERR_NO_MATCH;
		if (!token_is_identifier_name(token))
			break;

		if (token_is_reserved_word(token)) {
			parse_node_delete(node);
			err = parse_node_new(token_type(token), &node);
		} else {
			cooked = token_cooked(token, &cooked_len);
			assert(cooked);
			parse_node_set_cooked(node, cooked, cooked_len);
			err = ERR_SUCCESS;
		}
		break;
	case TOKEN_SCRIPT:
		type = TOKEN_SCRIPT_BODY;
		err = parser_parse(this, type, flags, q_pos, &child);
		break;
	case TOKEN_SCRIPT_BODY:
		rhs = g_script_body_rhs;
		type = rhs[0].type;
		flags |= rhs[0].on;
		flags &= rhs[0].off;
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
		}
		break;
	case TOKEN_STMT_LIST_ITEM:
		type = TOKEN_STMT;
		err = parser_parse(this, type, flags, q_pos, &child);
		if (err == ERR_NO_MATCH) {
			type = TOKEN_DECL;
			flags &= bits_off(GP_RETURN);
			err = parser_parse(this, type, flags, q_pos, &child);
		}
		break;
		/*******************************************************************/
	case TOKEN_ASSIGN_EXPR: {
		static const enum token_type g_ops[] = {
			TOKEN_EQUALS,
			TOKEN_MUL_EQUALS,
			TOKEN_DIV_EQUALS,
			TOKEN_MOD_EQUALS,
			TOKEN_PLUS_EQUALS,
			TOKEN_MINUS_EQUALS,
			TOKEN_EXP_EQUALS,
			TOKEN_SHL_EQUALS,
			TOKEN_SHR_EQUALS,
			TOKEN_SAR_EQUALS,	/* Signed SHR */
			TOKEN_LOGICAL_OR_EQUALS,
			TOKEN_LOGICAL_AND_EQUALS,
			TOKEN_BITWISE_AND_EQUALS,
			TOKEN_BITWISE_XOR_EQUALS,
			TOKEN_BITWISE_OR_EQUALS,
			TOKEN_COALESCE_EQUALS,
		};

		/*
		 * LHS_Expr occurs both as a child and grand(n)-child. Hence, check
		 * LHS_Expr first
		 */
		rhs = g_assign_expr_rhs;
		type = rhs[0].type;	/* LHS_EXPR */
		assert(type == TOKEN_LHS_EXPR);
		flags |= rhs[0].on;
		flags &= rhs[0].off;

		err = parser_parse(this, type, flags, q_pos, &child);
		if (!err) {
			/* If LHS_EXPR was parsed, check for operators next */
			parse_node_add_child(node, child);

			for (i = 0; i < ARRAY_SIZE(g_ops); ++i) {
				type = g_ops[i];
				err = parser_parse(this, type, 0, q_pos, &child);
				/* if no match continue, else break */
				if (err != ERR_NO_MATCH)
					break;
			}

			/* If there was an error, reparse. Else, continue. */
			if (!err) {
				parse_node_add_child(node, child);
				type = TOKEN_ASSIGN_EXPR;
				flags = in_flags;
				err = parser_parse(this, type, flags, q_pos, &child);
				/*
				 * The end-of-func will deal with the child depending on the
				 * error.
				 */
				break;
			}

			/* Reparse */
			*q_pos = in_pos;
		}

		/*
		 * TODO: This loop occurs multiple times; simplify by making it a
		 * common function.
		 */
		flags = in_flags;

		/* Start from 1, since 0 is done above. */
		for (i = 1; i < ARRAY_SIZE(g_assign_expr_rhs); ++i, flags = in_flags) {
			type = rhs[i].type;

			if (type == TOKEN_YIELD_EXPR && !bits_get(flags, GP_YIELD))
				continue;

			flags |= rhs[i].on;
			flags &= rhs[i].off;

			/*
			 * If success, break. The end-of-func will take care of the child
			 * insertion
			 */
			err = parser_parse(this, type, flags, q_pos, &child);
			if (!err)
				break;
			else if (err == ERR_NO_MATCH)
				continue;
			else
				break;
		}
		break;
	}
	case TOKEN_INITIALIZER:
		type = TOKEN_EQUALS;
		err = parser_parse(this, type, 0, q_pos, &child);
		if (err)
			break;

		parse_node_delete(child);
		type = TOKEN_ASSIGN_EXPR;
		err = parser_parse(this, type, flags, q_pos, &child);
		break;
		/*******************************************************************/
	case TOKEN_BINDING_IDENTIFIER:
		type = TOKEN_IDENTIFIER_NAME;
		err = parser_parse(this, type, 0, q_pos, &child);
		if (err)
			break;

		if (parse_node_is_reserved_word(child)) {
			/* This is a reserved word. Fail */
			parse_node_delete(child);
			child = NULL;
			err = ERR_NO_MATCH;	/* TODO: Convert to SyntaxError */
		}
		/* TODO: Check for await and yield errors */
		break;
		/*******************************************************************/
	case TOKEN_VAR_DECL: {
		bool is_ident;

		type = TOKEN_BINDING_IDENTIFIER;
		flags = in_flags & bits_off(GP_IN);
		err = parser_parse(this, type, flags, q_pos, &child);
		if (err == ERR_NO_MATCH) {
			type = TOKEN_BINDING_PATTERN;
			flags = in_flags & bits_off(GP_IN);
			err = parser_parse(this, type, flags, q_pos, &child);
		}
		if (err)
			break;

		/* Initializer is optional for Identifier */
		parse_node_add_child(node, child);
		is_ident = type == TOKEN_BINDING_IDENTIFIER;

		flags = in_flags;
		type = TOKEN_INITIALIZER;
		err = parser_parse(this, type, flags, q_pos, &child);
		if (err && is_ident)
			err = ERR_SUCCESS;
		break;
	}
	case TOKEN_VAR_DECL_LIST:
		while (true) {
			type = TOKEN_VAR_DECL;
			err = parser_parse(this, type, flags, q_pos, &child);
			if (err == ERR_END_OF_FILE) {
				if (parse_node_has_children(node))
					err = ERR_SUCCESS;
				break;
			} else if (err) {
				break;
			}

			parse_node_add_child(node, child);

			/* Check for comma. If exists, continue, else break. */
			type = TOKEN_COMMA;
			err = parser_parse(this, type, 0, q_pos, &child);
			if (err) {
				/* We have at least a proper var decl list */
				err = ERR_SUCCESS;
				break;
			}
			/* Delete the COMMA node */
			parse_node_delete(child);
		}
		break;
	case TOKEN_VAR_STMT: {
		static const enum token_type g_ends[] = {
			TOKEN_NEW_LINE,
			TOKEN_SEMI_COLON,
		};

		type = TOKEN_VAR;
		err = parser_parse(this, type, 0, q_pos, &child);
		if (err)
			break;
		parse_node_delete(child);	/* Delete the VAR node */

		type = TOKEN_VAR_DECL_LIST;
		flags |= bits_on(GP_IN);
		err = parser_parse(this, type, flags, q_pos, &child);
		if (err)
			break;

		parse_node_add_child(node, child);

		/*
		 * SEMI_COLON, EOF and NL end the Var_Stmt.
		 *
		 * Check NL first. The reason is:
		 *	var a = 123 <nl> ;
		 * should treat the ; as an empty statement instead of as part of the
		 * var stmt.
		 */

		for (i = 0; i < ARRAY_SIZE(g_ends); ++i) {
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
	}
		/*******************************************************************/
	case TOKEN_BLOCK:
		type = TOKEN_LEFT_BRACE;
		err = parser_parse(this, type, 0, q_pos, &child);
		if (err)
			break;
		parse_node_delete(child);

		/* stmt_list is optional for block_stmt */
		type = TOKEN_RIGHT_BRACE;
		err = parser_parse(this, type, 0, q_pos, &child);
		if (!err) {
			/* Empty Block Stmt */
			parse_node_delete(child);
			child = NULL;
			break;
		}

		/* Non-Empty Block. */
		type = TOKEN_STMT_LIST;
		err = parser_parse(this, type, flags, q_pos, &child);
		break;
	case TOKEN_BLOCK_STMT:
		type = TOKEN_BLOCK;
		err = parser_parse(this, type, flags, q_pos, &child);
		break;
		/*******************************************************************/
	case TOKEN_STMT:
		rhs = g_stmt_rhs;
		for (i = 0; i < ARRAY_SIZE(g_stmt_rhs); ++i, flags = in_flags) {
			type = rhs[i].type;

			// check flags before modifying
			if (type == TOKEN_RETURN_STMT && !bits_get(flags, GP_RETURN))
				continue;

			flags |= rhs[i].on;
			flags &= rhs[i].off;

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
	size_t q_pos;
	int err;

	q_pos = 0;
	err = parser_parse(this, TOKEN_SCRIPT, 0, &q_pos, &this->root);
	if (err == ERR_END_OF_FILE)
		err = ERR_SUCCESS;
	return err;
}
