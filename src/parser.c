/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2023 Amol Surati */

#include <prv/parser.h>

#include <pub/system.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static const
enum token_type g_assign_expr_ops[] = {
	TOKEN_EQUALS,
	TOKEN_MUL_EQUALS,
	TOKEN_DIV_EQUALS,
	TOKEN_MOD_EQUALS,
	TOKEN_PLUS_EQUALS,
	TOKEN_MINUS_EQUALS,
	TOKEN_EXP_EQUALS,
	TOKEN_SHL_EQUALS,
	TOKEN_SHR_EQUALS,	/* Unsigned SHR */
	TOKEN_SAR_EQUALS,	/* Signed SHR */
	TOKEN_LOGICAL_OR_EQUALS,
	TOKEN_LOGICAL_AND_EQUALS,
	TOKEN_BITWISE_AND_EQUALS,
	TOKEN_BITWISE_XOR_EQUALS,
	TOKEN_BITWISE_OR_EQUALS,
	TOKEN_COALESCE_EQUALS,
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
	bool is_ident, has_opt_chain;
	size_t i, pos, cooked_len;
	struct parse_node *node, *child;
	const struct token *token;
	const char16_t *cooked;
	const size_t in_pos = *q_pos;
	const int in_flags = flags;
	const enum token_type in_type = type;

	*out = child = NULL;

	err = parse_node_new(type, &node);
	if (err)
		return err;

