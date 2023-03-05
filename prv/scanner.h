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
	TOKEN_ANY,		/* Used whem matching rsvd words/literals. */
	TOKEN_NEW_LINE,	/* Internal use */

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
	TOKEN_EQUALS,
	TOKEN_ARROW,
	TOKEN_DOUBLE_EQUALS,
	TOKEN_TRIPLE_EQUALS,
	TOKEN_FORWARD_SLASH,
	TOKEN_DIV,

	TOKEN_STRING,

	TOKEN_ADD,
	TOKEN_MUL,

	// Same order as g_key_words
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
bool	token_is_rsvd_word(const struct token *this,
						   enum token_type type);

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

// rsvd_literal == rsvd_word with no unc escs.
static inline
bool token_is_rsvd_literal(const struct token *this,
						   enum token_type type)
{
	if (!token_is_rsvd_word(this, type))
		return false;
	return !token_has_unc_esc(this);
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
