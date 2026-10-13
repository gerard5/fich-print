/*
 * Label bitmap generator, loader and preview
 * Copyright 2026 Gerard Świderski
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <dmtx.h>

#include "printer.h"
#include "font_pcf.h"
#include "label.h"

#define LABEL_WIDTH  (PRINTER_DEFAULT_HEIGHT_PELS)
#define LABEL_HEIGHT (PRINTER_WIDTH_PELS)

static void unescape_string(char *str)
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
					if (c >= '0' && c <= '9')
						digit_val = c - '0';
					else if (c >= 'a' && c <= 'f')
						digit_val = c - 'a' + 10;
					else if (c >= 'A' && c <= 'F')
						digit_val = c - 'A' + 10;

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
	*dst = '\0';
}

static void pbm_skip_comments(FILE *f)
{
	int c;
	while ((c = fgetc(f)) != EOF) {
		if (c == '#') {
			while ((c = fgetc(f)) != EOF && c != '\n')
				;
		}
		else if (!isspace(c)) {
			ungetc(c, f);
			break;
		}
	}
}

unsigned char *load_pbm_file(const char *filename, int *out_height)
{
	FILE *f = fopen(filename, "r");
	if (f == NULL) {
		perror("Failed to open PBM file");
		return NULL;
	}

	char magic[3];
	if (fscanf(f, "%2s", magic) != 1 || magic[0] != 'P' || magic[1] != '1') {
		fprintf(stderr, "Invalid or unsupported PBM format (expected P1)\n");
		fclose(f);
		return NULL;
	}

	pbm_skip_comments(f);
	int width = 0, height = 0;
	if (fscanf(f, "%d %d", &width, &height) != 2) {
		fprintf(stderr, "Failed to read PBM dimensions\n");
		fclose(f);
		return NULL;
	}

	if (width != PRINTER_WIDTH_BYTES * 8) {
		fprintf(stderr, "Warning: PBM width is %d pixels, printer expects exactly %d pixels\n", width, PRINTER_WIDTH_BYTES * 8);
	}

	size_t bitmap_size = PRINTER_WIDTH_BYTES * height;
	unsigned char *bitmap_data = (unsigned char *)calloc(1, bitmap_size);
	if (bitmap_data == NULL) {
		perror("Failed to allocate bitmap memory for PBM");
		fclose(f);
		return NULL;
	}

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			pbm_skip_comments(f);
			int val = 0;
			if (fscanf(f, "%d", &val) != 1) {
				fprintf(stderr, "Unexpected end of PBM data at y=%d, x=%d\n", y, x);
				free(bitmap_data);
				fclose(f);
				return NULL;
			}
			if (val != 0) {
				int byte_idx = y * PRINTER_WIDTH_BYTES + (x >> 3);
				int bit_idx = ~x & 7;
				bitmap_data[byte_idx] |= (1 << bit_idx);
			}
		}
	}

	fclose(f);
	*out_height = height;
	return bitmap_data;
}

unsigned char *generate_test_pattern(int height_pels)
{
	size_t bitmap_size = PRINTER_WIDTH_BYTES * height_pels;
	unsigned char *bitmap_data = (unsigned char *)calloc(1, bitmap_size);
	if (bitmap_data == NULL) {
		perror("Failed to allocate test pattern memory");
		return NULL;
	}

	int width_pels = PRINTER_WIDTH_BYTES * 8;
	for (int y = 0; y < height_pels; y++) {
		for (int x = 0; x < width_pels; x++) {
			bool is_border = (y == 0 || y == height_pels - 1 || x == 0 || x == width_pels - 1);
			bool is_grid = ((x & 3) == 0 || (y & 3) == 0);
			if (is_border || is_grid) {
				int byte_idx = y * PRINTER_WIDTH_BYTES + (x >> 3);
				int bit_idx = ~x & 7;
				bitmap_data[byte_idx] |= (1 << bit_idx);
			}
		}
	}

	return bitmap_data;
}

unsigned char *build_text_bitmap(const char *text_str, const char *font_path, int *target_height_pels)
{
	if (load_pcf(font_path, 0) < 0) {
		fprintf(stderr, "Unable to load font \"%s\"\n", font_path);
		return NULL;
	}

	char *str = strdup(text_str);
	if (str == NULL) {
		free_pcf(FREE_PCF_ALL);
		perror("Failed to duplicate text string");
		return NULL;
	}
	unescape_string(str);

	static gplane_t label_plane = { 0 };

	if (*target_height_pels > 0) {
		label_plane.width = *target_height_pels;
	}
	else {
		label_plane.width = 0;
	}

	render_text_format(&label_plane, 0, str);

	if (label_plane.width < 32) {
		label_plane.width = 32;
	}
	label_plane.stride = label_plane.width;
	*target_height_pels = label_plane.width;

	if (label_plane.height > PRINTER_WIDTH_PELS) {
		fprintf(stderr, "Błąd: Tekst jest za wysoki (%d px) dla głowicy 96 px!\n", label_plane.height);
		free(str);
		free_pcf(FREE_PCF_ALL);
		return NULL;
	}

	label_plane.height = PRINTER_WIDTH_PELS;

	label_plane.stride_bytes = label_plane.width * sizeof(uint8_t);
	label_plane.size_bytes = label_plane.width * label_plane.height * sizeof(uint8_t);
	label_plane.size_area = label_plane.width * label_plane.height;

	uint8_t *label_buf = malloc(label_plane.size_bytes);
	if (label_buf == NULL) {
		free(str);
		free_pcf(FREE_PCF_ALL);
		perror("Failed to allocate bitmap memory for label");
		return NULL;
	}
	label_plane.pixels = label_buf;

	memset(label_plane.pixels, 0xff, label_plane.size_bytes);

	render_text_format(&label_plane, 0, str);
	free(str);
	free_pcf(FREE_PCF_ALL);

	int height = label_plane.width;
	int width = label_plane.height;

	const int printer_width_bytes = 12;
	size_t bitmap_size = printer_width_bytes * height;

	unsigned char *bitmap_data = (unsigned char *)calloc(1, bitmap_size);
	if (bitmap_data == NULL) {
		free(label_buf);
		perror("Failed to allocate bitmap memory for printer buffer");
		return NULL;
	}

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			int pixel = label_buf[label_plane.width * (width - x - 1) + y];
			if (pixel == 0) {
				int byte_idx = y * printer_width_bytes + (x >> 3);
				int bit_idx = ~x & 7;
				bitmap_data[byte_idx] |= (1 << bit_idx);
			}
		}
	}

	free(label_buf);
	return bitmap_data;
}

unsigned char *build_datamatrix_bitmap(const char *text_str, int *out_height_pels)
{
	static gplane_t label_plane;
	uint8_t *label_buf = malloc(LABEL_WIDTH * LABEL_HEIGHT * sizeof(uint8_t));
	if (label_buf == NULL) {
		perror("Failed to allocate bitmap memory for label");
		return NULL;
	}

	label_plane.pixels = (uint8_t *)label_buf;
	label_plane.width = LABEL_WIDTH;
	label_plane.height = LABEL_HEIGHT;
	label_plane.stride = LABEL_WIDTH;
	label_plane.stride_bytes = LABEL_WIDTH * sizeof(uint8_t);
	label_plane.size_bytes = LABEL_WIDTH * LABEL_HEIGHT * sizeof(uint8_t);
	label_plane.size_area = LABEL_WIDTH * LABEL_HEIGHT;

	memset(label_plane.pixels, 0xff, label_plane.size_bytes);

	DmtxEncode *enc = dmtxEncodeCreate();
	if (enc == NULL) {
		fprintf(stderr, "Failed to create libdmtx encoder\n");
		free(label_buf);
		return NULL;
	}

	dmtxEncodeSetProp(enc, DmtxPropSizeRequest, DmtxSymbolSquareAuto);
	dmtxEncodeSetProp(enc, DmtxPropModuleSize, 2);
	dmtxEncodeSetProp(enc, DmtxPropMarginSize, 4);

	int messageLen = strlen(text_str);
	if (dmtxEncodeDataMatrix(enc, messageLen, (unsigned char *)text_str) == DmtxFail) {
		fprintf(stderr, "Data Matrix encoding failed\n");
		dmtxEncodeDestroy(&enc);
		free(label_buf);
		return NULL;
	}

	int dm_width = dmtxImageGetProp(enc->image, DmtxPropWidth);
	int dm_height = dmtxImageGetProp(enc->image, DmtxPropHeight);
	int dm_bytes_per_pixel = dmtxImageGetProp(enc->image, DmtxPropBytesPerPixel);

	if (dm_height > LABEL_HEIGHT || dm_width > LABEL_WIDTH) {
		fprintf(stderr, "Error: Generated Data Matrix exceeds label bounds\n");
		dmtxEncodeDestroy(&enc);
		free(label_buf);
		return NULL;
	}

	int offset_x = 0;
	int offset_y = (LABEL_HEIGHT - dm_height) / 2;

	for (int y = 0; y < dm_height; y++) {
		for (int x = 0; x < dm_width; x++) {
			unsigned char *dm_pixel = enc->image->pxl + ((dm_height - 1 - y) * dm_width + x) * dm_bytes_per_pixel;
			if (*dm_pixel < 128) {
				int target_x = offset_x + x;
				int target_y = offset_y + y;
				label_buf[target_y * LABEL_WIDTH + target_x] = 0x00;
			}
		}
	}

	dmtxEncodeDestroy(&enc);

	int height = LABEL_WIDTH;
	int width = LABEL_HEIGHT;
	*out_height_pels = LABEL_WIDTH;

	size_t bitmap_size = PRINTER_WIDTH_BYTES * height;
	unsigned char *bitmap_data = (unsigned char *)calloc(1, bitmap_size);
	if (bitmap_data == NULL) {
		free(label_buf);
		perror("Failed to allocate bitmap memory for printer buffer");
		return NULL;
	}

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			int pixel = label_buf[LABEL_WIDTH * (width - x - 1) + y];
			if (pixel == 0) {
				int byte_idx = y * PRINTER_WIDTH_BYTES + (x >> 3);
				int bit_idx = ~x & 7;
				bitmap_data[byte_idx] |= (1 << bit_idx);
			}
		}
	}

	free(label_buf);
	return bitmap_data;
}

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

static unsigned char *finalize_label_bitmap(const uint8_t *label_buf, int *out_height_pels)
{
	int height = LABEL_WIDTH;
	int width = LABEL_HEIGHT;
	*out_height_pels = LABEL_WIDTH;

	size_t bitmap_size = PRINTER_WIDTH_BYTES * height;
	unsigned char *bitmap_data = (unsigned char *)calloc(1, bitmap_size);
	if (bitmap_data == NULL) {
		return NULL;
	}

	for (int y = 0; y < height; y++) {
		for (int x = 0; x < width; x++) {
			int pixel = label_buf[LABEL_WIDTH * (width - x - 1) + y];
			if (pixel == 0) {
				int byte_idx = y * PRINTER_WIDTH_BYTES + (x >> 3);
				int bit_idx = ~x & 7;
				bitmap_data[byte_idx] |= (1 << bit_idx);
			}
		}
	}

	return bitmap_data;
}

unsigned char *build_ean13_bitmap(const char *text_str, const char *font_path, int *out_height_pels)
{
	size_t len = strlen(text_str);
	if (len < 12 || len > 13) {
		fprintf(stderr, "Error: EAN-13 requires 12 or 13 digits\n");
		return NULL;
	}

	char digits[14];
	for (size_t i = 0; i < len; i++) {
		if (!isdigit((unsigned char)text_str[i])) {
			fprintf(stderr, "Error: EAN-13 input must be numeric digits only\n");
			return NULL;
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

	uint8_t *label_buf = malloc(LABEL_WIDTH * LABEL_HEIGHT * sizeof(uint8_t));
	if (label_buf == NULL) {
		return NULL;
	}
	memset(label_buf, 0xff, LABEL_WIDTH * LABEL_HEIGHT * sizeof(uint8_t));

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
	int data_bar_height = 56;
	int guard_bar_height = 64;

	int start_x = (LABEL_WIDTH - barcode_width) / 2;
	int start_y = 1;

	for (int m = 0; m < total_modules; m++) {
		if (pattern[m] == '1') {
			bool is_guard = (m < 3) || (m >= 45 && m < 50) || (m >= 92);
			int bar_height = is_guard ? guard_bar_height : data_bar_height;

			for (int w = 0; w < module_width; w++) {
				int x = start_x + m * module_width + w;
				for (int y = 0; y < bar_height; y++) {
					int py = start_y + y;
					if (x >= 0 && x < LABEL_WIDTH && py >= 0 && py < LABEL_HEIGHT) {
						label_buf[py * LABEL_WIDTH + x] = 0;
					}
				}
			}
		}
	}

	if (font_path && load_pcf(font_path, 0) == 0) {
		static gplane_t plane;
		plane.pixels = label_buf;
		plane.width = LABEL_WIDTH;
		plane.height = LABEL_HEIGHT;
		plane.stride = LABEL_WIDTH;

		char first_txt[2] = { digits[0], '\0' };
		char left_txt[7];
		strncpy(left_txt, digits + 1, 6);
		left_txt[6] = '\0';
		char right_txt[7];
		strncpy(right_txt, digits + 7, 6);
		right_txt[6] = '\0';

		int text_y = start_y + data_bar_height + 4;
		render_text(&plane, 0, first_txt, start_x - 14, text_y, 0);
		render_text(&plane, 0, left_txt, start_x + 10, text_y, 0);
		render_text(&plane, 0, right_txt, start_x + 106, text_y, 0);
		free_pcf(FREE_PCF_ALL);
	}

	unsigned char *bitmap_data = finalize_label_bitmap(label_buf, out_height_pels);
	free(label_buf);
	return bitmap_data;
}

unsigned char *build_ean5_bitmap(const char *text_str, const char *font_path, int *out_height_pels)
{
	if (strlen(text_str) < 5) {
		fprintf(stderr, "Error: EAN-5 requires 5 digits\n");
		return NULL;
	}

	for (int i = 0; i < 5; i++) {
		if (!isdigit((unsigned char)text_str[i])) {
			fprintf(stderr, "Error: EAN-5 input must be numeric digits only\n");
			return NULL;
		}
	}

	uint8_t *label_buf = malloc(LABEL_WIDTH * LABEL_HEIGHT * sizeof(uint8_t));
	if (label_buf == NULL) {
		return NULL;
	}
	memset(label_buf, 0xff, LABEL_WIDTH * LABEL_HEIGHT * sizeof(uint8_t));

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
	int barcode_height = 56;

	int start_x = (LABEL_WIDTH - barcode_width) / 2;
	int start_y = 1;

	for (int m = 0; m < total_modules; m++) {
		if (pattern[m] == '1') {
			for (int w = 0; w < module_width; w++) {
				int x = start_x + m * module_width + w;
				for (int y = 0; y < barcode_height; y++) {
					int py = start_y + y;
					if (x >= 0 && x < LABEL_WIDTH && py >= 0 && py < LABEL_HEIGHT) {
						label_buf[py * LABEL_WIDTH + x] = 0;
					}
				}
			}
		}
	}

	if (font_path && load_pcf(font_path, 0) == 0) {
		static gplane_t plane;
		plane.pixels = label_buf;
		plane.width = LABEL_WIDTH;
		plane.height = LABEL_HEIGHT;
		plane.stride = LABEL_WIDTH;

		char txt[6];
		strncpy(txt, text_str, 5);
		txt[5] = '\0';
		render_text(&plane, 0, txt, start_x + 18, start_y + barcode_height + 4, 0);
		free_pcf(FREE_PCF_ALL);
	}

	unsigned char *bitmap_data = finalize_label_bitmap(label_buf, out_height_pels);
	free(label_buf);
	return bitmap_data;
}

unsigned char *build_code128_bitmap(const char *text_str, const char *font_path, int *out_height_pels)
{
	size_t len = strlen(text_str);
	if (len == 0) {
		fprintf(stderr, "Error: Code 128 input cannot be empty\n");
		return NULL;
	}

	for (size_t i = 0; i < len; i++) {
		unsigned char ch = (unsigned char)text_str[i];
		if (ch < 32 || ch > 126) {
			fprintf(stderr, "Error: Unsupported Code 128 character '%c' (ASCII %d)\n", ch, ch);
			return NULL;
		}
	}

	uint8_t *label_buf = malloc(LABEL_WIDTH * LABEL_HEIGHT * sizeof(uint8_t));
	if (label_buf == NULL) {
		return NULL;
	}
	memset(label_buf, 0xff, LABEL_WIDTH * LABEL_HEIGHT * sizeof(uint8_t));

	/* Code 128 Code Set B: Start B index = 104 */
	int checksum = 104;
	char pattern[2048] = "";
	strcat(pattern, code128_patterns[104]);

	for (size_t i = 0; i < len; i++) {
		int val = (unsigned char)text_str[i] - 32;
		checksum += (int)(i + 1) * val;
		strcat(pattern, code128_patterns[val]);
	}

	int check_val = checksum % 103;
	strcat(pattern, code128_patterns[check_val]);
	strcat(pattern, code128_patterns[106]); /* Stop pattern */

	int total_modules = (int)strlen(pattern);
	int module_width = (total_modules * 2 <= (LABEL_WIDTH - 16)) ? 2 : 1;
	int barcode_width = total_modules * module_width;

	if (barcode_width > LABEL_WIDTH) {
		fprintf(stderr, "Error: Code 128 barcode width (%d px) exceeds label width (%d px)\n", barcode_width, LABEL_WIDTH);
		free(label_buf);
		return NULL;
	}

	int barcode_height = 56;
	int start_x = (LABEL_WIDTH - barcode_width) / 2;
	int start_y = 1;

	for (int m = 0; m < total_modules; m++) {
		if (pattern[m] == '1') {
			for (int w = 0; w < module_width; w++) {
				int x = start_x + m * module_width + w;
				for (int y = 0; y < barcode_height; y++) {
					int py = start_y + y;
					if (x >= 0 && x < LABEL_WIDTH && py >= 0 && py < LABEL_HEIGHT) {
						label_buf[py * LABEL_WIDTH + x] = 0;
					}
				}
			}
		}
	}

	if (font_path && load_pcf(font_path, 0) == 0) {
		static gplane_t plane;
		plane.pixels = label_buf;
		plane.width = LABEL_WIDTH;
		plane.height = LABEL_HEIGHT;
		plane.stride = LABEL_WIDTH;

		int text_width = 0;
		for (size_t i = 0; i < len; i++) {
			text_width += get_pcf_char_step(0, (unsigned char)text_str[i], 8);
		}

		int text_x = (LABEL_WIDTH - text_width) / 2;
		if (text_x < 2)
			text_x = 2;
		render_text(&plane, 0, text_str, text_x, start_y + barcode_height + 4, 0);
		free_pcf(FREE_PCF_ALL);
	}

	unsigned char *bitmap_data = finalize_label_bitmap(label_buf, out_height_pels);
	free(label_buf);
	return bitmap_data;
}

bool print_preview(const unsigned char *bitmap_data, int height_pels)
{
	for (int y = 0; y < height_pels; y++) {
		printf("|");
		for (int x = 0; x < 96; x++) {
			int byte_idx = y * PRINTER_WIDTH_BYTES + (x >> 3);
			int bit_idx = ~x & 7;
			if (bitmap_data[byte_idx] & (1 << bit_idx)) {
				printf("#");
			}
			else {
				printf(" ");
			}
		}
		printf("|\n");
	}

	printf("Proceed with printing? [y/N]: ");
	int c = getchar();
	return (c == 'y' || c == 'Y');
}
