/*
 * Label bitmap generator, loader and preview
 * Copyright 2026 Gerard Świderski
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <dmtx.h>

#include "printer/printer.h"
#include "image.h"
#include "font_pcf.h"
#include "barcode/barcode.h"
#include "label.h"

#define LABEL_WIDTH  (PRINTER_DEFAULT_HEIGHT_PELS)
#define LABEL_HEIGHT (PRINTER_WIDTH_PELS)

#define PIXEL_WHITE 0xff
#define PIXEL_BLACK 0

#define _log_prefix          "label: "
#define _log(fmt, ...)       printf(_log_prefix fmt "\n", ##__VA_ARGS__)
#define _log_error(fmt, ...) fprintf(stderr, _log_prefix fmt "\n", ##__VA_ARGS__)

unsigned char *build_pbm_bitmap(const char *filename, int *out_height_pels)
{
	struct gplane plane;

	if (plane_load_pbm(&plane, filename) < 0) {
		return NULL;
	}

	if (plane.height != PRINTER_WIDTH_PELS) {
		_log_error("PBM height is %d pixels, printer expects exactly %d pixels", plane.height, PRINTER_WIDTH_PELS);
		plane_free(&plane);

		return NULL;
	}

	*out_height_pels = PRINTER_DEFAULT_HEIGHT_PELS;
	unsigned char *bitmap_data = plane_to_bitmap(&plane);

	plane_free(&plane);

	return bitmap_data;
}


unsigned char *build_testpattern_bitmap(int height_pels)
{
	struct gplane plane;

	if (plane_alloc(&plane, NULL, height_pels, LABEL_HEIGHT) < 0) {
		_log_error("Failed to allocate image plane");
		return NULL;
	}

	memset(plane.pixels, PIXEL_WHITE, plane.size_bytes);

	uint8_t *pixels = plane.pixels;
	for (int y = 0; y < (int)plane.height; y++) {
		for (int x = 0; x < (int)plane.width; x++) {
			bool is_border = (y == 0 || y == ((int)plane.height - 1) || x == 0 || x == ((int)plane.width - 1));
			bool is_grid = ((x & 3) == 0 || (y & 3) == 0);
			if (is_border || is_grid) {
				pixels[plane.stride_bytes * y + x] = PIXEL_BLACK;
			}
		}
	}

	unsigned char *bitmap_data = plane_to_bitmap(&plane);
	plane_free(&plane);

	return bitmap_data;
}

unsigned char *build_text_bitmap(const char *text_str, const char *font_path, int *target_height_pels)
{
	if (load_pcf(font_path, 0) < 0) {
		_log_error("Unable to load font \"%s\"", font_path);
		return NULL;
	}

	static struct gplane plane = { 0 };
	plane.width = (*target_height_pels > 0) ?
		*target_height_pels :
		0;

	render_text_format(&plane, 0, text_str);

	if (plane.width < 32) {
		plane.width = 32;
	}

	if (plane.height > PRINTER_WIDTH_PELS) {
		_log_error("Label text is (%d px) too high for %d px printer head!", plane.height, PRINTER_WIDTH_PELS);
		free_pcf(FREE_PCF_ALL);
		return NULL;
	}

	int width = plane.width;
	int height = PRINTER_WIDTH_PELS;

	if (plane_alloc(&plane, NULL, width, height) < 0) {
		free_pcf(FREE_PCF_ALL);
		_log_error("Failed to allocate bitmap memory for label");
		return NULL;
	}

	memset(plane.pixels, PIXEL_WHITE, plane.size_bytes);

	render_text_format(&plane, 0, text_str);
	free_pcf(FREE_PCF_ALL);

	*target_height_pels = width;
	unsigned char *bitmap_data = plane_to_bitmap(&plane);
	plane_free(&plane);

	return bitmap_data;
}

unsigned char *build_datamatrix_bitmap(const char *text_str, int *out_height_pels)
{
	static struct gplane plane;
	if (plane_alloc(&plane, NULL, *out_height_pels, LABEL_HEIGHT) < 0) {
		_log_error("Failed to allocate bitmap memory for label");
		return NULL;
	}

	memset(plane.pixels, PIXEL_WHITE, plane.size_bytes);

	DmtxEncode *enc = dmtxEncodeCreate();
	if (enc == NULL) {
		_log_error("Failed to create libdmtx encoder");
		plane_free(&plane);
		return NULL;
	}

	dmtxEncodeSetProp(enc, DmtxPropSizeRequest, DmtxSymbolSquareAuto);
	dmtxEncodeSetProp(enc, DmtxPropModuleSize, 2);
	dmtxEncodeSetProp(enc, DmtxPropMarginSize, 4);

	int messageLen = strlen(text_str);
	if (dmtxEncodeDataMatrix(enc, messageLen, (unsigned char *)text_str) == DmtxFail) {
		_log_error("Data Matrix encoding failed");
		dmtxEncodeDestroy(&enc);
		plane_free(&plane);
		return NULL;
	}

	int dm_width = dmtxImageGetProp(enc->image, DmtxPropWidth);
	int dm_height = dmtxImageGetProp(enc->image, DmtxPropHeight);
	int dm_bytes_per_pixel = dmtxImageGetProp(enc->image, DmtxPropBytesPerPixel);

	if (dm_height > (int)plane.height || dm_width > (int)plane.width) {
		_log_error("Generated Data Matrix exceeds label bounds");
		dmtxEncodeDestroy(&enc);
		plane_free(&plane);
		return NULL;
	}

	int offset_x = 0;
	int offset_y = (plane.height - dm_height) / 2;

	uint8_t *pixels = plane.pixels;
	for (int y = 0; y < dm_height; y++) {
		for (int x = 0; x < dm_width; x++) {
			unsigned char *dm_pixel = enc->image->pxl + (y * dm_width + x) * dm_bytes_per_pixel;
			if (*dm_pixel < 128) {
				int target_x = offset_x + x;
				int target_y = offset_y + y;
				if ((unsigned int)target_x < plane.width && (unsigned int)target_y < plane.height) {
					pixels[target_y * (int)plane.stride + target_x] = PIXEL_BLACK;
				}
			}
		}
	}

	dmtxEncodeDestroy(&enc);

	/* *out_height_pels = PRINTER_DEFAULT_HEIGHT_PELS; */
	unsigned char *bitmap_data = plane_to_bitmap(&plane);
	plane_free(&plane);

	return bitmap_data;
}

