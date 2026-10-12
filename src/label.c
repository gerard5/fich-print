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

unsigned char *build_text_bitmap(const char *text_str, const char *font_path, int *out_height_pels)
{
	if (load_pcf(font_path, 0) < 0) {
		fprintf(stderr, "Unable to load font \"%s\"\n", font_path);
		return NULL;
	}

	static gplane_t label_plane;
	uint8_t *label_buf = malloc(LABEL_WIDTH * LABEL_HEIGHT * sizeof(uint8_t));
	if (label_buf == NULL) {
		free_pcf(FREE_PCF_ALL);
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

	char *str = strdup(text_str);
	if (str == NULL) {
		free(label_buf);
		free_pcf(FREE_PCF_ALL);
		perror("Failed to duplicate text string");
		return NULL;
	}

	unescape_string(str);
	render_text_format(&label_plane, 0, str);
	free(str);
	free_pcf(FREE_PCF_ALL);

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
