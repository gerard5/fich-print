/*
 * BARCODE CODE128 (A,B,C) generator
 * Copyright 2026 Gerard Świderski
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "../image.h"
#include "../font_pcf.h"
#include "barcode.h"

#define PIXEL_WHITE 0xff
#define PIXEL_BLACK 0

#define _log_prefix          "barcode: "
#define _log(fmt, ...)       printf(_log_prefix fmt "\n", ##__VA_ARGS__)
#define _log_error(fmt, ...) fprintf(stderr, _log_prefix fmt "\n", ##__VA_ARGS__)

/* Code 128 pattern table (107 patterns, 11 bits each, plus stop 13 bits) */
static const char *code128_patterns[107] = {
	"11011001100", "11001101100", "11001100110", "10010011000", "10010001100",
	"10001001100", "10011001000", "10011000100", "10001100100", "11001001000",
	"11001000100", "11000100100", "10110011100", "10011011100", "10011001110",
	"10111001100", "10011101100", "10011100110", "11001110010", "11001011100",
	"11001001110", "11011100100", "11001110100", "11101101110", "11101001100",
	"11100101100", "11100100110", "11101100100", "11100110100", "11100110010",
	"11011011000", "11011000110", "11000110110", "10100011000", "10001011000",
	"10001000110", "10110001000", "10001101000", "10001100010", "11010001000",
	"11000101000", "11000100010", "10110111000", "10110001110", "10001101110",
	"10111011000", "10111000110", "10001110110", "11101110110", "11010001110",
	"11000101110", "11011101000", "11011100010", "11011101110", "11101011000",
	"11101000110", "11100010110", "11101101000", "11101100010", "11100011010",
	"11101111010", "11001000010", "11110001010", "10100110000", "10100001100",
	"10010110000", "10010000110", "10000101100", "10000100110", "10110010000",
	"10110000100", "10011010000", "10011000010", "10000110100", "10000110010",
	"11000010010", "11001010000", "11110111010", "11000010100", "10001111010",
	"10100111100", "10010111100", "10010011110", "10111100100", "10011110100",
	"10011110010", "11110100100", "11110010100", "11110010010", "11011011110",
	"11011110110", "11110110110", "10101111000", "10100011110", "10001011110",
	"10111101000", "10111100010", "11110101000", "11110100010", "10111011110",
	"10111101110", "11101011110", "11110101110", "11010000100", "11010010000",
	"11010011100", "1100011101011"
};

struct code128_ctx {
	uint8_t *pixels;
	int width;
	int height;
	int start_x;
	int start_y;
	int barcode_height;
	int module_width;
	int current_m;
};

static void code128_draw_pattern(const char *bits, struct code128_ctx *ctx)
{
	for (int b = 0; bits[b] != '\0'; b++) {
		if (bits[b] == '1') {
			for (int w = 0; w < ctx->module_width; w++) {
				int x = ctx->start_x + ctx->current_m * ctx->module_width + w;
				for (int y = 0; y < ctx->barcode_height; y++) {
					int py = ctx->start_y + y;
					if (x >= 0 && x < ctx->width && py >= 0 && py < ctx->height) {
						ctx->pixels[py * ctx->width + x] = PIXEL_BLACK;
					}
				}
			}
		}
		ctx->current_m++;
	}
}

