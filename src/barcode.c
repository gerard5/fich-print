/*
 * BARCODE EAN5, EAN13, ITF, CODE128 (A,B,C) generator
 * Copyright 2026 Gerard Świderski
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "font_pcf.h"
#include "barcode.h"

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

int create_barcode_EAN13(struct gplane *plane, const char *text_str, const char *font_path, int width, int height)
{
	if (font_path == NULL || text_str == NULL) {
		return -1;
	}

	size_t len = strlen(text_str);
	if (len < 12 || len > 13) {
		fprintf(stderr, "Error: EAN-13 requires 12 or 13 digits\n");
		return -1;
	}

	char digits[14];
	for (size_t i = 0; i < len; i++) {
		if (!isdigit((unsigned char)text_str[i])) {
			fprintf(stderr, "Error: EAN-13 input must be numeric digits only\n");
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
			fprintf(stderr, "Warning: EAN-13 checksum incorrect (%d != %d), replacing with valid checksum\n", actual_check, expected_check);
			digits[12] = '0' + expected_check;
		}
		digits[13] = '\0';
	}
	else {
		digits[12] = '0' + expected_check;
		digits[13] = '\0';
	}

	uint8_t *label_buf = malloc(width * height * sizeof(uint8_t));
	if (label_buf == NULL) {
		return -1;
	}
	memset(label_buf, 0xff, width * height * sizeof(uint8_t));

	char pattern[128] = "";
	int first_digit = digits[0] - '0';
	const char *parity = ean13_parities[first_digit];

	strcat(pattern, "101");

	for (int i = 0; i < 6; i++) {
		int d = digits[1 + i] - '0';
		if (parity[i] == 'L') {
			strcat(pattern, eans_l_codes[d]);
		}
		else {
			strcat(pattern, eans_g_codes[d]);
		}
	}

	strcat(pattern, "01010");

	for (int i = 0; i < 6; i++) {
		int d = digits[7 + i] - '0';
		strcat(pattern, eans_r_codes[d]);
	}

	strcat(pattern, "101");

	int module_width = 2;
	int total_modules = (int)strlen(pattern);
	int barcode_width = total_modules * module_width;
	int data_bar_height = height - 35;
	int guard_bar_height = height - 25;

	int start_x = (width - barcode_width) / 2;
	int start_y = 0;

	for (int m = 0; m < total_modules; m++) {
		if (pattern[m] == '1') {
			bool is_guard = (m < 3) || (m >= 45 && m < 50) || (m >= 92);
			int bar_height = is_guard ? guard_bar_height : data_bar_height;

			for (int w = 0; w < module_width; w++) {
				int x = start_x + m * module_width + w;
				for (int y = 0; y < bar_height; y++) {
					int py = start_y + y;
					if (x >= 0 && x < width && py >= 0 && py < height) {
						label_buf[py * width + x] = 0;
					}
				}
			}
		}
	}

	char first_txt[2] = { digits[0], '\0' };
	char left_txt[7];
	strncpy(left_txt, digits + 1, 6);
	left_txt[6] = '\0';
	char right_txt[7];
	strncpy(right_txt, digits + 7, 6);
	right_txt[6] = '\0';

	int text_y = start_y + data_bar_height;

	if (load_pcf(font_path, 0) != 0) {
		free(label_buf);
		return -1;
	}

	plane->pixels = label_buf;
	plane->width = width;
	plane->height = height;
	plane->stride = width * sizeof(uint8_t);

	render_text(plane, 0, first_txt, start_x - 14, text_y, 0);
	render_text(plane, 0, left_txt, start_x + 10, text_y, 0);
	render_text(plane, 0, right_txt, start_x + 106, text_y, 0);
	free_pcf(FREE_PCF_ALL);

	return 0;
}

int create_barcode_EAN5(struct gplane *plane, const char *text_str, const char *font_path, int width, int height)
{
	if (font_path == NULL || text_str == NULL) {
		return -1;
	}

	if (strlen(text_str) < 5) {
		fprintf(stderr, "Error: EAN-5 requires 5 digits\n");
		return -1;
	}

	for (int i = 0; i < 5; i++) {
		if (!isdigit((unsigned char)text_str[i])) {
			fprintf(stderr, "Error: EAN-5 input must be numeric digits only\n");
			return -1;
		}
	}

	uint8_t *label_buf = malloc(width * height * sizeof(uint8_t));
	if (label_buf == NULL) {
		return -1;
	}
	memset(label_buf, 0xff, width * height * sizeof(uint8_t));

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

	char pattern[256] = "";
	strcat(pattern, "1011");

	for (int i = 0; i < 5; i++) {
		int d = text_str[i] - '0';
		if (parity[i] == 'L') {
			strcat(pattern, eans_l_codes[d]);
		}
		else {
			strcat(pattern, eans_g_codes[d]);
		}
		if (i < 4) {
			strcat(pattern, "01");
		}
	}

	int module_width = 2;
	int total_modules = (int)strlen(pattern);
	int barcode_width = total_modules * module_width;
	int barcode_height = height - 35;

	int start_x = (width - barcode_width) / 2;
	int start_y = 0;

	for (int m = 0; m < total_modules; m++) {
		if (pattern[m] == '1') {
			for (int w = 0; w < module_width; w++) {
				int x = start_x + m * module_width + w;
				for (int y = 0; y < barcode_height; y++) {
					int py = start_y + y;
					if (x >= 0 && x < width && py >= 0 && py < height) {
						label_buf[py * width + x] = 0;
					}
				}
			}
		}
	}

	char txt[6];
	strncpy(txt, text_str, 5);
	txt[5] = '\0';

	if (load_pcf(font_path, 0) != 0) {
		free(label_buf);
		return -1;
	}

	plane->pixels = label_buf;
	plane->width = width;
	plane->height = height;
	plane->stride = width * sizeof(uint8_t);

	render_text(plane, 0, txt, start_x + 18, start_y + barcode_height, 0);
	free_pcf(FREE_PCF_ALL);

	return 0;
}

/* Modulo 10 checksum (optional) */
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

	char pattern[4096] = { 0 };
	char full_text[256] = { 0 };
	strncpy(full_text, text_str, sizeof(full_text) - 2);
	full_text[sizeof(full_text) - 2] = '\0';

	size_t len = strlen(full_text);
	if (len == 0) {
		fprintf(stderr, "Error: ITF input cannot be empty\n");
		return -1;
	}
	for (size_t i = 0; i < len; i++) {
		if (full_text[i] < '0' || full_text[i] > '9') {
			fprintf(stderr, "Error: ITF supports digits only\n");
			return -1;
		}
	}

	if (use_checksum |= 0) {
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

	uint8_t *label_buf = malloc(width * height * sizeof(uint8_t));
	if (label_buf == NULL) {
		return -1;
	}
	memset(label_buf, 0xff, width * height * sizeof(uint8_t));

	/* Start pattern */
	strcat(pattern, "1010");

	for (size_t i = 0; i < len; i += 2) {
		int c1 = full_text[i] - '0';
		int c2 = full_text[i + 1] - '0';

		const char *p1 = itf_digit_patterns[c1]; /*  Lines (black) */
		const char *p2 = itf_digit_patterns[c2]; /*  Spaces (white) */

		/* Interleave one by one, each digit consists of 5 elements */
		for (int e = 0; e < 5; e++) {
			/* Black line (c1) */
			if (p1[e] == 'W') {
				strcat(pattern, "11");
			}
			else {
				strcat(pattern, "1");
			}

			/* White space (c2) */
			if (p2[e] == 'W') {
				strcat(pattern, "00");
			}
			else {
				strcat(pattern, "0");
			}
		}
	}

	/* Stop pattern */
	strcat(pattern, "1101");

	int total_modules = (int)strlen(pattern);
	int module_width = (total_modules * 2 <= (width - 16)) ? 2 : 1;
	int barcode_width = total_modules * module_width;

	if (barcode_width > width) {
		fprintf(stderr, "Error: ITF width (%d px) exceeds label width (%d px)\n", barcode_width, width);
		free(label_buf);
		return -1;
	}

	int barcode_height = 56;
	int start_x = (width - barcode_width) / 2;
	int start_y = 0;

	for (int m = 0; m < total_modules; m++) {
		if (pattern[m] == '1') {
			for (int w = 0; w < module_width; w++) {
				int x = start_x + m * module_width + w;
				for (int y = 0; y < barcode_height; y++) {
					int py = start_y + y;
					if (x >= 0 && x < width && py >= 0 && py < height) {
						label_buf[py * width + x] = 0;
					}
				}
			}
		}
	}

	if (load_pcf(font_path, 0) != 0) {
		free(label_buf);
		return -1;
	}

	plane->pixels = label_buf;
	plane->width = width;
	plane->height = height;
	plane->stride = width * sizeof(uint8_t);

	int text_width = 0;
	for (size_t j = 0; j < strlen(full_text); j++) {
		text_width += get_pcf_char_step(0, (unsigned char)full_text[j], 8);
	}

	int text_x = (width - text_width) / 2;
	if (text_x < 2) {
		text_x = 2;
	}

	render_text(plane, 0, full_text, text_x, start_y + barcode_height + 4, 0);
	free_pcf(FREE_PCF_ALL);

	return 0;
}

