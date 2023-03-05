/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2023 Amol Surati */

#include <pub/error.h>
#include <pub/system.h>
#include <pub/parser.h>

#include <assert.h>
#include <stdio.h>
#include <locale.h>
#include <string.h>
#include <stdlib.h>
#include <uchar.h>
#include <stdbool.h>

/* Assuming no embedded nul chars in the source stream */
static
int mbr_to_c16(const char *src, const char16_t **out, size_t *out_size)
{
	int count, err;
	size_t src_size, dst_size;
	mbstate_t state;
	char16_t cu, *dst;
	const char *locale;

	src_size = *out_size;
	dst_size = 0;

	err = ERR_NO_MEMORY;
	dst = malloc(2 * src_size * sizeof(char16_t));
	if (dst == NULL)
		goto err0;

	locale = setlocale(LC_ALL, NULL);
	setlocale(LC_ALL, "en_US.utf8");
	memset(&state, 0, sizeof(state));

	while (src_size) {
		count = mbrtoc16(&cu, src, src_size, &state);
		if (count == -3)	/* Low Surr. */
			count = 0;
		else if (count == -2 || count == -1)
			break;
		else if (count == 0)	/* NUL char */
			count = 1;

		dst[dst_size++] = cu;
		src += count;
		src_size -= count;
	}
	setlocale(LC_ALL, locale);

	err = ERR_BAD_FILE;
	if (src_size)
		goto err1;

	err = ERR_NO_MEMORY;
	dst = realloc(dst, dst_size * sizeof(char16_t));
	if (dst == NULL)
		goto err1;

	*out_size = dst_size;
	*out = dst;
	return ERR_SUCCESS;
err1:
	free(dst);
err0:
	return err;
}

int main(int argc, char **argv)
{
	int i, len, err;
	FILE *files, *file;
	size_t size;
	char *src;
	const char16_t *dst;
	struct parser *parser;
	static char path[1024];

	if (argc != 2) {
		fprintf(stderr, "%s: Usage: %s paths.file\n", __func__, argv[0]);
		return ERR_INVALID_PARAMETER;
	}

	files = fopen(argv[1], "r");
	if (files == NULL) {
		fprintf(stderr, "%s: Error: Opening %s\n", __func__, argv[1]);
		return ERR_OPEN_FILE;
	}

	err = ERR_SUCCESS;
	while (fgets(path, 1024, files)) {
		len = strlen(path);

		/* Trim from right */
		for (i = len - 1; i >= 0; --i) {
			char c = path[i];
			if (c == ' ' || c == '\r' || c == '\n' || c == '\t') {
				path[i] = 0;
				continue;
			}
			break;
		}
		if (i < 0)
			continue;

		printf("%s: Opening %s\n", __func__, path);
		file = fopen(path, "rb");
		if (file == NULL) {
			fprintf(stderr, "%s: Error: Opening %s\n", __func__, path);
			continue;
		}

		err = ERR_NO_MEMORY;
		fseek(file, 0, SEEK_END);
		size = ftell(file);
		src = malloc(size + 1);
		if (src == NULL) {
			fprintf(stderr, "%s: Error: malloc(%ld)\n", __func__, size);
			fclose(file);
			break;
		}

		src[size] = 0;
		fseek(file, 0, SEEK_SET);	/* non-portable */
		fread(src, size, 1, file);
		fclose(file);

		err = mbr_to_c16(src, &dst, &size);
		free(src);
		if (err)
			break;

		/* ownership of dst passed */
		err = parser_new(dst, size, &parser);
		if (!err)
			err = parser_parse_script(parser);
		parser_delete(parser);
		break;
	}
	fclose(files);
	return err;
}
