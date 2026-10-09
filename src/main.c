/*
 * Fichero D11s thermal printer utility
 * Copyright 2026, Gerard Świderski
 *
 * example usage:
 *   # open rfcomm0 in auxiliary terminal window
 *   sudo rfcomm connect 0 12:0F:F1:CE:C0:DE 1
 *   # then run in main terminal window
 *   ./program -f RobotoThinCondensed-18.pcf -d /dev/rfcomm0 -D 2 -s -t 'Hello'
 *
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <ctype.h>
#include <libgen.h>

#include <dmtx.h>

#include "printer.h"
#include "font_pcf.h"

/* LABEL_WIDTH:HEIGHT is 90deg CCW against FICHERO_WIDTH:HEIGHT */
#define LABEL_WIDTH  (FICHERO_DEFAULT_HEIGHT_PELS)
#define LABEL_HEIGHT (FICHERO_WIDTH_PELS)

void unescape_string(char *str)
{
	char *src = str;
	char *dst = str;

	while (*src) {
		if (*src == '\\' && *(src + 1) != '\0') {
			src++; /* Skip backslash */

			/* 1. Octal escape sequence handling */
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
			/* 2. Hexadecimal escape sequence handling */
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
			/* 3. Standard escape sequences */
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

/* Helper function to skip PBM comments and whitespace */
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

/* Load PBM (P1 text format) bitmap */
static unsigned char *load_pbm_file(const char *filename, int *out_height)
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

	if (width != FICHERO_WIDTH_BYTES * 8) {
		fprintf(stderr, "Warning: PBM width is %d pixels, printer expects exactly %d pixels\n", width, FICHERO_WIDTH_BYTES * 8);
	}

	size_t bitmap_size = FICHERO_WIDTH_BYTES * height;
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
				int byte_idx = y * FICHERO_WIDTH_BYTES + (x >> 3);
				int bit_idx = ~x & 7;
				bitmap_data[byte_idx] |= (1 << bit_idx);
			}
		}
	}

	fclose(f);
	*out_height = height;
	return bitmap_data;
}

/* Generate test pattern: grid every 4 pixels + border */
static unsigned char *generate_test_pattern(int height_pels)
{
	size_t bitmap_size = FICHERO_WIDTH_BYTES * height_pels;
	unsigned char *bitmap_data = (unsigned char *)calloc(1, bitmap_size);
	if (bitmap_data == NULL) {
		perror("Failed to allocate test pattern memory");
		return NULL;
	}

	int width_pels = FICHERO_WIDTH_BYTES * 8;
	for (int y = 0; y < height_pels; y++) {
		for (int x = 0; x < width_pels; x++) {
			bool is_border = (y == 0 || y == height_pels - 1 || x == 0 || x == width_pels - 1);
			bool is_grid = ((x & 3) == 0 || (y & 3) == 0);
			if (is_border || is_grid) {
				int byte_idx = y * FICHERO_WIDTH_BYTES + (x >> 3);
				int bit_idx = ~x & 7;
				bitmap_data[byte_idx] |= (1 << bit_idx);
			}
		}
	}

	return bitmap_data;
}

/* Build bitmap from text using PCF font */
static unsigned char *build_text_bitmap(const char *text_str, const char *font_path, int *out_height_pels)
{
	if (!load_pcf(font_path, 0)) {
		fprintf(stderr, "Unable to load font \"%s\"\n", font_path);
		return NULL;
	}

	static gplane_t label_plane;
	uint8_t *label_buf = malloc(LABEL_WIDTH * LABEL_HEIGHT * sizeof(uint8_t));
	if (label_buf == NULL) {
		free_pcf(-1);
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
		perror("Failed to duplicate text string");
		free(label_buf);
		free_pcf(-1);
		return NULL;
	}

	unescape_string(str);
	render_text_format(&label_plane, 0, str);
	free(str);

	free_pcf(-1);

	int height = LABEL_WIDTH;
	int width = LABEL_HEIGHT;
	*out_height_pels = LABEL_WIDTH;

	size_t bitmap_size = FICHERO_WIDTH_BYTES * height;
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
				int byte_idx = y * FICHERO_WIDTH_BYTES + (x >> 3);
				int bit_idx = ~x & 7;
				bitmap_data[byte_idx] |= (1 << bit_idx);
			}
		}
	}

	free(label_buf);
	return bitmap_data;
}

