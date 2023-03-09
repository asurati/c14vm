/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2023 Amol Surati */

#include <prv/scanner.h>

#include <pub/unicode.h>
#include <pub/system.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const
char16_t *g_key_words[] = {
	u"as",
	u"async",
	u"await",
	u"break",
	u"case",
	u"catch",
	u"class",
	u"const",
	u"continue",
	u"debugger",
	u"default",
	u"delete",
	u"do",
	u"else",
	u"enum",
	u"export",
	u"extends",
	u"false",
	u"finally",
	u"for",
	u"from",
	u"function",
	u"get",
	u"if",
	u"implements",
	u"import",
	u"in",
	u"instanceof",
	u"interface",
	u"let",
	u"meta",
	u"new",
	u"null",
	u"of",
	u"package",
	u"private",
	u"protected",
	u"public",
	u"return",
	u"set",
	u"static",
	u"super",
	u"switch",
	u"target",
	u"this",
	u"throw",
	u"true",
	u"try",
	u"typeof",
	u"var",
	u"void",
	u"while",
	u"with",
	u"yield",
};
/*******************************************************************/
static
int token_new(enum token_type type,
			  const struct token_location *locn,
			  size_t raw_len,
			  size_t flags,
			  struct token **out)
{
	struct token *token;

	token = calloc(1, sizeof(*token));
	if (token == NULL)
		return ERR_NO_MEMORY;

	token->type = type;
	token->locn = *locn;
	token->flags = flags;
	token->raw_len = raw_len;
	*out = token;
	return ERR_SUCCESS;
}

int token_delete(struct token *this)
{
	free((void *)this->cooked);
	free(this);
	return ERR_SUCCESS;
}
/*******************************************************************/
int scanner_new(const char16_t *src,
				size_t src_len,
				struct scanner **out)
{
	int err;
	struct scanner *scanner;

	err = ERR_NO_MEMORY;
	scanner = calloc(1, sizeof(*scanner));
	if (scanner == NULL)
		goto err0;

	err = ERR_SUCCESS;
	scanner->src = src;
	scanner->src_len = src_len;
	*out = scanner;
err0:
	return err;
}

int scanner_delete(struct scanner *this)
{
	free((void *)this->src);
	free(this);
	return ERR_SUCCESS;
}
/*******************************************************************/
static inline
int scanner_build_token(struct scanner *this,
						enum token_type type,
						size_t flags,
						struct token **out)
{
	size_t raw_len;

	if (this->prev_token_type == TOKEN_NEW_LINE)
		flags |= bits_on(TF_NL_PFX);
	raw_len = this->curr_locn.scan_pos - this->save_locn.scan_pos;
	return token_new(type, &this->save_locn, raw_len, flags, out);
}
/*******************************************************************/
static
bool scanner_is_end(const struct scanner *this,
					size_t pos)
{
	return pos >= this->src_len;
}

static
int scanner_peek(const struct scanner *this,
				 size_t offset,
				 char16_t *out)
{
	size_t pos, scan_pos;

	scan_pos = this->curr_locn.scan_pos;
	pos = scan_pos + offset;
	if (pos < scan_pos || scanner_is_end(this, pos))
		return ERR_END_OF_FILE;
	*out = this->src[pos];
	return ERR_SUCCESS;
}

/* For surr. pairs, file_col is incremented only once per pair */
static
void scanner_consume(struct scanner *this,
					 size_t num_units)
{
	size_t i;
	char16_t cu;
	struct token_location *locn;

	locn = &this->curr_locn;
	for (i = 0; i < num_units; ++i) {
		if (scanner_is_end(this, locn->scan_pos))
			break;

		cu = this->src[locn->scan_pos++];
		if (is_low_surrogate(cu) && locn->scan_pos >= 2 &&
			is_high_surrogate(this->src[locn->scan_pos - 2]))
			continue;
		++locn->file_col;

		/* \r and \r\n => \n */
		if (cu == '\r') {
			if (!scanner_is_end(this, locn->scan_pos) &&
				this->src[locn->scan_pos] == '\n')
				++locn->scan_pos;
			cu = '\n';
		}

		if (cu == '\n') {
			++locn->file_row;
			locn->file_col = 0;
		}
	}
}
/*******************************************************************/
static
void scanner_skip_single_line_comment(struct scanner *this)
{
	int err;
	char16_t cu;

	scanner_consume(this, 2);	/* Consume // or #! */

	while (true) {
		err = scanner_peek(this, 0, &cu);
		if (err || is_line_terminator(cu))
			break;
		scanner_consume(this, 1);
	}
}

