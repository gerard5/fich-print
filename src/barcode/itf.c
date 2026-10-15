/*
 * BARCODE ITF generator
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

/* ITF: 'N' = Narrow, 'W' = Wide (each digit considtw of 5 elements 2 wide, 3 narrow) */
static const char *itf_digit_patterns[10] = {
	"NNWWN", /* digit 0 */
	"WNNNW", /* digit 1 */
	"NWNNW", /* digit 2 */
	"WWNNN", /* digit 3 */
	"NNWNW", /* digit 4 */
	"WNWNN", /* digit 5 */
	"NWWNN", /* digit 6 */
	"NNNWW", /* digit 7 */
	"WNNWN", /* digit 8 */
	"NWNWN"  /* digit 9 */
};

struct itf_ctx {
	uint8_t *pixels;
	int width;
	int height;
	int start_x;
	int start_y;
	int barcode_height;
	int module_width;
	int current_m;
};

static void itf_draw_raw_bits(const char *bits, struct itf_ctx *ctx)
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

static void itf_draw_pair(const char *p1, const char *p2, struct itf_ctx *ctx)
{
	for (int e = 0; e < 5; e++) {
		int black_width = (p1[e] == 'W') ? 2 : 1;
		for (int w = 0; w < black_width; w++) {
			for (int mw = 0; mw < ctx->module_width; mw++) {
				int x = ctx->start_x + ctx->current_m * ctx->module_width + mw;
				for (int y = 0; y < ctx->barcode_height; y++) {
					int py = ctx->start_y + y;
					if (x >= 0 && x < ctx->width && py >= 0 && py < ctx->height) {
						ctx->pixels[py * ctx->width + x] = PIXEL_BLACK;
					}
				}
			}
			ctx->current_m++;
		}

		int white_width = (p2[e] == 'W') ? 2 : 1;
		ctx->current_m += white_width;
	}
}

static int calculate_itf_checksum(const char *text)
{
	int len = (int)strlen(text);
	int sum = 0;
	int weight = 3;

	for (int i = len - 1; i >= 0; i--) {
		sum += (text[i] - '0') * weight;
		weight = (weight == 3) ? 1 : 3;
	}

	int next_ten = ((sum + 9) / 10) * 10;
	return next_ten - sum;
}

int create_barcode_ITF(struct gplane *plane, const char *text_str, bool use_checksum, const char *font_path, int width, int height)
{
	if (font_path == NULL || text_str == NULL) {
		return -1;
	}

	char full_text[256] = { 0 };
	strncpy(full_text, text_str, sizeof(full_text) - 2);
	full_text[sizeof(full_text) - 2] = '\0';

	size_t len = strlen(full_text);
	if (len == 0) {
		_log_error("ITF input cannot be empty");
		return -1;
	}
	for (size_t i = 0; i < len; i++) {
		if (full_text[i] < '0' || full_text[i] > '9') {
			_log_error("ITF supports digits only");
			return -1;
		}
	}

	if (use_checksum) {
		int chk = calculate_itf_checksum(full_text);
		full_text[len] = '0' + chk;
		full_text[len + 1] = '\0';
		len++;
	}

	if ((len % 2) != 0) {
		memmove(full_text + 1, full_text, len + 1);
		full_text[0] = '0';
		len++;
	}

	int total_modules = 4 + ((int)(len / 2) * 14) + 4;

	int module_width = (total_modules * 2 <= (width - 2)) ? 2 : 1;
	int barcode_width = total_modules * module_width;

	uint8_t *pixels = malloc(width * height * sizeof(uint8_t));
	if (pixels == NULL) {
		return -1;
	}
	memset(pixels, PIXEL_WHITE, width * height * sizeof(uint8_t));

	if (barcode_width > width) {
		_log_error("ITF width (%d px) exceeds label width (%d px)", barcode_width, width);
		free(pixels);
		return -1;
	}

	struct itf_ctx ctx = {
		.pixels = pixels,
		.width = width,
		.height = height,
		.start_x = (width - barcode_width) / 2,
		.start_y = 0,
		.barcode_height = 56,
		.module_width = module_width,
		.current_m = 0
	};

	itf_draw_raw_bits("1010", &ctx);

	for (size_t i = 0; i < len; i += 2) {
		int c1 = full_text[i] - '0';
		int c2 = full_text[i + 1] - '0';

		itf_draw_pair(itf_digit_patterns[c1], itf_digit_patterns[c2], &ctx);
	}

	itf_draw_raw_bits("1101", &ctx);

	if (load_pcf(font_path, 0) != 0) {
		free(pixels);
		return -1;
	}

	(void)plane_alloc(plane, pixels, width, height);

	int text_width = 0;
	for (size_t j = 0; j < len; j++) {
		text_width += get_pcf_char_step(0, (unsigned char)full_text[j], 8);
	}

	int text_x = (width - text_width) / 2;
	if (text_x < 2) {
		text_x = 2;
	}

	render_text(plane, 0, full_text, text_x, ctx.start_y + ctx.barcode_height + 4, 0);
	free_pcf(FREE_PCF_ALL);

	return 0;
}
