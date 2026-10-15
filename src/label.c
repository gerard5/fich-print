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

#include "helpers.h"
#include "printer.h"
#include "font_pcf.h"
#include "label.h"
#include "barcode.h"

#define LABEL_WIDTH  (PRINTER_DEFAULT_HEIGHT_PELS)
#define LABEL_HEIGHT (PRINTER_WIDTH_PELS)

unsigned char *convert_to_monobitmap(gplane_t *plane)
{
	int src_height = plane->height;
	int src_stride = plane->width;

	/* dst to src is rotated 90 degrees clock-wise */
	int dst_height = plane->width;
	int dst_width = plane->height;
	int dst_stride = (dst_width + 7) >> 3;
	size_t bitmap_size = (dst_width * dst_height) >> 3;

	unsigned char *src_buf = plane->pixels;
	unsigned char *dst_buf = (unsigned char *)calloc(1, bitmap_size);
	if (dst_buf == NULL) {
		return NULL;
	}

	for (int y = 0; y < dst_height; y++) {
		for (int x = 0; x < dst_width; x++) {
			int pixel = src_buf[src_stride * ((src_height - 1) - x) + y];
			if (pixel == 0) {
				int byte_idx = dst_stride * y + (x >> 3);
				int bit_idx = ~x & 7;
				dst_buf[byte_idx] |= (1 << bit_idx);
			}
		}
	}

	return dst_buf;
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

int load_pbm_file(gplane_t *plane, const char *filename)
{
	FILE *f = fopen(filename, "r");
	if (f == NULL) {
		perror("Failed to open PBM file");
		return -1;
	}

	char magic[3];
	if (fscanf(f, "%2s", magic) != 1 || magic[0] != 'P' || magic[1] != '1') {
		fprintf(stderr, "Invalid or unsupported PBM format (expected P1)\n");
		fclose(f);
		return -1;
	}

	pbm_skip_comments(f);
	int width = 0, height = 0;
	if (fscanf(f, "%d %d", &width, &height) != 2) {
		fprintf(stderr, "Failed to read PBM dimensions\n");
		fclose(f);
		return -1;
	}

	unsigned char *label_buf = (unsigned char *)calloc(1, width * height);
	if (label_buf == NULL) {
		perror("Failed to allocate bitmap memory for PBM");
		fclose(f);
		return -1;
	}

	int total_pixels = width * height;
	int pixels_read = 0;
	int ch;

	while (pixels_read < total_pixels && (ch = fgetc(f)) != EOF) {
		if (ch == '#') {
			ungetc(ch, f);
			pbm_skip_comments(f);
			continue;
		}

		if (ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t') {
			continue;
		}

		if (ch == '0' || ch == '1') {
			label_buf[pixels_read] = (ch == '1') ? 0 : 0xff;
			pixels_read++;
		}
		else {
			fprintf(stderr, "Unexpected character '%c' in PBM data at pixel %d\n", ch, pixels_read);
			free(label_buf);
			fclose(f);
			return -1;
		}
	}

	if (pixels_read < total_pixels) {
		fprintf(stderr, "Unexpected end of PBM data. Read %d of %d pixels\n", pixels_read, total_pixels);
		free(label_buf);
		fclose(f);
		return -1;
	}

	fclose(f);

	plane->pixels = (uint8_t *)label_buf;
	plane->width = width;
	plane->height = height;
	plane->stride = width;
	plane->stride_bytes = width * sizeof(uint8_t);
	plane->size_bytes = width * height * sizeof(uint8_t);
	plane->size_area = width * height;

	return 0;
}

unsigned char *build_pbm_bitmap(const char *filename, int *out_height_pels)
{
	gplane_t plane;

	if (load_pbm_file(&plane, filename) < 0) {
		return NULL;
	}

	if (plane.height != PRINTER_WIDTH_PELS) {
		fprintf(stderr, "Error: PBM height is %d pixels, printer expects exactly %d pixels\n", plane.height, PRINTER_WIDTH_PELS);
		free(plane.pixels);

		return NULL;
	}

	*out_height_pels = PRINTER_DEFAULT_HEIGHT_PELS;
	unsigned char *bitmap_data = convert_to_monobitmap(&plane);

	free(plane.pixels);

	return bitmap_data;
}


unsigned char *build_testpattern_bitmap(int height_pels)
{
	gplane_t plane = { 0 };

	unsigned char *label_buf = calloc(1, LABEL_HEIGHT * height_pels);

	plane.pixels = (uint8_t *)label_buf;
	plane.width = height_pels;
	plane.height = LABEL_HEIGHT;
	plane.stride = height_pels;
	plane.stride_bytes = height_pels * sizeof(uint8_t);
	plane.size_bytes = height_pels * LABEL_HEIGHT * sizeof(uint8_t);
	plane.size_area = height_pels * LABEL_HEIGHT;

	memset(plane.pixels, 0xff, plane.size_bytes);

	for (int y = 0; y < (int)plane.height; y++) {
		for (int x = 0; x < (int)plane.width; x++) {
			bool is_border = (y == 0 || y == ((int)plane.height - 1) || x == 0 || x == ((int)plane.width - 1));
			bool is_grid = ((x & 3) == 0 || (y & 3) == 0);
			if (is_border || is_grid) {
				label_buf[plane.stride_bytes * y + x] = 0;
			}
		}
	}

	unsigned char *bitmap_data = convert_to_monobitmap(&plane);

	free(label_buf);
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

	static gplane_t plane = { 0 };

	if (*target_height_pels > 0) {
		plane.width = *target_height_pels;
	}
	else {
		plane.width = 0;
	}

	render_text_format(&plane, 0, str);

	if (plane.width < 32) {
		plane.width = 32;
	}
	plane.stride = plane.width;
	*target_height_pels = plane.width;

	if (plane.height > PRINTER_WIDTH_PELS) {
		fprintf(stderr, "Błąd: Tekst jest za wysoki (%d px) dla głowicy 96 px!\n", plane.height);
		free(str);
		free_pcf(FREE_PCF_ALL);
		return NULL;
	}

	plane.height = PRINTER_WIDTH_PELS;

	plane.stride_bytes = plane.width * sizeof(uint8_t);
	plane.size_bytes = plane.width * plane.height * sizeof(uint8_t);
	plane.size_area = plane.width * plane.height;

	uint8_t *label_buf = malloc(plane.size_bytes);
	if (label_buf == NULL) {
		free(str);
		free_pcf(FREE_PCF_ALL);
		perror("Failed to allocate bitmap memory for label");
		return NULL;
	}
	plane.pixels = label_buf;

	memset(plane.pixels, 0xff, plane.size_bytes);

	render_text_format(&plane, 0, str);
	free(str);
	free_pcf(FREE_PCF_ALL);

	*target_height_pels = PRINTER_DEFAULT_HEIGHT_PELS;
	unsigned char *bitmap_data = convert_to_monobitmap(&plane);

	free(label_buf);
	return bitmap_data;
}

unsigned char *build_datamatrix_bitmap(const char *text_str, int *out_height_pels)
{
	static gplane_t plane;
	uint8_t *label_buf = malloc(LABEL_WIDTH * LABEL_HEIGHT * sizeof(uint8_t));
	if (label_buf == NULL) {
		perror("Failed to allocate bitmap memory for label");
		return NULL;
	}

	plane.pixels = (uint8_t *)label_buf;
	plane.width = LABEL_WIDTH;
	plane.height = LABEL_HEIGHT;
	plane.stride = LABEL_WIDTH;
	plane.stride_bytes = LABEL_WIDTH * sizeof(uint8_t);
	plane.size_bytes = LABEL_WIDTH * LABEL_HEIGHT * sizeof(uint8_t);
	plane.size_area = LABEL_WIDTH * LABEL_HEIGHT;

	memset(plane.pixels, 0xff, plane.size_bytes);

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

	*out_height_pels = PRINTER_DEFAULT_HEIGHT_PELS;
	unsigned char *bitmap_data = convert_to_monobitmap(&plane);

	free(label_buf);
	return bitmap_data;
}

unsigned char *build_ean13_bitmap(const char *text_str, const char *font_path, int *out_height_pels)
{
	gplane_t plane;

	if (create_barcode_EAN13(&plane, text_str, font_path, LABEL_WIDTH, LABEL_HEIGHT) < 0) {
		return NULL;
	}

	*out_height_pels = PRINTER_DEFAULT_HEIGHT_PELS;
	unsigned char *bitmap_data = convert_to_monobitmap(&plane);

	free(plane.pixels);

	return bitmap_data;
}

unsigned char *build_ean5_bitmap(const char *text_str, const char *font_path, int *out_height_pels)
{
	gplane_t plane;

	if (create_barcode_EAN5(&plane, text_str, font_path, LABEL_WIDTH, LABEL_HEIGHT) < 0) {
		return NULL;
	}

	*out_height_pels = PRINTER_DEFAULT_HEIGHT_PELS;
	unsigned char *bitmap_data = convert_to_monobitmap(&plane);

	free(plane.pixels);

	return bitmap_data;
}

unsigned char *build_itf_bitmap(const char *text_str, int use_checksum, const char *font_path, int *out_height_pels)
{
	gplane_t plane;

	if (create_barcode_ITF(&plane, text_str, (use_checksum != 0), font_path, LABEL_WIDTH, LABEL_HEIGHT) < 0) {
		return NULL;
	}

	*out_height_pels = PRINTER_DEFAULT_HEIGHT_PELS;
	unsigned char *bitmap_data = convert_to_monobitmap(&plane);

	free(plane.pixels);

	return bitmap_data;
}

unsigned char *build_code128_bitmap(const char *text_str, char mode, const char *font_path, int *out_height_pels)
{
	gplane_t plane;

	if (create_barcode_CODE128(&plane, text_str, mode, font_path, LABEL_WIDTH, LABEL_HEIGHT) < 0) {
		return NULL;
	}

	*out_height_pels = PRINTER_DEFAULT_HEIGHT_PELS;
	unsigned char *bitmap_data = convert_to_monobitmap(&plane);

	free(plane.pixels);

	return bitmap_data;
}

bool save_bitmap_to_pbm(const char *filename, const unsigned char *bitmap_data, int width, int height)
{
	if (bitmap_data == NULL || filename == NULL) {
		return false;
	}

	FILE *fp = fopen(filename, "wb");
	if (fp == NULL) {
		perror("Nie można otworzyć pliku do zapisu");
		return false;
	}

	/* P4 - binary */
	fprintf(fp, "P4\n%d %d\n", width, height);

	size_t data_size = (size_t)(width / 8) * height;
	size_t written = fwrite(bitmap_data, 1, data_size, fp);

	fclose(fp);

	return written == data_size;
}

bool print_preview(const unsigned char *bitmap_data, bool ruler, int height_pels)
{
	save_bitmap_to_pbm("/tmp/output_label.pbm", bitmap_data, 96, height_pels);
	for (int y = 0; y < height_pels; y++) {
		printf("|");
		for (int x = 0; x < PRINTER_WIDTH_PELS; x++) {
			int byte_idx = y * PRINTER_WIDTH_BYTES + (x >> 3);
			int bit_idx = ~x & 7;
			if (bitmap_data[byte_idx] & (1 << bit_idx)) {
				printf("#");
			}
			else {
				printf(" ");
			}
		}

		if (ruler) {
			int current_mm = y / (PRINTER_PELS_PER_MM);
			int remainder_lines = y % (PRINTER_PELS_PER_MM);

			if (y % ((PRINTER_PELS_PER_MM)*10) == 0) {
				printf(":>%d", current_mm);
			}
			else if (remainder_lines == 0) {
				printf(": %d", current_mm);
			}
			else if (remainder_lines == ((PRINTER_PELS_PER_MM) / 2)) {
				printf(":");
			}
			else {
				printf("|");
			}
		}
		else {
			printf("|");
		}


		printf("\n");
	}
	printf("\n");

	float total_mm = (float)height_pels / (float)(PRINTER_PELS_PER_MM);

	printf("Label width %d pixels, %.2f mm\n", height_pels, total_mm);

	printf("Proceed with printing? [y/N]: ");
	fflush(stdout);

	int c = getchar();
	return (c == 'y' || c == 'Y');
}