static
bool scanner_skip_multi_line_comment(struct scanner *this)
{
	int err;
	char16_t cu;
	bool has_new_line;
	bool half_close_seen;

	has_new_line = half_close_seen = false;

	scanner_consume(this, 2);	/* Consume slash-star */

	while (true) {
		err = scanner_peek(this, 0, &cu);
		if (err)
			break;
		scanner_consume(this, 1);
		if (is_line_terminator(cu)) {
			has_new_line = true;
			half_close_seen = false;
		} else if (cu == '*') {
			half_close_seen = true;
		} else if (cu == '/' && half_close_seen) {
			break;
		} else {
			half_close_seen = false;
		}
	}
	return has_new_line;
}

/*
 * skip_white_space skips over whitespace and comments.
 * If it skips over one or more line-terminators, it returns true.
 * #! comment must be at scan_pos == 0
 */
static
bool scanner_skip_white_space(struct scanner *this)
{
	char16_t cu, t;
	bool has_new_line;

	has_new_line = false;

	while (true) {
		if (scanner_peek(this, 0, &cu))
			break;

		/*
		 * All white-space (+ line-terminators) are within the plane-0.
		 * i.e. they are not encoded within the src using surrogate pairs.
		 */
		if (is_white_space(cu) || is_line_terminator(cu)) {
			scanner_consume(this, 1);
			has_new_line = is_line_terminator(cu) ? true : has_new_line;
			continue;
		}

		/* Check for #!, slash-star and // comments */

		/* Can't be a comment */
		if (cu != '#' && cu != '/')
			break;

		/* #! must be at scan_pos == 0 */
		if (cu == '#' && this->curr_locn.scan_pos)
			break;

		if (scanner_peek(this, 1, &t))
			break;

		if (cu == '#') {
			cu = t;
			if (cu == '!')
				scanner_skip_single_line_comment(this);
			else
				break;
		} else {
			cu = t;
			if (cu == '/')
				scanner_skip_single_line_comment(this);
			else if (cu == '*' && scanner_skip_multi_line_comment(this))
				has_new_line = true;
			else
				break;
		}
	}
	return has_new_line;
}
/*******************************************************************/
int scanner_scan_string(struct scanner *this,
						struct token **out)
{
	int err;
	enum token_type type;
	struct token *token;
	size_t i, cooked_len, flags;
	char16_t cu, *cooked;
	bool is_double_quoted;
	static char16_t str[32];

	err = scanner_peek(this, 0, &cu);
	if (err)
		return err;
	scanner_consume(this, 1);
	is_double_quoted = cu == '\"';

	cooked_len = i = flags = 0;
	cooked = NULL;
	while (true) {
		err = scanner_peek(this, 0, &cu);
		/* Can't have an error parsing a string. */
		if (err)
			break;
		scanner_consume(this, 1);

		if (is_double_quoted && cu == '\"')
			break;
		if (!is_double_quoted && cu == '\'')
			break;

		/* Can't have unescaped LF, CR in a string. */
		err = ERR_INVALID_TOKEN;
		if (cu == '\r' || cu == '\n')
			break;

		/* Not an escape, so append */
		if (cu != '\\')
			goto append;

		/* Read the char after \ */
		err = scanner_peek(this, 0, &cu);
		if (err)
			break;
		scanner_consume(this, 1);

		/* LineTerminatorSequence. Ignore the line-terminator. */
		if (is_line_terminator(cu))
			continue;

		/* SingleEscapeCharacter */
		if (cu == '\'' || cu == '\"' || cu == '\\') {
			goto append;
		} else if (cu == 'b') {
			cu = '\b';
			goto append;
		} else if (cu == 'f') {
			cu = '\f';
			goto append;
		} else if (cu == 'n') {
			cu = '\n';
			goto append;
		} else if (cu == 'r') {
			cu = '\r';
			goto append;
		} else if (cu == 't') {
			cu = '\t';
			goto append;
		} else if (cu == 'v') {
			cu = '\v';
			goto append;
		}

		/* TODO: handle x, u, dec_digit and NonEscChar */
		/* Set flags */
		flags |= bits_on(TF_UNC_SEQ);	/* For u */
		flags |= bits_on(TF_HEX_SEQ);	/* For x */
		printf("%s: TODO %c\n", __func__, cu);
		exit(0);
	append:
		if (i == 32) {
			cooked = realloc(cooked, (cooked_len + i) * sizeof(char16_t));
			if (cooked == NULL)
				return ERR_NO_MEMORY;
			memcpy(&cooked[cooked_len], str, i * sizeof(char16_t));
			cooked_len += i;
			i = 0;
		}
		str[i++] = cu;
	}

	if (err) {
		free(cooked);
		return err;
	}

	if (i) {
		cooked = realloc(cooked, (cooked_len + i) * sizeof(char16_t));
		if (cooked == NULL)
			return ERR_NO_MEMORY;
		memcpy(&cooked[cooked_len], str, i * sizeof(char16_t));
		cooked_len += i;
	}

