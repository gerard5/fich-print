/*
 * Image plane management
 * Copyright 2026 Gerard Świderski
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "image.h"

#define _log_prefix          "plane: "
#define _log(fmt, ...)       printf(_log_prefix fmt "\n", ##__VA_ARGS__)
#define _log_error(fmt, ...) fprintf(stderr, _log_prefix fmt "\n", ##__VA_ARGS__)

int plane_alloc(struct gplane *plane, void *mem, int width, int height)
{
	void *p = (mem != NULL) ? mem : calloc(1, width * height);
	if (p == NULL) {
		return -1;
	}

	plane->pixels = (uint8_t *)p;
	plane->width = width;
	plane->height = height;
	plane->stride = width;
	plane->stride_bytes = width * sizeof(uint8_t);
	plane->size_bytes = width * height * sizeof(uint8_t);
	plane->size_area = width * height;

	return 0;
}

void plane_free(struct gplane *plane)
{
	free(plane->pixels);
	memset(plane, 0, sizeof(*plane));
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

int plane_load_pbm(struct gplane *plane, const char *filename)
{
	FILE *f = fopen(filename, "r");
	if (f == NULL) {
		_log_error("Failed to open PBM file");
		return -1;
	}

	char magic[3];
	if (fscanf(f, "%2s", magic) != 1 || magic[0] != 'P' || magic[1] != '1') {
		_log_error("Invalid or unsupported PBM format (expected P1)");
		fclose(f);
		return -1;
	}

	pbm_skip_comments(f);
	int width = 0, height = 0;
	if (fscanf(f, "%d %d", &width, &height) != 2) {
		_log_error("Failed to read PBM dimensions");
		fclose(f);
		return -1;
	}

	unsigned char *buf = (unsigned char *)calloc(1, width * height);
	if (buf == NULL) {
		_log_error("Failed to allocate bitmap memory for PBM");
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
			buf[pixels_read] = (ch == '1') ? 0 : 0xff;
			pixels_read++;
		}
		else {
			_log_error("Unexpected character '%c' in PBM data at pixel %d", ch, pixels_read);
			free(buf);
			fclose(f);
			return -1;
		}
	}

	if (pixels_read < total_pixels) {
		_log_error("Unexpected end of PBM data. Read %d of %d pixels", pixels_read, total_pixels);
		free(buf);
		fclose(f);
		return -1;
	}

	fclose(f);

	return plane_alloc(plane, buf, width, height);
}

int plane_save_pbm(struct gplane *plane, const char *filename)
{
	int w = plane->width;
	int h = plane->height;

	uint8_t *pixels = (uint8_t *)plane->pixels;

	FILE *f = fopen(filename, "w");
	if (!f) {
		_log_error("Unable to open file for writting: %s", filename);
		free(pixels);
		return -1;
	}

	fprintf(f, "P1\n");
	fprintf(f, "#\n");

	fprintf(f, "%d %d\n", w, h);

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

	fclose(f);

	return 0;
}

unsigned char *plane_to_bitmap(struct gplane *plane)
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