/* Build DataMatrix 2D code bitmap */
static unsigned char *build_datamatrix_bitmap(const char *text_str, int *out_height_pels)
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
	if (dmtxEncodeDataMatrix(enc, messageLen, (unsigned char *)text_str) ==
		DmtxFail) {
		fprintf(stderr,
			"Data Matrix encoding failed (text might be too long for "
			"specified module size)\n");
		dmtxEncodeDestroy(&enc);
		free(label_buf);
		return NULL;
	}

	int dm_width = dmtxImageGetProp(enc->image, DmtxPropWidth);
	int dm_height = dmtxImageGetProp(enc->image, DmtxPropHeight);
	int dm_bytes_per_pixel =
		dmtxImageGetProp(enc->image, DmtxPropBytesPerPixel);

	if (dm_height > LABEL_HEIGHT || dm_width > LABEL_WIDTH) {
		fprintf(stderr,
			"Error: Generated Data Matrix (%dx%d) exceeds label bounds "
			"(%dx%d)\n",
			dm_width, dm_height, LABEL_WIDTH, LABEL_HEIGHT);
		dmtxEncodeDestroy(&enc);
		free(label_buf);
		return NULL;
	}

	int offset_x = 0; /* TODO: use switch to center/align: (LABEL_WIDTH - dm_width) / 2; */
	int offset_y = (LABEL_HEIGHT - dm_height) / 2;

	for (int y = 0; y < dm_height; y++) {
		for (int x = 0; x < dm_width; x++) {
			unsigned char *dm_pixel =
				enc->image->pxl +
				((dm_height - 1 - y) * dm_width + x) * dm_bytes_per_pixel;

			if (*dm_pixel < 128) {
				int target_x = offset_x + x;
				int target_y = offset_y + y;
				label_buf[target_y * LABEL_WIDTH + target_x] = 0;
			}
		}
	}

	dmtxEncodeDestroy(&enc);

	int height = LABEL_WIDTH;
	int width = LABEL_HEIGHT;
	*out_height_pels = LABEL_WIDTH;

	size_t bitmap_size = FICHERO_WIDTH_BYTES * height;
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
				int byte_idx = y * FICHERO_WIDTH_BYTES + (x >> 3);
				int bit_idx = ~x & 7;
				bitmap_data[byte_idx] |= (1 << bit_idx);
			}
		}
	}

	free(label_buf);
	return bitmap_data;
}

