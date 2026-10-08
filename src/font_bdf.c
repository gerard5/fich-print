/*
 * BDF font loader, text and bitmap processing
 * Copyright 2026, Gerard Świderski
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "font_bdf.h"

/* Structure supporting glyphs up to 16 pixels width (2 bytes per row) */
typedef struct {
	int width;
	int height;
	int x_offset;
	int y_offset;
	int dwidth_x;
	unsigned int bitmap[MAX_HEIGHT];
} Glyph;

static Glyph font[MAX_GLYPHS];
int font_ascent = 0;
int font_descent = 0;
int default_width = 8;
int default_height = 16;

/* Helper function decoding UTF-8 to a single Unicode codepoint */
static int next_utf8_codepoint(const char **text)
{
	unsigned char c = (unsigned char)(**text);
	if (c == '\0')
		return 0;

	(*text)++;

	/* 1 byte (Standard ASCII: 0xxxxxxx) */
	if (c < 0x80) {
		return c;
	}
	/* 2 bytes (Diacritical characters: 110xxxxx 10xxxxxx) */
	else if ((c & 0xE0) == 0xC0) {
		unsigned char next = (unsigned char)(**text);
		if ((next & 0xC0) == 0x80) {
			(*text)++;
			return ((c & 0x1F) << 6) | (next & 0x3F);
		}
	}
	/* 3 bytes (1110xxxx 10xxxxxx 10xxxxxx) */
	else if ((c & 0xF0) == 0xE0) {
		unsigned char next1 = (unsigned char)(**text);
		if ((next1 & 0xC0) == 0x80) {
			(*text)++;
			unsigned char next2 = (unsigned char)(**text);
			if ((next2 & 0xC0) == 0x80) {
				(*text)++;
				return ((c & 0x0F) << 12) | ((next1 & 0x3F) << 6) | (next2 & 0x3F);
			}
		}
	}
	return '?';
}

int load_bdf(const char *filename)
{
	FILE *file = fopen(filename, "r");
	if (!file) {
		fprintf(stderr, "Cannot open font file: %s\n", filename);
		return 0;
	}

	printf("loaded \"%s\"\n", filename);

	char line[256];
	int current_encoding = -1;
	int in_bitmap = 0;
	int bitmap_row = 0;

	memset(font, 0, sizeof(font));

	while (fgets(line, sizeof(line), file)) {
		line[strcspn(line, "\r\n")] = 0;

		if (strncmp(line, "FONTBOUNDINGBOX", 15) == 0) {
			sscanf(line, "FONTBOUNDINGBOX %d %d", &default_width, &default_height);
		}
		else if (strncmp(line, "FONT_ASCENT", 11) == 0) {
			sscanf(line, "FONT_ASCENT %d", &font_ascent);
		}
		else if (strncmp(line, "FONT_DESCENT", 12) == 0) {
			sscanf(line, "FONT_DESCENT %d", &font_descent);
		}
		else if (strncmp(line, "ENCODING", 8) == 0) {
			int enc = -1;
			sscanf(line, "ENCODING %d", &enc);
			if (enc >= 0 && enc < MAX_GLYPHS) {
				current_encoding = enc;
			}
			else {
				current_encoding = -1;
			}
		}
		else if (strncmp(line, "DWIDTH", 6) == 0) {
			if (current_encoding >= 0) {
				sscanf(line, "DWIDTH %d", &font[current_encoding].dwidth_x);
			}
		}
		else if (strncmp(line, "BBX", 3) == 0) {
			if (current_encoding >= 0) {
				sscanf(line, "BBX %d %d %d %d",
					&font[current_encoding].width,
					&font[current_encoding].height,
					&font[current_encoding].x_offset,
					&font[current_encoding].y_offset);
			}
		}
		else if (strncmp(line, "BITMAP", 6) == 0) {
			if (current_encoding >= 0) {
				in_bitmap = 1;
				bitmap_row = 0;
			}
		}
		else if (strncmp(line, "ENDCHAR", 7) == 0) {
			in_bitmap = 0;
			current_encoding = -1;
		}
		else if (in_bitmap && current_encoding >= 0) {
			if (bitmap_row < MAX_HEIGHT) {
				unsigned int val;
				if (sscanf(line, "%x", &val) == 1) {
					char *trimmed = line;
					while (*trimmed == ' ' || *trimmed == '\t') trimmed++;
					size_t hex_len = strspn(trimmed, "0123456789ABCDEFabcdef");

					if (hex_len == 2) {
						val <<= 24;
					}
					else if (hex_len == 4) {
						val <<= 16;
					}
					else if (hex_len == 6) {
						val <<= 8;
					}
					font[current_encoding].bitmap[bitmap_row++] = (unsigned int)val;
				}
			}
		}
	}

	if (font_ascent == 0)
		font_ascent = default_height;

	fclose(file);
	return 1;
}

