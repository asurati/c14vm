/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2023 Amol Surati */

#ifndef PRV_SCANNER_H
#define PRV_SCANNER_H

#include <pub/error.h>
#include <pub/list.h>
#include <pub/bits.h>

#include <assert.h>
#include <uchar.h>
#include <stdbool.h>

#define TF_UNC_SEQ_POS	0
#define TF_HEX_SEQ_POS	1
#define TF_NL_PFX_POS	2
#define TF_UNC_SEQ_BITS	1
#define TF_HEX_SEQ_BITS	1
#define TF_NL_PFX_BITS	1

enum token_type {
	TOKEN_INVALID,	/* Must be 0 */
	TOKEN_NEW_LINE,	/* Internal use */

	TOKEN_STRING,
	TOKEN_NUMBER,	/* Includes BigInt */

	/* Literal Punctuations */
	TOKEN_LEFT_PAREN,
	TOKEN_LEFT_BRACE,
	TOKEN_LEFT_BRACKET,
	TOKEN_RIGHT_PAREN,
	TOKEN_RIGHT_BRACE,
	TOKEN_RIGHT_BRACKET,

	TOKEN_PLUS,	/* Maths */
	TOKEN_MINUS,
	TOKEN_DIV,
	TOKEN_MUL,
	TOKEN_MOD,
	TOKEN_EXP,

	TOKEN_NUMBER_SIGN,
	TOKEN_DOT,
	TOKEN_QUOTE,
	TOKEN_DOUBLE_QUOTE,
	TOKEN_BACK_QUOTE,
	TOKEN_COLON,
	TOKEN_SEMI_COLON,
	TOKEN_COMMA,
	TOKEN_ARROW,
	TOKEN_QUESTION_DOT,

	/* All EQUALS */
	TOKEN_EQUALS,
	TOKEN_DOUBLE_EQUALS,
	TOKEN_TRIPLE_EQUALS,
	TOKEN_MUL_EQUALS,
	TOKEN_MOD_EQUALS,
	TOKEN_DIV_EQUALS,
	TOKEN_PLUS_EQUALS,
	TOKEN_MINUS_EQUALS,
	TOKEN_EXP_EQUALS,
	TOKEN_SHL_EQUALS,
	TOKEN_SHR_EQUALS,
	TOKEN_SAR_EQUALS,
	TOKEN_LOGICAL_OR_EQUALS,
	TOKEN_LOGICAL_AND_EQUALS,
	TOKEN_BITWISE_AND_EQUALS,
	TOKEN_BITWISE_XOR_EQUALS,
	TOKEN_BITWISE_OR_EQUALS,
	TOKEN_COALESCE_EQUALS,

	/*
	 * Same order as g_key_words.
	 * These are identifiers. Their raw forms may contain unc escs, but never a
	 * hex esc.
	 */
	TOKEN_IDENTIFIER,	/* 0 */
	TOKEN_AS,
	TOKEN_ASYNC,
	TOKEN_AWAIT,
	TOKEN_BREAK,
	TOKEN_CASE,
	TOKEN_CATCH,
	TOKEN_CLASS,
	TOKEN_CONST,
	TOKEN_CONTINUE,
	TOKEN_DEBUGGER,	/* 10 */
	TOKEN_DEFAULT,
	TOKEN_DELETE,
	TOKEN_DO,
	TOKEN_ELSE,
	TOKEN_ENUM,
	TOKEN_EXPORT,
	TOKEN_EXTENDS,
	TOKEN_FALSE,
	TOKEN_FINALLY,
	TOKEN_FOR,	/* 20 */
	TOKEN_FROM,
	TOKEN_FUNCTION,
	TOKEN_GET,
	TOKEN_IF,
	TOKEN_IMPLEMENTS,
	TOKEN_IMPORT,
	TOKEN_IN,
	TOKEN_INSTANCEOF,
	TOKEN_INTERFACE,
	TOKEN_LET,	/* 30 */
	TOKEN_META,
	TOKEN_NEW,
	TOKEN_NULL,
	TOKEN_OF,
	TOKEN_PACKAGE,
	TOKEN_PRIVATE,
	TOKEN_PROTECTED,
	TOKEN_PUBLIC,
	TOKEN_RETURN,
	TOKEN_SET,	/* 40 */
	TOKEN_STATIC,
	TOKEN_SUPER,
	TOKEN_SWITCH,
	TOKEN_TARGET,
	TOKEN_THIS,
	TOKEN_THROW,
	TOKEN_TRUE,
	TOKEN_TRY,
	TOKEN_TYPEOF,
	TOKEN_VAR,	/* 50 */
	TOKEN_VOID,
	TOKEN_WHILE,
	TOKEN_WITH,
	TOKEN_YIELD,

	/* Syntactical Grammar Non-Terminals */
	SCRIPT,			/* 0 */
	SCRIPT_BODY,
	STATEMENT_LIST,
	STATEMENT_LIST_ITEM,
	STATEMENT,
	DECLARATION,
	BLOCK_STATEMENT,
	VARIABLE_STATEMENT,
	EMPTY_STATEMENT,
	EXPRESSION_STATEMENT,