	type = TOKEN_STRING;
	err = scanner_build_token(this, type, flags, &token);
	if (!err)
		token_set_cooked(token, cooked, cooked_len);
	if (!err)
		*out = token;
	return err;
}

static
int scanner_scan_equals(struct scanner *this,
						struct token **out)
{
	enum token_type type;
	char16_t cu;

	/* =, ==, ===, => */

	/* At least TOKEN_EQUALS is present. */
	type = TOKEN_EQUALS;
	scanner_consume(this, 1);

	while (true) {
		if (scanner_peek(this, 0, &cu))
			break;

		if (type == TOKEN_EQUALS && cu == '=')
			type = TOKEN_DOUBLE_EQUALS;
		else if (type == TOKEN_EQUALS && cu == '>')
			type = TOKEN_ARROW;
		else if (type == TOKEN_DOUBLE_EQUALS && cu == '=')
			type = TOKEN_TRIPLE_EQUALS;
		else
			break;

		scanner_consume(this, 1);
		if (type != TOKEN_DOUBLE_EQUALS)
			break;
	}
	return scanner_build_token(this, type, 0, out);
}
/*******************************************************************/
/*
 * Identifiers can contain \uxxxx or \u{} esc. seqs.
 * They cannot contain \x.. hex esc seqs.
 * They cannot contain surr pairs.
 */
static
int scanner_scan_identifier(struct scanner *this,
							struct token **out)
{
	int err;
	size_t i, j, flags;
	size_t cooked_len;
	char16_t cu, *cooked;
	enum token_type type;
	struct token *token;
	static char16_t name[32];

	i = flags = 0;
	cooked_len = 0;
	cooked = NULL;
	while (true) {
		if (scanner_peek(this, 0, &cu))
			break;

		/* Can't contain surrogates in an identifier */
		if (is_low_surrogate(cu) || is_high_surrogate(cu))
			break;

		if (cu == '\\') {
			flags |= bits_on(TF_UNC_SEQ);
			printf("%s: TODO\n", __func__);
			exit(0);
		}

		/* At start, the cu must be in id_start */
		if (i == 0 && cooked_len == 0 && !is_id_start(cu))
			break;

		/* Valid codepoint but not part of id_continue */
		if ((i || cooked_len) && !is_id_continue(cu))
			break;

		if (i == 32) {
			cooked = realloc(cooked, (cooked_len + i) * sizeof(char16_t));
			if (cooked == NULL)
				return ERR_NO_MEMORY;
			memcpy(&cooked[cooked_len], name, i * sizeof(char16_t));
			cooked_len += i;
			i = 0;
		}
		name[i++] = cu;
		scanner_consume(this, 1);
	}

	if (i == 0 && cooked_len == 0)
		return ERR_INVALID_TOKEN;

	if (i) {
		cooked = realloc(cooked, (cooked_len + i) * sizeof(char16_t));
		if (cooked == NULL)
			return ERR_NO_MEMORY;
		memcpy(&cooked[cooked_len], name, i * sizeof(char16_t));
		cooked_len += i;
	}

	type = TOKEN_IDENTIFIER;
	for (i = 0; i < ARRAY_SIZE(g_key_words); ++i) {
		for (j = 0; j < cooked_len; ++j) {
			if (cooked[j] != g_key_words[i][j])
				break;
		}

		if (j == cooked_len && g_key_words[i][j] == 0) {
			type = TOKEN_IDENTIFIER + i + 1;
			break;
		}
	}

	err = scanner_build_token(this, type, flags, &token);
	if (!err && type == TOKEN_IDENTIFIER)
		token_set_cooked(token, cooked, cooked_len);
	if (!err)
		*out = token;
	return err;
}
/*******************************************************************/
static
int scanner_scan_next_token(struct scanner *this,
							struct token **out)
{
	int err;
	char16_t cu;

	this->save_locn = this->curr_locn;

	err = scanner_peek(this, 0, &cu);
	if (err)
		return err;

	if (cu == '\"' || cu == '\'')
		return scanner_scan_string(this, out);

	if (cu == '=')
		return scanner_scan_equals(this, out);

	if (is_id_start(cu))
		return scanner_scan_identifier(this, out);

	printf("%s: (%ld, %ld) unsup %x (%c)\n", __func__,
		   this->curr_locn.file_row + 1,
		   this->curr_locn.file_col + 1,
		   cu, cu);
	exit(0);
	return err;
}

int scanner_get_next_token(struct scanner *this,
						   const struct token **out)
{
	int err;
	struct token *token;

	if (scanner_skip_white_space(this))
		this->prev_token_type = TOKEN_NEW_LINE;

	err = scanner_scan_next_token(this, &token);

	/* Restore the curr_locn on error. */
	if (err)
		this->curr_locn = this->save_locn;
	else
		*out = token;
	return err;
}