int create_barcode_CODE128(struct gplane *plane, const char *text_str, char mode, const char *font_path, int width, int height)
{
	if (font_path == NULL || text_str == NULL) {
		return -1;
	}

	char pattern[2048] = { 0 };

	size_t len = strlen(text_str);
	if (len == 0) {
		fprintf(stderr, "Error: Code 128 input cannot be empty\n");
		return -1;
	}

	mode |= 0x20; /* lower case */

	if (mode == 'c') {
		if (len % 2 != 0) {
			fprintf(stderr, "Error: Code 128C requires an even number of digits\n");
			return -1;
		}
		for (size_t i = 0; i < len; i++) {
			if (text_str[i] < '0' || text_str[i] > '9') {
				fprintf(stderr, "Error: Code 128C supports digits only ('%c' found)\n", text_str[i]);
				return -1;
			}
		}
	}
	else if (mode == 'a') {
		for (size_t i = 0; i < len; i++) {
			unsigned char ch = (unsigned char)text_str[i];
			if (ch > 95) {
				fprintf(stderr, "Error: Unsupported Code 128A character '%c'\n", ch);
				return -1;
			}
		}
	}
	else if (mode == 'b') {
		for (size_t i = 0; i < len; i++) {
			unsigned char ch = (unsigned char)text_str[i];
			if (ch < 32 || ch > 126) {
				fprintf(stderr, "Error: Unsupported Code 128B character '%c' (ASCII %d)\n", ch, ch);
				return -1;
			}
		}
	}
	else {
		fprintf(stderr, "Error: Unsupported Code 128 mode '%c'\n", mode);
		return -1;
	}

	uint8_t *label_buf = malloc(width * height * sizeof(uint8_t));
	if (label_buf == NULL) {
		return -1;
	}
	memset(label_buf, 0xff, width * height * sizeof(uint8_t));

	int start_val = (mode == 'a') ? 103 : ((mode == 'b') ? 104 : 105);
	int checksum = start_val;

	strcat(pattern, code128_patterns[start_val]);

	size_t i = 0;
	int position = 1;

	while (i < len) {
		int val = 0;

		if (mode == 'c') {
			val = (text_str[i] - '0') * 10 + (text_str[i + 1] - '0');
			i += 2;
		}
		else if (mode == 'a') {
			unsigned char ch = (unsigned char)text_str[i];
			val = (ch < 32) ? ch + 64 : ch - 32;
			i++;
		}
		else {
			val = (unsigned char)text_str[i] - 32;
			i++;
		}

		checksum += position * val;
		strcat(pattern, code128_patterns[val]);
		position++;
	}

	int check_val = checksum % 103;
	strcat(pattern, code128_patterns[check_val]);
	strcat(pattern, code128_patterns[106]);

	int total_modules = (int)strlen(pattern);
	int module_width = (total_modules * 2 <= (width - 16)) ? 2 : 1;
	int barcode_width = total_modules * module_width;

	if (barcode_width > width) {
		fprintf(stderr, "Error: Code 128 barcode width (%d px) exceeds label width (%d px)\n", barcode_width, width);
		free(label_buf);
		return -1;
	}

	int barcode_height = height - 35;
	int start_x = (width - barcode_width) / 2;
	int start_y = 0;

	for (int m = 0; m < total_modules; m++) {
		if (pattern[m] == '1') {
			for (int w = 0; w < module_width; w++) {
				int x = start_x + m * module_width + w;
				for (int y = 0; y < barcode_height; y++) {
					int py = start_y + y;
					if (x >= 0 && x < width && py >= 0 && py < height) {
						label_buf[py * width + x] = 0;
					}
				}
			}
		}
	}

	if (load_pcf(font_path, 0) != 0) {
		free(label_buf);
		return -1;
	}

	plane->pixels = label_buf;
	plane->width = width;
	plane->height = height;
	plane->stride = width * sizeof(uint8_t);

	int text_width = 0;
	for (size_t j = 0; j < len; j++) {
		text_width += get_pcf_char_step(0, (unsigned char)text_str[j], 8);
	}

	int text_x = (width - text_width) / 2;
	if (text_x < 2) {
		text_x = 2;
	}

	render_text(plane, 0, text_str, text_x, start_y + barcode_height, 0);
	free_pcf(FREE_PCF_ALL);

	return 0;
}