unsigned char *build_ean13_bitmap(const char *text_str, const char *font_path, int *out_height_pels)
{
	struct gplane plane;

	if (create_barcode_EAN13(&plane, text_str, font_path, *out_height_pels, LABEL_HEIGHT) < 0) {
		return NULL;
	}

	/* *out_height_pels = PRINTER_DEFAULT_HEIGHT_PELS; */
	unsigned char *bitmap_data = plane_to_bitmap(&plane);
	plane_free(&plane);

	return bitmap_data;
}

unsigned char *build_ean8_bitmap(const char *text_str, const char *font_path, int *out_height_pels)
{
	struct gplane plane;

	if (create_barcode_EAN8(&plane, text_str, font_path, *out_height_pels, LABEL_HEIGHT) < 0) {
		return NULL;
	}

	/* *out_height_pels = PRINTER_DEFAULT_HEIGHT_PELS; */
	unsigned char *bitmap_data = plane_to_bitmap(&plane);
	plane_free(&plane);

	return bitmap_data;
}

unsigned char *build_ean5_bitmap(const char *text_str, const char *font_path, int *out_height_pels)
{
	struct gplane plane;

	if (create_barcode_EAN5(&plane, text_str, font_path, *out_height_pels, LABEL_HEIGHT) < 0) {
		return NULL;
	}

	/* *out_height_pels = PRINTER_DEFAULT_HEIGHT_PELS; */
	unsigned char *bitmap_data = plane_to_bitmap(&plane);
	plane_free(&plane);

	return bitmap_data;
}

unsigned char *build_itf_bitmap(const char *text_str, int use_checksum, const char *font_path, int *out_height_pels)
{
	struct gplane plane;

	if (create_barcode_ITF(&plane, text_str, (use_checksum != 0), font_path, *out_height_pels, LABEL_HEIGHT) < 0) {
		return NULL;
	}

	/* *out_height_pels = PRINTER_DEFAULT_HEIGHT_PELS; */
	unsigned char *bitmap_data = plane_to_bitmap(&plane);
	plane_free(&plane);

	return bitmap_data;
}

unsigned char *build_code128_bitmap(const char *text_str, char mode, const char *font_path, int *out_height_pels)
{
	struct gplane plane;

	if (create_barcode_CODE128(&plane, text_str, mode, font_path, *out_height_pels, LABEL_HEIGHT) < 0) {
		return NULL;
	}

	/* *out_height_pels = PRINTER_DEFAULT_HEIGHT_PELS; */
	unsigned char *bitmap_data = plane_to_bitmap(&plane);
	plane_free(&plane);

	return bitmap_data;
}

static bool save_bitmap_to_pbm(const char *filename, const unsigned char *bitmap_data, int width, int height)
{
	if (bitmap_data == NULL || filename == NULL) {
		return false;
	}

	FILE *fp = fopen(filename, "wb");
	if (fp == NULL) {
		_log_error("Unable to open file for writting");
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