void render_char(gplane_t *plane, int c, int x, int y, uint8_t color)
{
	if (c < 0 || c >= MAX_GLYPHS)
		return;

	Glyph g = font[c];

	int width = (g.width > 0) ? g.width : default_width;
	int height = (g.height > 0) ? g.height : default_height;

	int start_x = x + g.x_offset;
	int start_y = y + (font_ascent - g.y_offset - height);
	uint8_t *pixels = (uint8_t *)plane->pixels;
	size_t plane_width = plane->width;

	for (int row = 0; row < height; row++) {
		if (row >= MAX_HEIGHT)
			break;
		unsigned int bits = g.bitmap[row];
		for (int col = 0; col < width; col++) {
			if (col >= 32)
				break;

			if (bits & (0x80000000 >> col)) {
				int px = (start_x + col);
				int py = (start_y + row);
				if (((unsigned int)px < plane->width) && ((unsigned int)py < plane->height)) {
					pixels[px + py * plane_width] = color;
				}
			}
		}
	}
}

void render_text(gplane_t *plane, const char *text, int x, int y, uint8_t color)
{
	int current_x = x;
	while (*text) {
		int codepoint = next_utf8_codepoint(&text);

		render_char(plane, codepoint, current_x, y, color);

		int step = (font[codepoint].dwidth_x > 0) ? font[codepoint].dwidth_x : default_width;
		current_x += step;
	}
}

int save_pbm(gplane_t *plane, const char *filename, int obrot_90_cw)
{
	int w = plane->width;
	int h = plane->height;

	uint8_t *pixels = (uint8_t *)plane->pixels;

	FILE *f = fopen(filename, "w");
	if (!f) {
		fprintf(stderr, "Cannot open file for writing: %s\n", filename);
		return -1;
	}

	fprintf(f, "P1\n");
	fprintf(f, "# Thermal print imitation with rotation\n");

	if (obrot_90_cw) {
		fprintf(f, "%d %d\n", h, w);
	}
	else {
		fprintf(f, "%d %d\n", w, h);
	}

	if (obrot_90_cw) {
		for (int new_y = 0; new_y < w; new_y++) {
			for (int new_x = 0; new_x < h; new_x++) {
				int orig_x = new_y;
				int orig_y = (h - 1) - new_x;

				int pixel = pixels[orig_y * w + orig_x];
				fprintf(f, (pixel != 0) ? "0" : "1");

				if (new_x < h - 1) {
					fprintf(f, " ");
				}
			}
			fprintf(f, "\n");
		}
	}
	else {
		for (int y = 0; y < h; y++) {
			for (int x = 0; x < w; x++) {
				int pixel = pixels[y * w + x];
				fprintf(f, (pixel != 0) ? "0" : "1");

				if (x < w - 1) {
					fprintf(f, " ");
				}
			}
			fprintf(f, "\n");
		}
	}

	fclose(f);
	return 0;
}

void draw_label(gplane_t *plane, const char *label_str)
{
	if (!label_str || !plane)
		return;

	int base_x = 3;
	int base_y = ((int)plane->height - 3 * default_height) / 2 + default_height;
	bool is_bold = false;

	const char *ptr = label_str;
	int line_idx = 0;

	int lines = 0;
	while (*ptr) {
		if (*ptr++ == '\n') {
			lines++;
		}
	}

	if (lines > 3) {
		return;
	}

	base_y -= (default_height * lines / 2);
	ptr = label_str;

	while (*ptr && line_idx < 3) {
		const char *line_end = ptr;
		while (*line_end != '\n' && *line_end != '\0') {
			line_end++;
		}

		char current_align = 'c';
		if (*ptr != '\a' && *ptr != '\r' && *ptr != '\f' && *ptr != '\n' && *ptr != '\0') {
			current_align = 'l';
		}

		while (ptr < line_end) {
			if (*ptr == '\a' || *ptr == '\r' || *ptr == '\f') {
				if (*ptr == '\a')
					current_align = 'c';
				else if (*ptr == '\r')
					current_align = 'r';
				else if (*ptr == '\f')
					current_align = 'l';
				ptr++;
				continue;
			}

			const char *sub_ptr = ptr;
			int sub_width = 0;
			while (sub_ptr < line_end && *sub_ptr != '\a' && *sub_ptr != '\r' && *sub_ptr != '\f') {
				if (*sub_ptr == '\b') {
					sub_ptr++;
					continue;
				}
				int cp = next_utf8_codepoint(&sub_ptr);
				int step = (font[cp].dwidth_x > 0) ? font[cp].dwidth_x : default_width;
				sub_width += step;
			}

			int target_x = base_x;
			if (current_align == 'c') {
				target_x = (plane->width - sub_width) / 2;
			}
			else if (current_align == 'r') {
				target_x = plane->width - sub_width - base_x;
			}

			int current_x = target_x;
			int current_y = base_y + default_height * line_idx;
			while (ptr < line_end && *ptr != '\a' && *ptr != '\r' && *ptr != '\f') {
				if (*ptr == '\b') {
					if (++ptr >= line_end) {
						break;
					}
					is_bold = !is_bold;
				}
				int codepoint = next_utf8_codepoint(&ptr);
				render_char(plane, codepoint, current_x, current_y, 0);
				if (is_bold) {
					render_char(plane, codepoint, current_x - 1, current_y, 0);
				}

				int step = (font[codepoint].dwidth_x > 0) ? font[codepoint].dwidth_x : default_width;
				current_x += step;
			}
		}

		if (*ptr == '\n') {
			ptr++;
		}
		line_idx++;
	}
}
