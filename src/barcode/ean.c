/*
 * BARCODE EAN5, EAN8, EAN13 generator
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

/* 1D Barcode encoding and rendering helpers */
static const char *eans_l_codes[10] = {
	"0001101", "0011001", "0010011", "0111101", "0100011",
	"0110001", "0101111", "0111011", "0110111", "0001011"
};

static const char *eans_g_codes[10] = {
	"0100111", "0110011", "0011011", "0100001", "0011101",
	"0111001", "0000101", "0010001", "0001001", "0010111"
};

static const char *eans_r_codes[10] = {
	"1110010", "1100110", "1101100", "1000010", "1011100",
	"1001110", "1010000", "1000100", "1001000", "1110100"
};

static const char *ean13_parities[10] = {
	"LLLLLL", "LLGLGG", "LLGGLG", "LLGGGL", "LGLLGG",
	"LGGLLG", "LGGGLG", "LGLGLG", "LGLGGL", "LGGLGL"
};

struct ean_ctx {
	uint8_t *pixels;
	int width;
	int height;
	int start_x;
	int start_y;
	int data_bar_height;
	int guard_bar_height;
	int module_width;
	int total_modules;
	int current_m;
};

static void ean_draw_pattern(const char *bits, struct ean_ctx *ctx)
{
	for (int b = 0; bits[b] != '\0'; b++) {
		if (bits[b] == '1') {
			bool is_guard = false;

			if (ctx->total_modules == 95) { /* EAN13 */
				is_guard = (ctx->current_m < 3) ||
					(ctx->current_m >= 45 && ctx->current_m < 50) ||
					(ctx->current_m >= 92);
			}
			else if (ctx->total_modules == 67) { /* EAN8 */
				is_guard = (ctx->current_m < 3) ||
					(ctx->current_m >= 31 && ctx->current_m < 36) ||
					(ctx->current_m >= 64);
			}
			else {
				/* EAN-5 (total_modules == 47) is_guard already is false */
			}

			int bar_height = is_guard ? ctx->guard_bar_height : ctx->data_bar_height;

			for (int w = 0; w < ctx->module_width; w++) {
				int x = ctx->start_x + ctx->current_m * ctx->module_width + w;
				for (int y = 0; y < bar_height; y++) {
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

int create_barcode_EAN13(struct gplane *plane, const char *text_str, const char *font_path, int width, int height)
{
	if (font_path == NULL || text_str == NULL) {
		return -1;
	}

	size_t len = strlen(text_str);
	if (len < 12 || len > 13) {
		_log_error("EAN-13 requires 12 or 13 digits");
		return -1;
	}

	char digits[14];
	for (size_t i = 0; i < len; i++) {
		if (!isdigit((unsigned char)text_str[i])) {
			_log_error("EAN-13 input must be numeric digits only");
			return -1;
		}
		digits[i] = text_str[i];
	}

	int sum = 0;
	for (int i = 0; i < 12; i++) {
		int d = digits[i] - '0';
		sum += (i % 2 == 0) ? d : (d * 3);
	}
	int expected_check = (10 - (sum % 10)) % 10;

	if (len == 13) {
		int actual_check = digits[12] - '0';
		if (actual_check != expected_check) {
			_log_error("EAN-13 checksum incorrect (%d != %d), replacing with valid checksum", actual_check, expected_check);
			digits[12] = '0' + expected_check;
		}
	}
	else {
		digits[12] = '0' + expected_check;
	}
	digits[13] = '\0';

	int total_modules = 95;
	int module_width = 2;
	int barcode_width = total_modules * module_width;

	uint8_t *pixels = malloc(width * height * sizeof(uint8_t));
	if (pixels == NULL) {
		return -1;
	}
	memset(pixels, PIXEL_WHITE, width * height * sizeof(uint8_t));

	if (barcode_width > width) {
		_log_error("EAN-13 barcode width (%d px) exceeds label width (%d px)", barcode_width, width);
		free(pixels);
		return -1;
	}

	struct ean_ctx ctx = {
		.pixels = pixels,
		.width = width,
		.height = height,
		.start_x = (width - barcode_width) / 2,
		.start_y = 0,
		.data_bar_height = height - 35,
		.guard_bar_height = height - 25,
		.module_width = module_width,
		.total_modules = total_modules,
		.current_m = 0
	};

	int first_digit = digits[0] - '0';
	const char *parity = ean13_parities[first_digit];

	ean_draw_pattern("101", &ctx);

	for (int i = 0; i < 6; i++) {
		int d = digits[1 + i] - '0';
		if (parity[i] == 'L') {
			ean_draw_pattern(eans_l_codes[d], &ctx);
		}
		else {
			ean_draw_pattern(eans_g_codes[d], &ctx);
		}
	}

	ean_draw_pattern("01010", &ctx);

	for (int i = 0; i < 6; i++) {
		int d = digits[7 + i] - '0';
		ean_draw_pattern(eans_r_codes[d], &ctx);
	}

	ean_draw_pattern("101", &ctx);

	if (load_pcf(font_path, 0) != 0) {
		free(pixels);
		return -1;
	}

	(void)plane_alloc(plane, pixels, width, height);

	char first_txt[2] = { digits[0], '\0' };

	char left_txt[7];
	strncpy(left_txt, digits + 1, 6);
	left_txt[6] = '\0';

	char right_txt[7];
	strncpy(right_txt, digits + 7, 6);
	right_txt[6] = '\0';

	int text_y = ctx.start_y + ctx.data_bar_height;

	render_text(plane, 0, first_txt, ctx.start_x - 14, text_y, 0);
	render_text(plane, 0, left_txt, ctx.start_x + 10, text_y, 0);
	render_text(plane, 0, right_txt, ctx.start_x + 106, text_y, 0);

	int barcode_total_width = 95 * ctx.module_width;
	render_text(plane, 0, ">", ctx.start_x + barcode_total_width + 4, text_y, 0);

	free_pcf(FREE_PCF_ALL);

	return 0;
}

int create_barcode_EAN8(struct gplane *plane, const char *text_str, const char *font_path, int width, int height)
{
	if (font_path == NULL || text_str == NULL) {
		return -1;
	}

	size_t len = strlen(text_str);
	if (len < 7 || len > 8) {
		_log_error("EAN-8 requires 7 or 8 digits");
		return -1;
	}

	char digits[9];
	for (size_t i = 0; i < len; i++) {
		if (!isdigit((unsigned char)text_str[i])) {
			_log_error("EAN-8 input must be numeric digits only");
			return -1;
		}
		digits[i] = text_str[i];
	}

	int sum = 0;
	int weights[7] = { 3, 1, 3, 1, 3, 1, 3 };
	for (int i = 0; i < 7; i++) {
		sum += (digits[i] - '0') * weights[i];
	}
	int expected_check = (10 - (sum % 10)) % 10;

	if (len == 8) {
		int actual_check = digits[7] - '0';
		if (actual_check != expected_check) {
			_log_error("EAN-8 checksum incorrect (%d != %d), replacing with valid checksum", actual_check, expected_check);
			digits[7] = '0' + expected_check;
		}
	}
	else {
		digits[7] = '0' + expected_check;
	}
	digits[8] = '\0';

	int total_modules = 67;
	int module_width = 2;
	int barcode_width = total_modules * module_width;

	uint8_t *pixels = malloc(width * height * sizeof(uint8_t));
	if (pixels == NULL) {
		return -1;
	}
	memset(pixels, PIXEL_WHITE, width * height * sizeof(uint8_t));

	if (barcode_width > width) {
		_log_error("EAN-8 barcode width (%d px) exceeds label width (%d px)", barcode_width, width);
		free(pixels);
		return -1;
	}

	struct ean_ctx ctx = {
		.pixels = pixels,
		.width = width,
		.height = height,
		.start_x = (width - barcode_width) / 2,
		.start_y = 0,
		.data_bar_height = height - 35,
		.guard_bar_height = height - 25,
		.module_width = module_width,
		.total_modules = total_modules,
		.current_m = 0
	};

	ean_draw_pattern("101", &ctx);

	for (int i = 0; i < 4; i++) {
		int d = digits[i] - '0';
		ean_draw_pattern(eans_l_codes[d], &ctx);
	}

	ean_draw_pattern("01010", &ctx);

	for (int i = 0; i < 4; i++) {
		int d = digits[4 + i] - '0';
		ean_draw_pattern(eans_r_codes[d], &ctx);
	}

	ean_draw_pattern("101", &ctx);

	if (load_pcf(font_path, 0) != 0) {
		free(pixels);
		return -1;
	}

	(void)plane_alloc(plane, pixels, width, height);

	char left_txt[5];
	strncpy(left_txt, digits, 4);
	left_txt[4] = '\0';

	char right_txt[5];
	strncpy(right_txt, digits + 4, 4);
	right_txt[4] = '\0';

	int text_y = ctx.start_y + ctx.data_bar_height;

	render_text(plane, 0, left_txt, ctx.start_x + 10, text_y, 0);
	render_text(plane, 0, right_txt, ctx.start_x + 72, text_y, 0);

	int barcode_total_width = 67 * ctx.module_width;
	render_text(plane, 0, "<", ctx.start_x - 14, text_y, 0);
	render_text(plane, 0, ">", ctx.start_x + barcode_total_width + 4, text_y, 0);


	free_pcf(FREE_PCF_ALL);

	return 0;
}

int create_barcode_EAN5(struct gplane *plane, const char *text_str, const char *font_path, int width, int height)
{
	if (font_path == NULL || text_str == NULL) {
		return -1;
	}

	size_t len = strlen(text_str);
	if (len < 5) {
		_log_error("EAN-5 requires 5 digits");
		return -1;
	}

	for (int i = 0; i < 5; i++) {
		if (!isdigit((unsigned char)text_str[i])) {
			_log_error("EAN-5 input must be numeric digits only");
			return -1;
		}
	}

	int d1 = text_str[0] - '0';
	int d2 = text_str[1] - '0';
	int d3 = text_str[2] - '0';
	int d4 = text_str[3] - '0';
	int d5 = text_str[4] - '0';
	int checksum = (3 * (d1 + d3 + d5) + 9 * (d2 + d4)) % 10;

	static const char *ean5_parities[10] = {
		"GGLLL", "GLGLL", "GLLGL", "GLLLG", "LGGLL",
		"LLGGL", "LLLGG", "LGLGL", "LGLLG", "LLGLG"
	};
	const char *parity = ean5_parities[checksum];

	int total_modules = 47;
	int module_width = 2;
	int barcode_width = total_modules * module_width;

	uint8_t *pixels = malloc(width * height * sizeof(uint8_t));
	if (pixels == NULL) {
		return -1;
	}
	memset(pixels, PIXEL_WHITE, width * height * sizeof(uint8_t));

	if (barcode_width > width) {
		_log_error("EAN-5 barcode width (%d px) exceeds label width (%d px)", barcode_width, width);
		free(pixels);
		return -1;
	}

	struct ean_ctx ctx = {
		.pixels = pixels,
		.width = width,
		.height = height,
		.start_x = (width - barcode_width) / 2,
		.start_y = 0,
		.data_bar_height = height - 35,
		.guard_bar_height = height - 25,
		.module_width = module_width,
		.total_modules = total_modules,
		.current_m = 0
	};

	ean_draw_pattern("1011", &ctx);

	for (int i = 0; i < 5; i++) {
		int d = text_str[i] - '0';

		if (parity[i] == 'L') {
			ean_draw_pattern(eans_l_codes[d], &ctx);
		}
		else {
			ean_draw_pattern(eans_g_codes[d], &ctx);
		}

		if (i < 4) {
			ean_draw_pattern("01", &ctx);
		}
	}

	if (load_pcf(font_path, 0) != 0) {
		free(pixels);
		return -1;
	}

	(void)plane_alloc(plane, pixels, width, height);

	char txt[6];
	strncpy(txt, text_str, 5);
	txt[5] = '\0';

	int text_y = ctx.start_y + ctx.data_bar_height;
	render_text(plane, 0, txt, ctx.start_x + 18, text_y, 0);
	free_pcf(FREE_PCF_ALL);

	return 0;
}
