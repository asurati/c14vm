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

	/* Literal Punctuations */
	TOKEN_LEFT_PAREN,
	TOKEN_LEFT_BRACE,
	TOKEN_LEFT_BRACKET,
	TOKEN_RIGHT_PAREN,
	TOKEN_RIGHT_BRACE,
	TOKEN_RIGHT_BRACKET,

	TOKEN_QUOTE,
	TOKEN_DOUBLE_QUOTE,
	TOKEN_BACK_QUOTE,
	TOKEN_COLON,
	TOKEN_SEMI_COLON,
	TOKEN_COMMA,
	TOKEN_ARROW,

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

	TOKEN_PLUS,
	TOKEN_MINUS,
	TOKEN_DIV,
	TOKEN_MUL,
	TOKEN_MOD,

	/*
	 * Same order as g_key_words.
	 * These are identifiers. Their raw forms may contain unc escs, but never a
	 * hex esc.
	 */
	TOKEN_IDENTIFIER,
	TOKEN_AS,
	TOKEN_ASYNC,
	TOKEN_AWAIT,
	TOKEN_BREAK,
	TOKEN_CASE,
	TOKEN_CATCH,
	TOKEN_CLASS,
	TOKEN_CONST,
	TOKEN_CONTINUE,
	TOKEN_DEBUGGER,
	TOKEN_DEFAULT,
	TOKEN_DELETE,
	TOKEN_DO,
	TOKEN_ELSE,
	TOKEN_ENUM,
	TOKEN_EXPORT,
	TOKEN_EXTENDS,
	TOKEN_FALSE,
	TOKEN_FINALLY,
	TOKEN_FOR,
	TOKEN_FROM,
	TOKEN_FUNCTION,
	TOKEN_GET,
	TOKEN_IF,
	TOKEN_IMPLEMENTS,
	TOKEN_IMPORT,
	TOKEN_IN,
	TOKEN_INSTANCEOF,
	TOKEN_INTERFACE,
	TOKEN_LET,
	TOKEN_META,
	TOKEN_NEW,
	TOKEN_NULL,
	TOKEN_OF,
	TOKEN_PACKAGE,
	TOKEN_PRIVATE,
	TOKEN_PROTECTED,
	TOKEN_PUBLIC,
	TOKEN_RETURN,
	TOKEN_SET,
	TOKEN_STATIC,
	TOKEN_SUPER,
	TOKEN_SWITCH,
	TOKEN_TARGET,
	TOKEN_THIS,
	TOKEN_THROW,
	TOKEN_TRUE,
	TOKEN_TRY,
	TOKEN_TYPEOF,
	TOKEN_VAR,
	TOKEN_VOID,
	TOKEN_WHILE,
	TOKEN_WITH,
	TOKEN_YIELD,

	/* Syntactical Grammar Non-Terminals */
	TOKEN_SCRIPT,	/* 0 */
	TOKEN_SCRIPT_BODY,
	TOKEN_STMT_LIST,
	TOKEN_STMT_LIST_ITEM,
	TOKEN_STMT,
	TOKEN_DECL,
	TOKEN_BLOCK_STMT,
	TOKEN_VAR_STMT,
	TOKEN_EMPTY_STMT,
	TOKEN_EXPR_STMT,

	TOKEN_IF_STMT,	/* 10 */
	TOKEN_BREAKABLE_STMT,
	TOKEN_CONTINUE_STMT,
	TOKEN_BREAK_STMT,
	TOKEN_RETURN_STMT,
	TOKEN_WITH_STMT,
	TOKEN_LABELLED_STMT,
	TOKEN_THROW_STMT,
	TOKEN_TRY_STMT,
	TOKEN_DEBUGGER_STMT,

	TOKEN_BLOCK,	/* 20 */
	TOKEN_VAR_DECL_LIST,
	TOKEN_VAR_DECL,
	TOKEN_BINDING_IDENTIFIER,
	TOKEN_BINDING_PATTERN,
	TOKEN_INITIALIZER,
	TOKEN_ASSIGN_EXPR,
	TOKEN_LHS_EXPR,
	TOKEN_COND_EXPR,
	TOKEN_YIELD_EXPR,

	TOKEN_ARROW_FUNC,	/* 30 */
	TOKEN_ASYNC_ARROW_FUNC,
	TOKEN_IDENTIFIER_NAME,
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

	int				flags;
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