	IF_STATEMENT,	/* 10 */
	BREAKABLE_STATEMENT,
	CONTINUE_STATEMENT,
	BREAK_STATEMENT,
	RETURN_STATEMENT,
	WITH_STATEMENT,
	LABELLED_STATEMENT,
	THROW_STATEMENT,
	TRY_STATEMENT,
	DEBUGGER_STATEMENT,

	BLOCK,			/* 20 */
	VARIABLE_DECLARATION_LIST,
	VARIABLE_DECLARATION,
	BINDING_IDENTIFIER,
	BINDING_PATTERN,
	INITIALIZER,
	ASSIGNMENT_EXPRESSION,
	LHS_EXPRESSION,
	CONDITIONAL_EXPRESSION,
	YIELD_EXPRESSION,

	ARROW_FUNCTION,	/* 30 */
	ASYNC_ARROW_FUNCTION,
	IDENTIFIER_NAME,
	OPTIONAL_EXPRESSION,
	CALL_EXPRESSION,
	NEW_EXPRESSION,
	MEMBER_EXPRESSION,
	ARGUMENTS,
	EXPRESSION,
	OPTIONAL_CHAIN,

	ARRAY_EXPRESSION,	/* 40 */
	TEMPLATE_LITERAL,
	PRIVATE_IDENTIFIER,
	CALL_MEMBER_EXPRESSION,
	CALL_EXPRESSION_POST,
	OPTIONAL_CHAIN_POST,
	SUPER_CALL,
	IMPORT_CALL,
	SUPER_PROPERTY,
	META_PROPERTY,

	PRIMARY_EXPRESSION,	/* 50 */
	MEMBER_EXPRESSION_POST,
	DOT_IDENTIFIER_NAME,
	DOT_PRIVATE_IDENTIFIER,
	IMPORT_META,
	NEW_TARGET,
	NUMERIC_LITERAL,
	STRING_LITERAL,
	ARRAY_LITERAL,
	OBJECT_LITERAL,

	FUNCTION_EXPRESSION,	/* 60 */
	CLASS_EXPRESSION,
	GENERATOR_EXPRESSION,
	ASYNC_FUNCTION_EXPRESSION,
	ASYNC_GENERATOR_EXPRESSION,
	REGEXP_LITERAL,
	PARENTHESIZED_EXPRESSION,
	IDENTIFIER_REFERENCE,
};

struct token_location {
	size_t		file_row;
	size_t		file_col;
	size_t		scan_pos;
};

struct token {
	struct token_location	locn;
	size_t			raw_len;

	const char16_t	*cooked;
	size_t			cooked_len;

	size_t			flags;
	enum token_type	type;
};

int		token_delete(struct token *this);

static inline
bool token_type_is_reserved_word(enum token_type type)
{
	return  type >= TOKEN_AS && type <= TOKEN_YIELD;
}

static inline
bool token_is_reserved_word(const struct token *this)
{
	return token_type_is_reserved_word(this->type);
}

static inline
void token_set_cooked(struct token *this,
					  const char16_t *cooked,
					  size_t cooked_len)
{
	this->cooked = cooked;
	this->cooked_len = cooked_len;
}

static inline
enum token_type token_type(const struct token *this)
{
	return this->type;
}

static inline
const char16_t *token_cooked(const struct token *this,
							 size_t *cooked_len)
{
	*cooked_len = this->cooked_len;
	return this->cooked;
}

static inline
bool token_has_unc_esc(const struct token *this)
{
	return bits_get(this->flags, TF_UNC_SEQ) != 0;
}

static inline
bool token_has_hex_esc(const struct token *this)
{
	return bits_get(this->flags, TF_HEX_SEQ) != 0;
}

static inline
bool token_has_new_line_pfx(const struct token *this)
{
	return bits_get(this->flags, TF_NL_PFX) != 0;
}

/* IdentifierName:
 *	can contain unc esc.
 *	can be a rsvd word.
 *	can't contain hex escs.
 */
static inline
bool token_is_identifier_name(const struct token *this)
{
	enum token_type type = token_type(this);

	/*
	 * If it is not lexical TOKEN_IDENTIFIER, and
	 * if it is not a rsvd word,
	 * then it cannot be an identifier name
	 */
	if (type != TOKEN_IDENTIFIER && !token_is_reserved_word(this))
		return false;

	/*
	 * If this has hex esc seqs, it cannot be an identifier name.
	 * This is very likely redundant, since the scanner, when preparing an
	 * identifier token, will raise an error if it finds a hex esc.seq.
	 */
	assert(!token_has_hex_esc(this));
	return true;
}

// rsvd_literal == rsvd_word with no escs.
static inline
bool token_is_reserved_literal(const struct token *this)
{
	if (!token_is_reserved_word(this) ||
		token_has_unc_esc(this))
		return false;
	assert(!token_has_hex_esc(this));
	return true;
}

struct scanner {
	const char16_t	*src;
	size_t			src_len;
	enum token_type	prev_token_type;

	struct token_location	curr_locn;
	struct token_location	save_locn;
};

int	scanner_new(const char16_t *src,
				size_t src_len,
				struct scanner **out);
int	scanner_delete(struct scanner *this);
int	scanner_get_next_token(struct scanner *this,
						   const struct token **out);
#endif