int create_barcode_CODE128(struct gplane *plane, const char *text_str, int mode, const char *font_path, int width, int height)
{
	uint8_t tokens[256];

	if (font_path == NULL || text_str == NULL) {
		return -1;
	}

	if (mode < code128_mode_auto || mode > code128_mode_c) {
		_log_error("Invalid Code 128 mode");
		return -1;
	}

	size_t len = strlen(text_str);
	if (len == 0) {
		_log_error("Code 128 input cannot be empty");
		return -1;
	}

	if (len > 250) {
		_log_error("Code 128 input string is too long (max 250 characters)");
		return -1;
	}

	for (size_t i = 0; i < len; i++) {
		if ((unsigned char)text_str[i] > 127) {
			_log_error("Unsupported non-ASCII character at index %zu", i);
			return -1;
		}
	}

	int start_val = 104;
	if (mode == code128_mode_a) {
		start_val = 103;
	}
	else if (mode == code128_mode_c) {
		start_val = 105;
	}

	int checksum = start_val;
	int token_count = 0;

	int current_sub_mode = (mode == code128_mode_auto) ?
		code128_mode_b :
		mode;

	size_t i = 0;
	while (i < len) {
		if (text_str[i] == '\f') {
			tokens[token_count++] = 102; /* GS1-128 FNC1 */
			i++;

			/* If in Auto mode, after FNC1, follows min. 4 digits, optimize to C mode */
			if (mode == code128_mode_auto && current_sub_mode == code128_mode_b) {
				size_t consecutive_digits = 0;
				for (size_t j = i; j < len && text_str[j] >= '0' && text_str[j] <= '9'; j++) {
					consecutive_digits++;
				}
				if (consecutive_digits >= 4) {
					tokens[token_count++] = 99; /* CODE_C */
					current_sub_mode = code128_mode_c;
				}
			}
			continue;
		}

		if (mode == code128_mode_auto && current_sub_mode == code128_mode_b) {
			size_t consecutive_digits = 0;
			for (size_t j = i; j < len && text_str[j] >= '0' && text_str[j] <= '9'; j++) {
				consecutive_digits++;
			}
			if (consecutive_digits >= 4) {
				tokens[token_count++] = 99; /* CODE_C */
				current_sub_mode = code128_mode_c;
			}
		}

		if (mode == code128_mode_auto && current_sub_mode == code128_mode_c) {
			if (text_str[i] < '0' || text_str[i] > '9') {
				tokens[token_count++] = 100; /* CODE_B */
				current_sub_mode = code128_mode_b;
			}
		}

		int val = 0;
		if (current_sub_mode == code128_mode_c) {
			if (i + 1 >= len || text_str[i] < '0' || text_str[i] > '9' || text_str[i + 1] < '0' || text_str[i + 1] > '9') {
				_log_error("Code 128C/Auto mode requires an even number of digits");
				return -1;
			}
			val = (text_str[i] - '0') * 10 + (text_str[i + 1] - '0');
			i += 2;
		}
		else if (current_sub_mode == code128_mode_a) {
			unsigned char ch = (unsigned char)text_str[i];
			if (ch > 95) {
				_log_error("Invalid 128A char");
				return -1;
			}
			val = (ch < 32) ? ch + 64 : ch - 32;
			i++;
		}
		else if (current_sub_mode == code128_mode_b) {
			unsigned char ch = (unsigned char)text_str[i];
			if (ch < 32 || ch > 126) {
				_log_error("Invalid 128B char");
				return -1;
			}
			val = ch - 32;
			i++;
		}
		else {
			_log_error("Invalid Code 128 mode");
			return -1;
		}

		tokens[token_count++] = val;
	}

	for (int pos = 0; pos < token_count; pos++) {
		checksum += (pos + 1) * tokens[pos];
	}
	int check_val = checksum % 103;

	int total_modules = 11 + (token_count * 11) + 11 + 13;

	int module_width = (total_modules * 2 <= (width - 16)) ? 2 : 1;
	int barcode_width = total_modules * module_width;

	uint8_t *pixels = malloc(width * height * sizeof(uint8_t));
	if (pixels == NULL) {
		return -1;
	}
	memset(pixels, PIXEL_WHITE, width * height * sizeof(uint8_t));

	if (barcode_width > width) {
		_log_error("Barcode width (%d px) exceeds label width (%d px)", barcode_width, width);
		free(pixels);
		return -1;
	}

	struct code128_ctx ctx = {
		.pixels = pixels,
		.width = width,
		.height = height,
		.start_x = (width - barcode_width) / 2,
		.start_y = 0,
		.barcode_height = height - 35,
		.module_width = module_width,
		.current_m = 0
	};

	code128_draw_pattern(code128_patterns[start_val], &ctx);

	for (int t = 0; t < token_count; t++) {
		code128_draw_pattern(code128_patterns[tokens[t]], &ctx);
	}

	code128_draw_pattern(code128_patterns[check_val], &ctx);
	code128_draw_pattern(code128_patterns[106], &ctx);

	if (load_pcf(font_path, 0) != 0) {
		free(pixels);
		return -1;
	}

	(void)plane_alloc(plane, pixels, width, height);

	char human_readable[256] = { 0 };
	size_t hr_idx = 0;
	for (size_t j = 0; j < len && hr_idx < 255; j++) {
		if (text_str[j] != '\f') {
			human_readable[hr_idx++] = text_str[j];
		}
	}
	human_readable[hr_idx] = '\0';

	int text_width = 0;
	size_t hr_len = strlen(human_readable);
	for (size_t j = 0; j < hr_len; j++) {
		text_width += get_pcf_char_step(0, (unsigned char)human_readable[j], 8);
	}

	int text_x = (width - text_width) / 2;
	if (text_x < 2) {
		text_x = 2;
	}

	render_text(plane, 0, human_readable, text_x, ctx.start_y + ctx.barcode_height, 0);
	free_pcf(FREE_PCF_ALL);

	return 0;
}
