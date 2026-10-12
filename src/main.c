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
#include <unistd.h>
#include <getopt.h>

#include "printer.h"
#include "label.h"

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
	{ "image", required_argument, NULL, 'i' },
	{ "testpattern", no_argument, NULL, 'T' },
	{ "density", required_argument, NULL, 'D' },
	{ "paper", required_argument, NULL, 'p' },
	{ "preview", no_argument, NULL, 's' },
	{ "help", no_argument, NULL, 'h' },
	{ NULL, 0, NULL, 0 }
};

int main(int argc, char **argv)
{
	const char *port_path = "/dev/rfcomm0";
	const char *font_path = "default.pcf";
	const char *pbm_filename = NULL;
	const char *text_str = NULL;
	const char *dmtx_str = NULL;

	printer_config_t printer_cfg = { .density_level = 1, .paper_type = printer_paper_gap };
	int printer_model = 0;
	int preview = 0;

	if (argc < 2) {
		print_usage(argv[0]);
		return EXIT_SUCCESS;
	}

	int opt;
	int option_index = 0;
	int mutex_count = 0;

	while ((opt = getopt_long(argc, argv, "d:m:i:D:p:t:f:b:Tsh", long_options, &option_index)) != -1) {
		switch (opt) {
			case 'd':
				port_path = optarg;
				break;
			case 'm':
				printer_model = atoi(optarg);
				break;
			case 'f':
				font_path = optarg;
				break;

			case 't':
				text_str = optarg;
				mutex_count++; /* mutually exclusive */
				break;
			case 'b':
				dmtx_str = optarg;
				mutex_count++; /* mutually exclusive */
				break;
			case 'i':
				pbm_filename = optarg;
				mutex_count++; /* mutually exclusive */
				break;
			case 'T':
				mutex_count++; /* mutually exclusive */
				break;

			case 'D':
				printer_cfg.density_level = atoi(optarg);
				break;
			case 'p':
				printer_cfg.paper_type = atoi(optarg);
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

	if (mutex_count != 1) {
		fprintf(stderr, "Error: Parameters -t, -b and -i are mutually exclusive.\n");
		print_usage(argv[0]);
		return EXIT_FAILURE;
	}

	printer_t p = { 0 };
	int ret = EXIT_FAILURE;

	int height_pels = PRINTER_DEFAULT_HEIGHT_PELS;
	unsigned char *bitmap_data = NULL;

	switch (printer_model) {
		case 1: printer_init(&p, &printer_ops_aiyin); break;
		case 2: printer_init(&p, &printer_ops_niimbot); break;
		default: break;
	}

	if (!printer_validate(&p)) {
		fprintf(stderr, "No valid printer model was selected\n");
		return EXIT_FAILURE;
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
