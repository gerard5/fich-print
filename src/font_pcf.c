/*
 * PCF font loader, text and bitmap processing
 * Copyright 2026 Gerard Świderski
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "font_pcf.h"

#ifndef FONT_DIR
#define FONT_DIR "/usr/local/share/fichero/fonts"
#endif

#define PCF_MAGIC_LE 0x70636601
#define PCF_MAGIC_BE 0x01666370

#define PCF_METRICS       (1 << 2)
#define PCF_BITMAPS       (1 << 3)
#define PCF_BDF_ENCODINGS (1 << 5)

#define PCF_GLYPH_PAD_MASK (3 << 0)
#define PCF_BYTE_MASK      (1 << 2)
#define PCF_BIT_MASK       (1 << 3)

#define MAX_TYPEFACES 8

typedef struct {
	uint8_t *raw_data;
	size_t file_size;
	bool is_file_be;

	bool metrics_compressed;
	bool metrics_be;
	const uint8_t *metrics_ptr;
	uint32_t metrics_glyph_count;

	const uint8_t *bitmaps_ptr;
	int bitmap_be;
	int bit_swap;
	int glyph_pad;
	const uint8_t *bitmap_offsets_start;
	const uint8_t *bitmap_data_start;

	const uint8_t *encodings_ptr;
	int enc_be;
	int16_t min_byte2, max_byte2;
	int16_t min_byte1, max_byte1;
	const uint8_t *glyph_indices_start;

	int default_width;
	int default_height;
	int font_ascent;
	int font_descent;
} typeface_t;

static typeface_t typefaces[MAX_TYPEFACES];

static inline uint16_t mem_read16(const uint8_t *ptr, bool be)
{
	return be ? (((uint16_t)ptr[0] << 8) | ptr[1]) : (((uint16_t)ptr[1] << 8) | ptr[0]);
}

static inline uint32_t mem_read32(const uint8_t *ptr, bool be)
{
	return be ?
		((uint32_t)ptr[0] << 24) | ((uint32_t)ptr[1] << 16) | ((uint32_t)ptr[2] << 8) | ptr[3] :
		((uint32_t)ptr[3] << 24) | ((uint32_t)ptr[2] << 16) | ((uint32_t)ptr[1] << 8) | ptr[0];
}

static inline uint8_t reverse_bits(uint8_t b)
{
	b = (b & 0xF0) >> 4 | (b & 0x0F) << 4;
	b = (b & 0xCC) >> 2 | (b & 0x33) << 2;
	b = (b & 0xAA) >> 1 | (b & 0x55) << 1;
	return b;
}

/* Helper function decoding UTF-8 to a single Unicode codepoint */
static int next_utf8_codepoint(const char **text)
{
	unsigned char c = (unsigned char)(**text);
	if (c == '\0') {
		return 0;
	}

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

static uint32_t find_table_in_memory(const uint8_t *buf, uint32_t type, bool file_be, uint32_t *out_format)
{
	uint32_t table_count = mem_read32(&buf[4], file_be);
	const uint8_t *toc = &buf[8];

	for (uint32_t i = 0; i < table_count; i++) {
		uint32_t t_type = mem_read32(&toc[i * 16], file_be);
		uint32_t t_format = mem_read32(&toc[i * 16 + 4], file_be);
		uint32_t t_offset = mem_read32(&toc[i * 16 + 12], file_be);
		if (t_type == type) {
			if (out_format) {
				*out_format = t_format;
			}
			return t_offset;
		}
	}
	return 0;
}

int free_pcf(int slot)
{
	if (slot == FREE_PCF_ALL) {
		for (int i = 0; i < MAX_TYPEFACES; i++) {
			free(typefaces[i].raw_data);
			typefaces[i].raw_data = NULL;
		}
		return 0;
	}
	else {
		if (slot >= 0 && slot < MAX_TYPEFACES) {
			free(typefaces[slot].raw_data);
			typefaces[slot].raw_data = NULL;
			return 0;
		}
	}
	return -1;
}

int load_pcf(const char *filename, int slot)
{
	char font_path_buf[1024];

	if (slot < 0 || slot >= MAX_TYPEFACES) {
		return -1;
	}

	FILE *f = fopen(filename, "rb");
	if (f == NULL && strchr(filename, '/') == NULL) {
		snprintf(font_path_buf, sizeof(font_path_buf) - 1, "%s/%s", FONT_DIR, filename);
		f = fopen(font_path_buf, "rb");
		if (f == NULL) {
			fprintf(stderr, "Cannot open PCF file: %s\n", font_path_buf);
		}
	}

	if (f == NULL) {
		fprintf(stderr, "Cannot open PCF file: %s\n", filename);
		return -1;
	}

	/* Get file size */
	(void)fseek(f, 0, SEEK_END);
	size_t size = ftell(f);
	(void)fseek(f, 0, SEEK_SET);

	uint8_t *buf = malloc(size);
	if (buf == NULL || fread(buf, 1, size, f) != size) {
		free(buf);
		fclose(f);
		return -1;
	}
	fclose(f);

	bool file_be = false;
	uint32_t magic = mem_read32(buf, 0);

	if (magic == PCF_MAGIC_LE) {
		file_be = false;
	}
	else if (magic == PCF_MAGIC_BE) {
		file_be = true;
	}
	else {
		free(buf);
		return -1;
	}

	if (typefaces[slot].raw_data) {
		free(typefaces[slot].raw_data);
	}
	memset(&typefaces[slot], 0, sizeof(typeface_t));

	typeface_t *tf = &typefaces[slot];
	tf->raw_data = buf;
	tf->file_size = size;
	tf->is_file_be = file_be;

	uint32_t fmt;
	uint32_t off;

	off = find_table_in_memory(buf, PCF_METRICS, file_be, &fmt);
	if (off == 0) {
		goto bad_file;
	}

	tf->metrics_ptr = &buf[off];
	tf->metrics_be = !!(fmt & PCF_BYTE_MASK);
	tf->metrics_compressed = !!(fmt & 0x100);
	tf->metrics_glyph_count = tf->metrics_compressed ?
		mem_read16(&tf->metrics_ptr[4], tf->metrics_be) :
		mem_read32(&tf->metrics_ptr[4], tf->metrics_be);

	off = find_table_in_memory(buf, PCF_BITMAPS, file_be, &fmt);
	if (off == 0) {
		goto bad_file;
	}

	tf->bitmaps_ptr = &buf[off];
	tf->bitmap_be = (fmt & PCF_BYTE_MASK);
	tf->bit_swap = !(fmt & PCF_BIT_MASK);
	tf->glyph_pad = 1 << (fmt & PCF_GLYPH_PAD_MASK);

	tf->bitmap_offsets_start = &tf->bitmaps_ptr[8];
	uint32_t b_count = mem_read32(&tf->bitmaps_ptr[4], tf->bitmap_be);
	tf->bitmap_data_start = tf->bitmap_offsets_start + (b_count * 4) + 16;

	off = find_table_in_memory(buf, PCF_BDF_ENCODINGS, file_be, &fmt);
	if (off == 0) {
		goto bad_file;
	}

	tf->encodings_ptr = &buf[off];
	tf->enc_be = (fmt & PCF_BYTE_MASK);
	tf->min_byte2 = (int16_t)mem_read16(&tf->encodings_ptr[4], tf->enc_be);
	tf->max_byte2 = (int16_t)mem_read16(&tf->encodings_ptr[6], tf->enc_be);
	tf->min_byte1 = (int16_t)mem_read16(&tf->encodings_ptr[8], tf->enc_be);
	tf->max_byte1 = (int16_t)mem_read16(&tf->encodings_ptr[10], tf->enc_be);
	tf->glyph_indices_start = &tf->encodings_ptr[14];
	int max_w = 0;
	int max_h = 0;

	for (uint32_t i = 0; i < tf->metrics_glyph_count; i++) {
		int16_t w = 0, h = 0;

		if (tf->metrics_compressed) {
			const uint8_t *m = &tf->metrics_ptr[6 + (i * 5)];
			int16_t left = (int16_t)m[0] - 0x80;
			int16_t right = (int16_t)m[1] - 0x80;
			int16_t ac = (int16_t)m[3] - 0x80;
			int16_t de = (int16_t)m[4] - 0x80;

			h = ac + de;
			w = right - left;
		}
		else {
			const uint8_t *m = &tf->metrics_ptr[8 + (i * 12)];
			int16_t left = (int16_t)mem_read16(&m[0], tf->metrics_be);
			int16_t right = (int16_t)mem_read16(&m[2], tf->metrics_be);
			int16_t ac = (int16_t)mem_read16(&m[6], tf->metrics_be);
			int16_t de = (int16_t)mem_read16(&m[8], tf->metrics_be);

			h = ac + de;
			w = right - left;
		}

		if (w > max_w) {
			max_w = w;
		}
		if (h > max_h) {
			max_h = h;
		}
	}

	tf->default_width = (max_w > 0) ? max_w : 8;
	tf->default_height = (max_h > 0) ? max_h : 16;
	tf->font_ascent = tf->default_height;
	tf->font_descent = 0;

	printf("Loaded PCF into slot %d (%s, maximum font dimensions: %dx%d)\n",
		slot, filename, tf->default_width, tf->default_height);
	return 0;

bad_file:
	free(buf);

	return -1;
}

int get_pcf_char_step(int typeface_idx, int codepoint, int default_w)
{
	if (typeface_idx < 0 || typeface_idx >= MAX_TYPEFACES) {
		return default_w;
	}

	typeface_t *tf = &typefaces[typeface_idx];
	if (tf->raw_data == NULL) {
		return default_w;
	}

	int16_t byte2 = codepoint & 0xFF;
	int16_t byte1 = (codepoint >> 8) & 0xFF;

	if (byte2 < tf->min_byte2 || byte2 > tf->max_byte2 || byte1 < tf->min_byte1 || byte1 > tf->max_byte1) {
		return default_w;
	}

	int enc_idx = (byte1 - tf->min_byte1) * (tf->max_byte2 - tf->min_byte2 + 1) + (byte2 - tf->min_byte2);
	uint16_t glyph_idx = mem_read16(tf->glyph_indices_start + (enc_idx * 2), tf->enc_be);
	if (glyph_idx == 0xFFFF || glyph_idx >= tf->metrics_glyph_count) {
		return default_w;
	}

	if (tf->metrics_compressed) {
		const uint8_t *m = &tf->metrics_ptr[6 + (glyph_idx * 5)];
		return (int16_t)m[2] - 0x80;
	}
	else {
		const uint8_t *m = &tf->metrics_ptr[8 + (glyph_idx * 12)];
		return (int16_t)mem_read16(&m[4], tf->metrics_be);
	}
}

int render_char(gplane_t *plane, int typeface_idx, int c, int x, int y, uint8_t color)
{
	if (typeface_idx < 0 || typeface_idx >= MAX_TYPEFACES) {
		return -1;
	}

	typeface_t *tf = &typefaces[typeface_idx];
	if (tf->raw_data == NULL) {
		return -1;
	}

	int16_t byte2 = c & 0xFF;
	int16_t byte1 = (c >> 8) & 0xFF;

	if (byte2 < tf->min_byte2 || byte2 > tf->max_byte2 ||
		byte1 < tf->min_byte1 || byte1 > tf->max_byte1) {
		return -1;
	}

	int enc_idx = (byte1 - tf->min_byte1) * (tf->max_byte2 - tf->min_byte2 + 1) + (byte2 - tf->min_byte2);
	uint16_t glyph_idx = mem_read16(tf->glyph_indices_start + (enc_idx * 2), tf->enc_be);
	if (glyph_idx == 0xFFFF || glyph_idx >= tf->metrics_glyph_count) {
		return -1;
	}

	int width = 0, height = 0, x_offset = 0, y_offset = 0, dwidth_x = 0;
	if (tf->metrics_compressed) {
		const uint8_t *m = tf->metrics_ptr + 6 + (glyph_idx * 5);
		int16_t left = (int16_t)m[0] - 0x80;
		int16_t right = (int16_t)m[1] - 0x80;
		int16_t width_c = (int16_t)m[2] - 0x80;
		int16_t ac = (int16_t)m[3] - 0x80;
		int16_t de = (int16_t)m[4] - 0x80;

		x_offset = left;
		width = right - left;
		dwidth_x = width_c;
		height = ac + de;
		y_offset = -de;
	}
	else {
		const uint8_t *m = tf->metrics_ptr + 8 + (glyph_idx * 12);
		x_offset = (int16_t)mem_read16(m + 0, tf->metrics_be);
		int16_t r = (int16_t)mem_read16(m + 2, tf->metrics_be);
		width = r - x_offset;
		dwidth_x = (int16_t)mem_read16(m + 4, tf->metrics_be);
		int16_t ac = (int16_t)mem_read16(m + 6, tf->metrics_be);
		int16_t de = (int16_t)mem_read16(m + 8, tf->metrics_be);
		height = ac + de;
		y_offset = -de;
	}

	if (width <= 0 && dwidth_x > 0) {
		width = dwidth_x;
	}

	uint32_t bitmap_off = mem_read32(tf->bitmap_offsets_start + (glyph_idx * 4), tf->bitmap_be);
	const uint8_t *glyph_bits = tf->bitmap_data_start + bitmap_off;

	int bytes_per_row = ((width + 7) / 8);
	bytes_per_row = ((bytes_per_row + tf->glyph_pad - 1) / tf->glyph_pad) * tf->glyph_pad;

	int start_x = x + x_offset;
	int start_y = y + (tf->font_ascent - y_offset - height);
	uint8_t *pixels = (uint8_t *)plane->pixels;
	size_t plane_width = plane->width;

	for (int row = 0; row < height; row++) {
		const uint8_t *row_bytes_start = glyph_bits + (row * bytes_per_row);

		for (int col = 0; col < width; col++) {
			int byte_idx = col >> 3;
			int bit_idx = col & 7;

			uint8_t current_byte = row_bytes_start[byte_idx];

			if (tf->bit_swap) {
				current_byte = reverse_bits(current_byte);
			}

			if (current_byte & (0x80 >> bit_idx)) {
				int px = (start_x + col);
				int py = (start_y + row);

				if (((unsigned int)px < plane->width) && ((unsigned int)py < plane->height)) {
					pixels[px + py * plane_width] = color;
				}
			}
		}
	}

	return 0;
}

/* Renders single line text */
void render_text(gplane_t *plane, int typeface_idx, const char *text, int x, int y, uint8_t color)
{
	if (text == NULL || plane == NULL || typeface_idx < 0 || typeface_idx >= MAX_TYPEFACES) {
		return;
	}

	int def_h = (typefaces[typeface_idx].raw_data) ? typefaces[typeface_idx].default_height : 16;
	int def_w = (typefaces[typeface_idx].raw_data) ? typefaces[typeface_idx].default_width : 8;

	bool dry_run = (plane->width == 0 || plane->height == 0);

	int current_x = x;
	const char *ptr = text;

	while (*ptr != '\0') {
		int codepoint = next_utf8_codepoint(&ptr);

		if (!dry_run) {
			render_char(plane, typeface_idx, codepoint, current_x, y, color);
		}

		int step = get_pcf_char_step(typeface_idx, codepoint, def_w);
		current_x += (step > 0) ? step : def_w;
	}

	if (dry_run) {
		if (plane->width == 0) {
			plane->width = (current_x - x);
		}
		if (plane->height == 0) {
			plane->height = def_h;
		}
		plane->stride = plane->width;
	}
}


/* Renders formatted multiline string (\f - left, \a - center \r - right, \b - toggle bold) */
void render_text_format(gplane_t *plane, int typeface_idx, const char *format_str)
{
	if (format_str == NULL || plane == NULL || typeface_idx < 0 || typeface_idx >= MAX_TYPEFACES) {
		return;
	}

	typeface_t *tf = &typefaces[typeface_idx];
	if (tf->raw_data == NULL) {
		return;
	}

	bool dry_run = (plane->width == 0 || plane->height == 0);

	int def_h = tf->default_height;
	int def_w = tf->default_width;
	int base_x = 0;

	int lines_count = 0;
	const char *count_ptr = format_str;
	while (*count_ptr) {
		if (*count_ptr++ == '\n') {
			lines_count++;
		}
	}

	int total_text_block_height = (lines_count + 1) * def_h;

	int base_y = 0;
	if (!dry_run) {
		base_y = ((int)plane->height - total_text_block_height) / 2;
		base_y -= 5;
	}


	bool is_bold = false;
	const char *ptr = format_str;
	int line_idx = 0;
	int max_estimated_width = 0;

	while (*ptr != '\0') {
		const char *line_end = ptr;
		while (*line_end != '\n' && *line_end != '\0') {
			line_end++;
		}

		char current_align = 'c';
		if (*ptr != '\a' && *ptr != '\r' && *ptr != '\f' && *ptr != '\n' && *ptr != '\0') {
			current_align = 'l';
		}

		const char *sub_ptr = ptr;
		int sub_width = 0;
		while (sub_ptr < line_end && *sub_ptr != '\0') {
			if (*sub_ptr == '\a' || *sub_ptr == '\r' || *sub_ptr == '\f') {
				if (*sub_ptr == '\a')
					current_align = 'c';
				else if (*sub_ptr == '\r')
					current_align = 'r';
				else if (*sub_ptr == '\f')
					current_align = 'l';
				sub_ptr++;
				continue;
			}
			if (*sub_ptr == '\b') {
				sub_ptr++;
				continue;
			}
			int cp = next_utf8_codepoint(&sub_ptr);
			int step = get_pcf_char_step(typeface_idx, cp, def_w);
			sub_width += step;
		}

		if (sub_width > max_estimated_width) {
			max_estimated_width = sub_width;
		}

		if (!dry_run) {
			int target_x = base_x;
			if (current_align == 'c') {
				target_x = ((int)plane->width - sub_width) / 2;
			}
			else if (current_align == 'r') {
				target_x = (int)plane->width - sub_width - base_x;
			}

			int current_x = target_x;
			int current_y = base_y + def_h * line_idx;
			const char *render_ptr = ptr;

			while (render_ptr < line_end && *render_ptr != '\0') {
				if (*render_ptr == '\a' || *render_ptr == '\r' || *render_ptr == '\f') {
					render_ptr++;
					continue;
				}
				if (*render_ptr == '\b') {
					render_ptr++;
					is_bold = !is_bold;
					continue;
				}

				int codepoint = next_utf8_codepoint(&render_ptr);
				render_char(plane, typeface_idx, codepoint, current_x, current_y, 0);
				if (is_bold) {
					render_char(plane, typeface_idx, codepoint, current_x - 1, current_y, 0);
				}

				int step = get_pcf_char_step(typeface_idx, codepoint, def_w);
				current_x += step;
			}
		}

		ptr = line_end;
		if (*ptr == '\n') {
			ptr++;
		}
		line_idx++;
	}

	if (dry_run) {
		if (plane->width == 0) {
			plane->width = max_estimated_width;
		}
		if (plane->height == 0) {
			plane->height = total_text_block_height;
		}
		plane->stride = plane->width;
	}
}