	switch (type) {
		/* Syntactical Grammar Terminals */
		/*******************************************************************/
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
	case TOKEN_SUPER:
	case TOKEN_IMPORT:
	case TOKEN_THIS:
	case TOKEN_NEW:
	case TOKEN_NULL:
	case TOKEN_TRUE:
	case TOKEN_FALSE:
		/* These are all reserved literals. */
		err = parser_get_token(this, q_pos, &token);
		if (!err &&
			(token_type(token) != type || !token_is_reserved_literal(token)))
			err = ERR_NO_MATCH;
		break;
	case TOKEN_NUMBER:
	case TOKEN_STRING:
		/* fall-through */
	case TOKEN_LEFT_BRACE:
	case TOKEN_RIGHT_BRACE:
	case TOKEN_LEFT_BRACKET:
	case TOKEN_RIGHT_BRACKET:
	case TOKEN_SEMI_COLON:
	case TOKEN_COMMA:
	case TOKEN_EQUALS:
		/*
		 * Punctuations, etc. Always literals.
		 * If the token doesn't match, the q_pos is reset at the end, becoz
		 * we throw ERR_NO_MATCH.
		 */
		err = parser_get_token(this, q_pos, &token);
		if (!err && token_type(token) != type)
			err = ERR_NO_MATCH;
		break;
		/* Syntactical Grammar Non-Terminals */
		/*******************************************************************/
	case PRIVATE_IDENTIFIER:
		type = TOKEN_NUMBER_SIGN;
		err = parser_parse(this, type, 0, q_pos, &child);
		if (err)
			break;
		parse_node_delete(child);	/* Consume # */
		type = IDENTIFIER_NAME;
		err = parser_parse(this, type, 0, q_pos, &child);
		break;
	case IDENTIFIER_NAME:
		err = parser_get_token(this, q_pos, &token);
		if (err)
			break;

		err = ERR_NO_MATCH;
		if (!token_is_identifier_name(token))
			break;

		/* If possible, use non-cooked */
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
		/*******************************************************************/
	case SCRIPT:
		type = SCRIPT_BODY;
		err = parser_parse(this, type, flags, q_pos, &child);
		break;
	case SCRIPT_BODY:
		type = STATEMENT_LIST;
		flags &= bits_off(GP_YIELD),
			flags &= bits_off(GP_AWAIT),
			flags &= bits_off(GP_RETURN),
			err = parser_parse(this, type, flags, q_pos, &child);
		break;
		/*******************************************************************/
	case STATEMENT_LIST:	/* left-associative */
		type = STATEMENT_LIST_ITEM;
		while (true) {
			err = parser_parse(this, type, flags, q_pos, &child);
			if (err)
				break;
			parse_node_add_child(node, child);
		}
		break;
	case STATEMENT_LIST_ITEM:
		type = STATEMENT;
		err = parser_parse(this, type, flags, q_pos, &child);
		if (err == ERR_NO_MATCH) {
			type = DECLARATION;
			flags &= bits_off(GP_RETURN);
			err = parser_parse(this, type, flags, q_pos, &child);
		}
		break;
		/*******************************************************************/
	case ARRAY_EXPRESSION:
		type = TOKEN_LEFT_BRACKET;
		err = parser_parse(this, type, 0, q_pos, &child);
		if (err)
			break;
		parse_node_delete(child);

		type = EXPRESSION;
		err = parser_parse(this, type, 0, q_pos, &child);
		if (err)
			break;
		parse_node_add_child(node, child);

		type = TOKEN_RIGHT_BRACKET;
		err = parser_parse(this, type, 0, q_pos, &child);
		if (err)
			break;
		parse_node_delete(child);
		child = NULL;
		break;
		/*******************************************************************/
	case PRIMARY_EXPRESSION:
		type = TOKEN_THIS;
		err = parser_parse(this, type, 0, q_pos, &child);
		if (err == ERR_NO_MATCH) {
			type = TOKEN_NULL;
			err = parser_parse(this, type, 0, q_pos, &child);
		}
		if (err == ERR_NO_MATCH) {
			type = TOKEN_TRUE;
			err = parser_parse(this, type, 0, q_pos, &child);
		}
		if (err == ERR_NO_MATCH) {
			type = TOKEN_FALSE;
			err = parser_parse(this, type, 0, q_pos, &child);
		}
		if (err == ERR_NO_MATCH) {
			type = TOKEN_NUMBER;	/* Same as NumericLiteral */
			err = parser_parse(this, type, 0, q_pos, &child);
		}
		if (err == ERR_NO_MATCH) {
			type = TOKEN_STRING;	/* Same as StringLiteral */
			err = parser_parse(this, type, 0, q_pos, &child);
		}
		if (err == ERR_NO_MATCH) {
			type = ARRAY_LITERAL;
			err = parser_parse(this, type, flags, q_pos, &child);
		}
		if (err == ERR_NO_MATCH) {
			type = OBJECT_LITERAL;
			err = parser_parse(this, type, flags, q_pos, &child);
		}
		if (err == ERR_NO_MATCH) {
			type = FUNCTION_EXPRESSION;
			err = parser_parse(this, type, 0, q_pos, &child);
		}
		if (err == ERR_NO_MATCH) {
			type = CLASS_EXPRESSION;
			err = parser_parse(this, type, flags, q_pos, &child);
		}
		if (err == ERR_NO_MATCH) {
			type = GENERATOR_EXPRESSION;
			err = parser_parse(this, type, 0, q_pos, &child);
		}
		if (err == ERR_NO_MATCH) {
			type = ASYNC_FUNCTION_EXPRESSION;
			err = parser_parse(this, type, 0, q_pos, &child);
		}
		if (err == ERR_NO_MATCH) {
			type = ASYNC_GENERATOR_EXPRESSION;
			err = parser_parse(this, type, 0, q_pos, &child);
		}
		if (err == ERR_NO_MATCH) {
			type = REGEXP_LITERAL;
			err = parser_parse(this, type, 0, q_pos, &child);
		}
		if (err == ERR_NO_MATCH) {
			type = PARENTHESIZED_EXPRESSION;	/* restrictive grammar */
			err = parser_parse(this, type, flags, q_pos, &child);
		}
		if (err == ERR_NO_MATCH) {
			type = IDENTIFIER_REFERENCE;
			err = parser_parse(this, type, flags, q_pos, &child);
		}
		if (err == ERR_NO_MATCH) {
			type = TEMPLATE_LITERAL;
			flags &= bits_off(GP_TAGGED);
			err = parser_parse(this, type, flags, q_pos, &child);
		}
		break;
		/*******************************************************************/
	case IMPORT_META:
		type = TOKEN_IMPORT;
		err = parser_parse(this, type, 0, q_pos, &child);
		if (err)
			break;

		type = TOKEN_DOT;
		err = parser_parse(this, type, 0, q_pos, &child);
		if (err)
			break;

		type = TOKEN_META;
		err = parser_parse(this, type, 0, q_pos, &child);
		break;
	case NEW_TARGET:
		type = TOKEN_NEW;
		err = parser_parse(this, type, 0, q_pos, &child);
		if (err)
			break;

		type = TOKEN_DOT;
		err = parser_parse(this, type, 0, q_pos, &child);
		if (err)
			break;

		type = TOKEN_TARGET;
		err = parser_parse(this, type, 0, q_pos, &child);
		break;
	case META_PROPERTY:
		type = NEW_TARGET;
		err = parser_parse(this, type, 0, q_pos, &child);
		if (err == ERR_NO_MATCH) {
			type = IMPORT_META;
			err = parser_parse(this, type, 0, q_pos, &child);
		}
		break;
	case SUPER_PROPERTY:
		type = TOKEN_SUPER;
		err = parser_parse(this, type, 0, q_pos, &child);
		if (err)
			break;
		parse_node_delete(child);	/* Consume 'super' */

		type = ARRAY_EXPRESSION;
		flags |= bits_on(GP_IN);
		err = parser_parse(this, type, flags, q_pos, &child);
		if (err == ERR_NO_MATCH) {
			type = DOT_IDENTIFIER_NAME;
			err = parser_parse(this, type, 0, q_pos, &child);
		}
		break;
	case MEMBER_EXPRESSION:	/* left-associative */
		type = SUPER_PROPERTY;
		err = parser_parse(this, type, flags, q_pos, &child);
		if (err == ERR_NO_MATCH) {
			type = META_PROPERTY;
			err = parser_parse(this, type, 0, q_pos, &child);
		}
		if (err == ERR_NO_MATCH) {
			type = TOKEN_NEW;
			err = parser_parse(this, type, flags, q_pos, &child);
		}
		if (err == ERR_NO_MATCH) {
			type = PRIMARY_EXPRESSION;
			err = parser_parse(this, type, flags, q_pos, &child);
		}

		if (err)
			break;

		if (type == TOKEN_NEW) {
			type = MEMBER_EXPRESSION;
			err = parser_parse(this, type, flags, q_pos, &child);
			if (err)
				break;
			type = ARGUMENTS;
			err = parser_parse(this, type, flags, q_pos, &child);
			if (err)
				break;
		}

		/*
		 * Now we must check in a loop for:
		 *	Array Expr (i.e. [ Expr ])
		 *	. IdentifierName
		 *	. PrivateIdentifier
		 *	TemplateLiteral
		 */
		type = MEMBER_EXPRESSION_POST;
		err = parser_parse(this, type, flags, q_pos, &child);
		break;
		/*******************************************************************/
	case NEW_EXPRESSION:	/* right-associative */
		/* NewExpr has starting with 'new':
		 * 	new NewExpr
		 * 	new MemberExpr Arguments
		 * Check the longest first (MemberExpr)
		 */
		type = MEMBER_EXPRESSION;
		err = parser_parse(this, type, flags, q_pos, &child);
		if (err == ERR_NO_MATCH) {
			type = TOKEN_NEW;
			err = parser_parse(this, type, flags, q_pos, &child);
		}

		if (err)
			break;

		parse_node_add_child(node, child);
		if (type == MEMBER_EXPRESSION)
			break;

		type = NEW_EXPRESSION;
		err = parser_parse(this, type, flags, q_pos, &child);
		break;
		/*******************************************************************/
	case CALL_MEMBER_EXPRESSION:
		type = MEMBER_EXPRESSION;
		err = parser_parse(this, type, flags, q_pos, &child);
		if (err)
			break;
		parse_node_add_child(node, child);

		type = ARGUMENTS;
		err = parser_parse(this, type, flags, q_pos, &child);
		break;
	case IMPORT_CALL:
		type = TOKEN_IMPORT;
		err = parser_parse(this, type, 0, q_pos, &child);
		if (err)
			break;
		parse_node_delete(child);	/* Consume 'import' */

		type = TOKEN_LEFT_PAREN;
		err = parser_parse(this, type, 0, q_pos, &child);
		if (err)
			break;
		parse_node_delete(child);	/* Consume ( */

		type = ASSIGNMENT_EXPRESSION;
		flags |= bits_on(GP_IN);
		err = parser_parse(this, type, flags, q_pos, &child);
		if (err)
			break;

		type = TOKEN_RIGHT_PAREN;
		err = parser_parse(this, type, 0, q_pos, &child);
		if (err)
			break;
		parse_node_delete(child);	/* Consume ) */
		break;
	case SUPER_CALL:
		type = TOKEN_SUPER;
		err = parser_parse(this, type, 0, q_pos, &child);
		if (err)
			break;
		parse_node_delete(child);	/* Consume 'super' */

		type = ARGUMENTS;
		err = parser_parse(this, type, flags, q_pos, &child);
		break;
	case CALL_EXPRESSION:	/* left-associative */
		type = SUPER_CALL;
		err = parser_parse(this, type, flags, q_pos, &child);
		if (err == ERR_NO_MATCH) {
			type = IMPORT_CALL;
			err = parser_parse(this, type, flags, q_pos, &child);
		}
		if (err == ERR_NO_MATCH) {
			type = CALL_MEMBER_EXPRESSION;	/* restrictive grammar */
			err = parser_parse(this, type, flags, q_pos, &child);
		}
		if (err)
			break;
		parse_node_add_child(node, child);

		/*
		 * Now we must check for these to follow, in a loop. If none-follow,
		 * done. Ignore error.
		 * 	Arguments
		 *	Array Expr. [ Expr ]
		 *	. IdentifierName
		 *	TemplateLiteral
		 * 	. PrivateIdentifier
		 * 	TemplateLiteral
		 * 	IdentifierName
		 * 	PrivateIdentifier
		 */
		type = CALL_EXPRESSION_POST;
		flags = in_flags;
		err = parser_parse(this, type, flags, q_pos, &child);
		if (err)
			err = ERR_SUCCESS;
		break;
		/*******************************************************************/
	case OPTIONAL_CHAIN_POST:
	case CALL_EXPRESSION_POST:
	case MEMBER_EXPRESSION_POST:
		/*
		 * Now we must check for these to follow, in a loop. If none-follow,
		 * done. APplies to both OptionalChain and CallExpr.
		 * 	Arguments
		 *	Array Expr. [ Expr ]
		 *	. IdentifierName
		 *	TemplateLiteral
		 * 	. PrivateIdentifier
		 * 	TemplateLiteral
		 * 	IdentifierName
		 * 	PrivateIdentifier
		 */
		while (true) {
			err = ERR_NO_MATCH;

			/* ARGUMENTS not applicable to MEMBER_EXPRESSION_POST */
			if (in_type != MEMBER_EXPRESSION_POST) {
				flags = in_flags;
				type = ARGUMENTS;
				err = parser_parse(this, type, flags, q_pos, &child);
			}
			if (err == ERR_NO_MATCH) {
				type = ARRAY_EXPRESSION;
				flags |= bits_on(GP_IN);
				err = parser_parse(this, type, flags, q_pos, &child);
			}
			if (err == ERR_NO_MATCH) {
				type = TEMPLATE_LITERAL;
				flags = in_flags | bits_on(GP_TAGGED);
				err = parser_parse(this, type, flags, q_pos, &child);
			}
			if (err == ERR_NO_MATCH) {
				type = DOT_IDENTIFIER_NAME;
				err = parser_parse(this, type, 0, q_pos, &child);
			}
			if (err == ERR_NO_MATCH) {
				type = DOT_PRIVATE_IDENTIFIER;
				err = parser_parse(this, type, 0, q_pos, &child);
			}
			if (err)
				break;
			parse_node_add_child(node, child);
		}
		break;
	case OPTIONAL_CHAIN:	/* left-associative */
		type = TOKEN_QUESTION_DOT;
		err = parser_parse(this, type, 0, q_pos, &child);
		if (err)
			break;

		/*
		 * Now we must check for these first, to follow ?. :
		 * 	Arguments
		 * 	[ Expr ] (also called ArrayExpr; != ArrayLiteral)
		 * 	TemplateLiteral
		 * 	IdentifierName
		 * 	PrivateIdentifier
		 */
		type = ARGUMENTS;
		err = parser_parse(this, type, flags, q_pos, &child);
		if (err == ERR_NO_MATCH) {
			type = ARRAY_EXPRESSION;	/* [ Expression ] */
			flags |= bits_on(GP_IN);
			err = parser_parse(this, type, 0, q_pos, &child);
		}
		if (err == ERR_NO_MATCH) {
			type = IDENTIFIER_NAME;
			err = parser_parse(this, type, 0, q_pos, &child);
		}
		if (err == ERR_NO_MATCH) {
			type = TEMPLATE_LITERAL;
			flags = in_flags | bits_on(GP_TAGGED);
			err = parser_parse(this, type, flags, q_pos, &child);
		}
		if (err == ERR_NO_MATCH) {
			type = PRIVATE_IDENTIFIER;
			err = parser_parse(this, type, 0, q_pos, &child);
		}

		/* We matched ?., but could not match its followers. Syntax Err. */
		if (err)
			break;
		parse_node_add_child(node, child);

		/*
		 * Now we must check for these to follow, in a loop. If none-follow,
		 * done. Ignore Error.
		 * 	Arguments
		 *	Array Expr. [ Expr ]
		 *	. IdentifierName
		 *	TemplateLiteral
		 * 	. PrivateIdentifier
		 * 	TemplateLiteral
		 * 	IdentifierName
		 * 	PrivateIdentifier
		 */
		type = OPTIONAL_CHAIN_POST;
		flags = in_flags;
		err = parser_parse(this, type, flags, q_pos, &child);
		if (err)
			err = ERR_SUCCESS;
		break;
	case OPTIONAL_EXPRESSION:	/* left-associative */
		/* In decr. order of length */
		type = CALL_EXPRESSION;
		err = parser_parse(this, type, flags, q_pos, &child);
		if (err == ERR_NO_MATCH) {
			type = MEMBER_EXPRESSION;
			err = parser_parse(this, type, flags, q_pos, &child);
		}

		if (err)
			break;

		/* Add call/memexpr into the node */
		parse_node_add_child(node, child);

		/* There must be OPTIONAL_CHAINs */
		has_opt_chain = false;
		type = OPTIONAL_CHAIN;
		while (true) {
			err = parser_parse(this, type, flags, q_pos, &child);
			if (err) {
				err = has_opt_chain ? ERR_SUCCESS : err;
				break;
			}
			parse_node_add_child(node, child);
		}
		break;
		/*******************************************************************/
	case LHS_EXPRESSION:
		/* In decr. order of length. */
		type = OPTIONAL_EXPRESSION;
		err = parser_parse(this, type, flags, q_pos, &child);
		if (err == ERR_NO_MATCH) {
			type = CALL_EXPRESSION;
			err = parser_parse(this, type, flags, q_pos, &child);
		}
		if (err == ERR_NO_MATCH) {
			type = NEW_EXPRESSION;
			err = parser_parse(this, type, flags, q_pos, &child);
		}
		break;
	case ASSIGNMENT_EXPRESSION:	/* right-associative */
		/*
		 * LHS_Expr occurs both as a child and grand(n)-child. Hence, check
		 * LHS_Expr first
		 */
		type = LHS_EXPRESSION;
		flags &= bits_off(GP_IN);
		err = parser_parse(this, type, flags, q_pos, &child);
		if (!err) {
			/* If LHS_EXPR was parsed, check for operators next */
			parse_node_add_child(node, child);	/* Add LHS_EXPRESSION */

			for (i = 0; i < ARRAY_SIZE(g_assign_expr_ops); ++i) {
				type = g_assign_expr_ops[i];
				err = parser_parse(this, type, 0, q_pos, &child);
				/* if no match continue, else break */
				if (err != ERR_NO_MATCH)
					break;
			}

			/* If there was an error, reparse. Else, continue. */
			if (err) {
				/* Reparse */
				*q_pos = in_pos;
			} else {
				parse_node_add_child(node, child);	/* Add the operator */

				type = ASSIGNMENT_EXPRESSION;
				flags = in_flags;
				err = parser_parse(this, type, flags, q_pos, &child);
				/*
				 * The end-of-func will deal with the child depending on the
				 * error.
				 */
				break;
			}
		}

		/* Reparse */
		type = ASYNC_ARROW_FUNCTION;
		flags = in_flags;
		err = parser_parse(this, type, flags, q_pos, &child);
		if (err == ERR_NO_MATCH && bits_get(in_flags, GP_YIELD)) {
			type = YIELD_EXPRESSION;
			flags &= bits_off(GP_YIELD);
			err = parser_parse(this, type, flags, q_pos, &child);
		}
		if (err == ERR_NO_MATCH) {
			type = ARROW_FUNCTION;
			flags = in_flags;
			err = parser_parse(this, type, flags, q_pos, &child);
		}
		if (err == ERR_NO_MATCH) {
			type = CONDITIONAL_EXPRESSION;
			err = parser_parse(this, type, flags, q_pos, &child);
		}
		/* func-end will take care of child. */
		break;
		/*******************************************************************/
	case INITIALIZER:
		type = TOKEN_EQUALS;
		err = parser_parse(this, type, 0, q_pos, &child);
		if (err)
			break;

		parse_node_delete(child);	/* Consume = */
		type = ASSIGNMENT_EXPRESSION;
		err = parser_parse(this, type, flags, q_pos, &child);
		break;
		/*******************************************************************/
	case BINDING_IDENTIFIER:
		type = IDENTIFIER_NAME;
		err = parser_parse(this, type, 0, q_pos, &child);
		if (err)
			break;

		if (parse_node_is_reserved_word(child)) {
			/* This is a reserved word. Fail. TODO await and yield. */
			parse_node_delete(child);
			child = NULL;
			err = ERR_NO_MATCH;
		}
		break;
		/*******************************************************************/
	case VARIABLE_DECLARATION:
		type = BINDING_IDENTIFIER;
		flags &= bits_off(GP_IN);
		err = parser_parse(this, type, flags, q_pos, &child);
		if (err == ERR_NO_MATCH) {
			type = BINDING_PATTERN;
			err = parser_parse(this, type, flags, q_pos, &child);
		}

		if (err)
			break;

		parse_node_add_child(node, child);
		is_ident = type == BINDING_IDENTIFIER;

		/* Initializer is optional for Identifier */
		flags = in_flags;
		type = INITIALIZER;
		err = parser_parse(this, type, flags, q_pos, &child);
		if (err && is_ident)
			err = ERR_SUCCESS;
		/* The func-end will add the initializer to the node */
		break;
	case VARIABLE_DECLARATION_LIST:	/* left-associative */
		while (true) {
			type = VARIABLE_DECLARATION;
			err = parser_parse(this, type, flags, q_pos, &child);
			if (err) {
				/* If we have a valid list, ignore the error. */
				if (parse_node_has_children(node))
					err = ERR_SUCCESS;	/* Error handling later. */
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

			/*
			 * Delete the COMMA node. The child ptr will be set to null by the
			 * call to parser_parse in this loop.
			 */
			parse_node_delete(child);
		}
		break;
	case VARIABLE_STATEMENT:
		type = TOKEN_VAR;
		err = parser_parse(this, type, 0, q_pos, &child);
		if (err)
			break;
		parse_node_delete(child);	/* Delete the VAR node */

		type = VARIABLE_DECLARATION_LIST;
		flags |= bits_on(GP_IN);
		err = parser_parse(this, type, flags, q_pos, &child);
		if (err)
			break;
		parse_node_add_child(node, child);

		/*
		 * SEMI_COLON, EOF and NL end the Var_Stmt.
		 *
		 * Check NL first. The reason is:
		 * 		var a = 123 <nl> ;
		 * should treat the ; as an empty stmt instead of as part of the var
		 * stmt.
		 */

		type = TOKEN_NEW_LINE;
		err = parser_parse(this, type, 0, q_pos, &child);
		if (err == ERR_NO_MATCH) {
			type = TOKEN_SEMI_COLON;
			err = parser_parse(this, type, 0, q_pos, &child);
		}

		if (!err) {
			parse_node_delete(child);	/* Consume the nl/; */
			child = NULL;
		} else if (err == ERR_END_OF_FILE) {
			err = ERR_SUCCESS;
		} else if (err == ERR_NO_MATCH) {
			/*
			 * If still no match, then this is a syntax error, since we have
			 * already mapped 'var' and a decl. list.
			 */
		}
		break;
		/*******************************************************************/
	case BLOCK:
		type = TOKEN_LEFT_BRACE;
		err = parser_parse(this, type, 0, q_pos, &child);
		if (err)
			break;
		parse_node_delete(child);

		/* STATEMENT_LIST is optional for BLOCK */
		type = TOKEN_RIGHT_BRACE;
		err = parser_parse(this, type, 0, q_pos, &child);
		if (!err) {
			/* Empty Block */
			parse_node_delete(child);
			child = NULL;
			break;
		}

		/* Non-Empty Block. */
		type = STATEMENT_LIST;
		err = parser_parse(this, type, flags, q_pos, &child);
		if (err)
			break;

		parse_node_add_child(node, child);

		/* There must be a }, after the STATEMENT_LIST */
		type = TOKEN_RIGHT_BRACE;
		err = parser_parse(this, type, 0, q_pos, &child);
		if (err)
			break;
		parse_node_delete(child);
		child = NULL;
		break;
	case BLOCK_STATEMENT:
		type = BLOCK;
		err = parser_parse(this, type, flags, q_pos, &child);
		break;
		/*******************************************************************/
	case STATEMENT:
		/* Keep EXPRESSION_STATEMENT last, as others begin with keywords. */
		type = BLOCK_STATEMENT;
		err = parser_parse(this, type, flags, q_pos, &child);
		if (err == ERR_NO_MATCH) {
			type = VARIABLE_STATEMENT;
			flags = in_flags & bits_off(GP_RETURN);
			err = parser_parse(this, type, flags, q_pos, &child);
		}
		if (err == ERR_NO_MATCH) {
			type = EMPTY_STATEMENT;
			err = parser_parse(this, type, 0, q_pos, &child);
		}
		/* EXPRESSION_STATEMENT at the end. */
		if (err == ERR_NO_MATCH) {
			type = IF_STATEMENT;
			err = parser_parse(this, type, flags, q_pos, &child);
		}
		if (err == ERR_NO_MATCH) {
			type = BREAKABLE_STATEMENT;
			err = parser_parse(this, type, flags, q_pos, &child);
		}
		if (err == ERR_NO_MATCH) {
			type = CONTINUE_STATEMENT;
			flags = in_flags & bits_off(GP_RETURN);
			err = parser_parse(this, type, flags, q_pos, &child);
		}
		if (err == ERR_NO_MATCH) {
			type = BREAK_STATEMENT;
			flags = in_flags & bits_off(GP_RETURN);
			err = parser_parse(this, type, flags, q_pos, &child);
		}
		if (err == ERR_NO_MATCH && bits_get(in_flags, GP_RETURN)) {
			type = RETURN_STATEMENT;
			flags = in_flags & bits_off(GP_RETURN);
			err = parser_parse(this, type, flags, q_pos, &child);
		}
		if (err == ERR_NO_MATCH) {
			type = WITH_STATEMENT;
			err = parser_parse(this, type, flags, q_pos, &child);
		}
		if (err == ERR_NO_MATCH) {
			type = LABELLED_STATEMENT;
			err = parser_parse(this, type, flags, q_pos, &child);
		}
		if (err == ERR_NO_MATCH) {
			type = THROW_STATEMENT;
			flags = in_flags & bits_off(GP_RETURN);
			err = parser_parse(this, type, flags, q_pos, &child);
		}
		if (err == ERR_NO_MATCH) {
			type = TRY_STATEMENT;
			err = parser_parse(this, type, flags, q_pos, &child);
		}
		if (err == ERR_NO_MATCH) {
			type = DEBUGGER_STATEMENT;
			err = parser_parse(this, type, 0, q_pos, &child);
		}
		if (err == ERR_NO_MATCH) {
			type = EXPRESSION_STATEMENT;
			flags = in_flags & bits_off(GP_RETURN);
			err = parser_parse(this, type, flags, q_pos, &child);
		}
		break;
	default:
		printf("%s: unsup %d\n", __func__, type);
		if (type >= SCRIPT)
			printf("%s: unsup non-term %d\n", __func__, type - SCRIPT);
		else if (type >= TOKEN_IDENTIFIER)
			printf("%s: unsup ident %d\n", __func__, type - TOKEN_IDENTIFIER);
		exit(0);
	}

	if (!err) {
		if (child)
			parse_node_add_child(node, child);
		*out = node;
	} else {
		/* child is not inserted into node yet. Should be NULL. */
		assert(child == NULL);

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
	err = parser_parse(this, SCRIPT, 0, &q_pos, &this->root);
	if (err == ERR_END_OF_FILE)
		err = ERR_SUCCESS;
	return err;
}