/* Display terminal ASCII preview and wait for confirmation */
static bool print_preview(const unsigned char *bitmap_data, int height_pels)
{
	for (int y = 0; y < height_pels; y++) {
		printf("|");
		for (int x = 0; x < 96; x++) {
			int byte_idx = y * FICHERO_WIDTH_BYTES + (x >> 3);
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

/* Resolve default font path relative to executable location */
static void get_default_font_path(char *buf, size_t bufsiz, const char *argv0)
{
	(void)argv0;

	char exe_path[1024];
	ssize_t len = readlink("/proc/self/exe", exe_path, sizeof(exe_path) - 1);
	if (len != -1) {
		exe_path[len] = '\0';
		char *dir = dirname(exe_path);
		snprintf(buf, bufsiz, "%s/fonts/fichero.pcf", dir);
		if (access(buf, R_OK) == 0) {
			return;
		}
	}

	/* Fallback to relative path */
	snprintf(buf, bufsiz, "fonts/fichero.pcf");
}

static void print_usage(const char *prog_name)
{
	printf("USAGE\n");
	printf("  %s [options]\n", prog_name);
	printf("\nOPTIONS\n");
	printf("  -d <dev/path> RFCOMM device path (default: /dev/rfcomm0)\n");
	printf("  -m <model>    Select printer model (1 - fichero D11s, 2 - niimbot D11,D110)\n");
	printf("  -i <file>     Load input from PBM (P1) file\n");
	printf("  -D <density>  Set density level 0-2 (default: 1)\n");
	printf("  -p <type>     Paper type: 0=gap, 1=black_mark, 2=continuous (default: 0)\n");
	printf("  -t <text>     Label text (format \\a-center \\f-left \\r-right \\b-bold toggle)\n");
	printf("  -f <font>     path to bdf font\n");
	printf("  -b <text>     Generate datamatrix code\n");
	printf("  -s            Preview before print\n");
	printf("  -h            Show this help message\n");
	printf("\nEXAMPLE\n");
	printf("  sudo rfcomm connect 0 12:0F:F1:CE:C0:DE 1\n");
	printf("  %s -m1 -d /dev/rfcomm0 -D2 -s -t 'hello'\n", prog_name);
	printf("\n");
}

int main(int argc, char **argv)
{

	const char *port_path = "/dev/rfcomm0";
	const char *pbm_filename = NULL;
	const char *text_str = NULL;
	const char *dmtx_str = NULL;
	char font_path_buf[1024];
	const char *font_path = NULL;
	printer_config_t printer_cfg = { .density_level = 1, .paper_type = printer_paper_gap };
	int printer_model = 0;
	int preview = 0;

	if (argc < 2) {
		print_usage(argv[0]);
		return EXIT_SUCCESS;
	}

	int opt;
	while ((opt = getopt(argc, argv, "d:m:i:D:p:t:f:b:sh")) != -1) {
		switch (opt) {
			case 'd':
				port_path = optarg;
				break;
			case 'm':
				printer_model = atoi(optarg);
				break;
			case 'i':
				pbm_filename = optarg;
				break;
			case 'D':
				printer_cfg.density_level = atoi(optarg);
				break;
			case 'p':
				printer_cfg.paper_type = atoi(optarg);
				break;
			case 't':
				text_str = optarg;
				break;
			case 'f':
				font_path = optarg;
				break;
			case 'b':
				dmtx_str = optarg;
				break;
			case 's':
				preview = 1;
				break;

			case 'h':
				print_usage(argv[0]);
				return EXIT_SUCCESS;

			case '?':
			default:
				print_usage(argv[0]);
				return EXIT_FAILURE;
		}
	}

	printer_t p = { 0 };
	int ret = EXIT_FAILURE;

	int height_pels = FICHERO_DEFAULT_HEIGHT_PELS;
	unsigned char *bitmap_data = NULL;

	switch (printer_model) {
		case 1: printer_init(&p, &printer_ops_fichero); break;
		case 2: printer_init(&p, &printer_ops_niimbot); break;
		default: break;
	}

	if (!printer_validate(&p)) {
		fprintf(stderr, "No valid printer model was selected\n");
		return EXIT_FAILURE;
	}

	if (font_path == NULL) {
		get_default_font_path(font_path_buf, sizeof(font_path_buf), argv[0]);
		font_path = font_path_buf;
	}

	if (pbm_filename != NULL) {
		bitmap_data = load_pbm_file(pbm_filename, &height_pels);
		if (bitmap_data == NULL) {
			return EXIT_FAILURE;
		}
	}
	else if (text_str != NULL) {
		bitmap_data = build_text_bitmap(text_str, font_path, &height_pels);
		if (bitmap_data == NULL) {
			return EXIT_FAILURE;
		}
	}
	else if (dmtx_str != NULL) {
		bitmap_data = build_datamatrix_bitmap(dmtx_str, &height_pels);
		if (bitmap_data == NULL) {
			return EXIT_FAILURE;
		}
	}
	else {
		bitmap_data = generate_test_pattern(height_pels);
		if (bitmap_data == NULL) {
			return EXIT_FAILURE;
		}
	}

	do {
		if (preview != 0) {
			if (!print_preview(bitmap_data, height_pels)) {
				break;
			}
		}

		if (printer_open(&p, port_path) != printer_status_ok) {
			break;
		}

		if (printer_print_label(&p, &printer_cfg, bitmap_data, height_pels) != printer_status_ok) {
			break;
		}

		ret = EXIT_SUCCESS;
	} while (0);

	free(bitmap_data);
	printer_close(&p);

	return ret;
}
