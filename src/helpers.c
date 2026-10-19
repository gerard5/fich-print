/*
 * Common functions and helpers
 * Copyright 2026 Gerard Świderski
 */

#include <ctype.h>
#include <string.h>

void unescape_string(char *str)
{
	char *src = str;
	char *dst = str;

	while (*src) {
		if (*src == '\\' && *(src + 1) != '\0') {
			src++;

			if (*src >= '0' && *src <= '7') {
				unsigned int octal_val = 0;
				int count = 0;

				while (count < 3 && *src >= '0' && *src <= '7') {
					octal_val = octal_val * 8 + (*src - '0');
					src++;
					count++;
				}
				*dst++ = (char)octal_val;
				src--;
			}
			else if (*src == 'x' || *src == 'X') {
				src++;
				unsigned int hex_val = 0;
				int count = 0;

				while (count < 2 && isxdigit((unsigned char)*src)) {
					char c = *src;
					int digit_val = 0;
					if (c >= '0' && c <= '9') {
						digit_val = c - '0';
					}
					else if (c >= 'a' && c <= 'f') {
						digit_val = c - 'a' + 10;
					}
					else if (c >= 'A' && c <= 'F') {
						digit_val = c - 'A' + 10;
					}

					hex_val = hex_val * 16 + digit_val;
					src++;
					count++;
				}

				if (count > 0) {
					*dst++ = (char)hex_val;
					src--;
				}
				else {
					*dst++ = '\\';
					*dst++ = *(src - 1);
					src--;
				}
			}
			else {
				switch (*src) {
					case 'n': *dst++ = '\n'; break;
					case 't': *dst++ = '\t'; break;
					case 'r': *dst++ = '\r'; break;
					case 'b': *dst++ = '\b'; break;
					case 'a': *dst++ = '\a'; break;
					case 'f': *dst++ = '\f'; break;
					case '\\': *dst++ = '\\'; break;
					case '\"': *dst++ = '\"'; break;
					default:
						*dst++ = '\\';
						*dst++ = *src;
						break;
				}
			}
		}
		else {
			*dst++ = *src;
		}
		src++;
	}

	while (dst < src) {
		*(dst++) = '\0';
	}
}

char *unescape_string_dup(const char *src)
{
	char *dst = (src != NULL) ?
		strdup(src) :
		NULL;

	if (dst != NULL) {
		unescape_string(dst);
	}

	return dst;
}
