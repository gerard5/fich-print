/*
 * Fichero D11s thermal printer utility
 * Copyright 2026 Gerard Świderski
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
#include <strings.h>
#include <unistd.h>
#include <getopt.h>

#include "printer/printer.h"
#include "helpers.h"
#include "label.h"
#include "barcode/barcode.h"

static void print_usage(const char *prog_name)
{
	printf("USAGE\n");
	printf("  %s [options]\n", prog_name);
	printf("\nOPTIONS\n");
	printf("  -d, --device <path>      RFCOMM path (default: /dev/rfcomm0) or usb\n");
	printf("  -m, --model <model>      Select printer model (1 - Fichero/AiYin D11s, 2 - Niimbot D11,D110)\n");
	printf("  -f, --font <path>        Path to PCF font\n");
	printf("  -t, --text <text>        Print formated text label (\\a-center \\f-left \\r-right \\b-bold toggle \\n-newline)\n");
	printf("  -b, --datamatrix <text>  Print Data Matrix encoding text value\n");
	printf("  -c, --code128 <text>     Print Code 128 barcode\n");
	printf("  -3, --ean13 <text>       Print EAN-13 barcode\n");
	printf("  -8, --ean8 <text>        Print EAN-8 barcode\n");
	printf("  -5, --ean5 <text>        Print EAN-5 barcode\n");
	printf("  -I, --itf <text>         Print ITF barcode\n");
	printf("  -i, --image <file>       Print image loaded from PBM (P1) file\n");
	printf("  -T, --testpattern        Print Test Pattern\n");
	printf("  -D, --density <level>    Set density level 0-2 (default: 1)\n");
	printf("  -p, --paper <type>       Paper type: 0=gap, 1=black_mark, 2=continuous (default: 0)\n");
	printf("  -s, --preview            Preview before print\n");
	printf("  -h, --help               Show this help message\n");
	printf("\nEXAMPLE\n");
	printf("  sudo rfcomm connect 0 12:0F:F1:CE:C0:DE 1\n");
	printf("  %s -m1 -d /dev/rfcomm0 -D2 -s -t 'hello'\n", prog_name);
	printf("\n");
}

static const struct option long_options[] = {
	{ "device", required_argument, NULL, 'd' },
	{ "model", required_argument, NULL, 'm' },
	{ "font", required_argument, NULL, 'f' },
	{ "text", required_argument, NULL, 't' },
	{ "datamatrix", required_argument, NULL, 'b' },
	{ "code128", required_argument, NULL, 'c' },
	{ "ean13", required_argument, NULL, '3' },
	{ "ean8", required_argument, NULL, '8' },
	{ "ean5", required_argument, NULL, '5' },
	{ "image", required_argument, NULL, 'i' },
	{ "testpattern", no_argument, NULL, 'T' },
	{ "density", required_argument, NULL, 'D' },
	{ "paper", required_argument, NULL, 'p' },
	{ "preview", no_argument, NULL, 's' },
	{ "help", no_argument, NULL, 'h' },
	{ NULL, 0, NULL, 0 }
};

enum {
	lopt_text,
	lopt_image_pbm,
	lopt_testpattern,
	lopt_barcode_datamatrix,
	lopt_barcode_128,
	lopt_barcode_itf,
	lopt_barcode_ean13,
	lopt_barcode_ean8,
	lopt_barcode_ean5,
};

#define LOPT_SET(op) \
	do { \
		lopt_typ = op; \
		lopt_arg = optarg; \
		lopt_count++; \
	} while (0)

int get_printer_model(const char *optarg)
{
	if (strcasecmp(optarg, "fichero") == 0 ||
		strcasecmp(optarg, "aiyin") == 0) {
		return 1;
	}
	else if (strcasecmp(optarg, "niimbot") == 0) {
		return 2;
	}

	return atoi(optarg);
}

int main(int argc, char **argv)
{
	printer_t p = { 0 };
	printer_config_t printer_cfg = { .density_level = 1, .paper_type = printer_paper_gap };

	const char *port_path = "/dev/rfcomm0";
	const char *font_path = "default.pcf";
	const char *lopt_arg = NULL;
	int lopt_count = 0;
	int lopt_typ = -1;

	unsigned char *bitmap_data = NULL;
	int printer_model = 0;
	int height_pels = PRINTER_DEFAULT_HEIGHT_PELS;
	bool is_preview = false;
	int ret = EXIT_FAILURE;

	int opt;
	int option_index = 0;

	if (argc < 2) {
		print_usage(argv[0]);
		return EXIT_SUCCESS;
	}

	while ((opt = getopt_long(argc, argv, "d:m:i:I:D:p:t:f:b:c:3:5:8:Tsh", long_options, &option_index)) != -1) {
		switch (opt) {
			case 'd': port_path = optarg; break;
			case 'm': printer_model = get_printer_model(optarg); break;
			case 'f': font_path = optarg; break;
			case 't': LOPT_SET(lopt_text); break;
			case 'b': LOPT_SET(lopt_barcode_datamatrix); break;
			case 'c': LOPT_SET(lopt_barcode_128); break;
			case 'I': LOPT_SET(lopt_barcode_itf); break;
			case '3': LOPT_SET(lopt_barcode_ean13); break;
			case '8': LOPT_SET(lopt_barcode_ean8); break;
			case '5': LOPT_SET(lopt_barcode_ean5); break;
			case 'i': LOPT_SET(lopt_image_pbm); break;
			case 'T': LOPT_SET(lopt_testpattern); break;
			case 'D': printer_cfg.density_level = atoi(optarg); break;
			case 'p': printer_cfg.paper_type = atoi(optarg); break;
			case 's': is_preview = true; break;
			case 'h': print_usage(argv[0]); return EXIT_SUCCESS;
			case '?':
			default: print_usage(argv[0]); return EXIT_FAILURE;
		}
	}

	switch (printer_model) {
		case 1: printer_init(&p, &printer_ops_aiyin); break;
		case 2: printer_init(&p, &printer_ops_niimbot); break;
		default: break;
	}

	if (!printer_validate(&p)) {
		fprintf(stderr, "No valid printer model was selected\n");
		return EXIT_FAILURE;
	}

	if (lopt_count != 1) {
		fprintf(stderr, "Error: Print content options t,b,c,3,5,8,i,T are mutually exclusive.\n");
		print_usage(argv[0]);
		return EXIT_FAILURE;
	}

	char *lopt_arg_raw = NULL;
	if (lopt_arg != NULL) {
		lopt_arg_raw = unescape_string_dup(lopt_arg);
		if (lopt_arg_raw == NULL) {
			fprintf(stderr, "Error: unable to allocate string\n");
			return EXIT_FAILURE;
		}
	}

	switch (lopt_typ) {
		case lopt_text:
			bitmap_data = build_text_bitmap(lopt_arg_raw, font_path, &height_pels);
			break;
		case lopt_image_pbm:
			bitmap_data = build_pbm_bitmap(lopt_arg, &height_pels);
			break;
		case lopt_testpattern:
			bitmap_data = build_testpattern_bitmap(height_pels);
			break;
		case lopt_barcode_datamatrix:
			bitmap_data = build_datamatrix_bitmap(lopt_arg_raw, &height_pels);
			break;
		case lopt_barcode_128:
			bitmap_data = build_code128_bitmap(lopt_arg_raw, code128_mode_auto, font_path, &height_pels);
			break;
		case lopt_barcode_itf:
			bitmap_data = build_itf_bitmap(lopt_arg, 0, font_path, &height_pels);
			break;
		case lopt_barcode_ean13:
			bitmap_data = build_ean13_bitmap(lopt_arg, font_path, &height_pels);
			break;
		case lopt_barcode_ean8:
			bitmap_data = build_ean8_bitmap(lopt_arg, font_path, &height_pels);
			break;
		case lopt_barcode_ean5:
			bitmap_data = build_ean5_bitmap(lopt_arg, font_path, &height_pels);
			break;
		default:
			break;
	}

	free(lopt_arg_raw);
	lopt_arg_raw = NULL;

	if (bitmap_data == NULL) {
		fprintf(stderr, "Unable to create bitmap\n");
		return EXIT_FAILURE;
	}

	do {
		if (is_preview && !print_preview(bitmap_data, true, height_pels)) {
			break;
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
