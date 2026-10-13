/*
 * Label bitmap generator, loader and preview
 * Copyright 2026 Gerard Świderski
 */

#ifndef LABEL_H
#define LABEL_H

#include <stdbool.h>

unsigned char *load_pbm_file(const char *filename, int *out_height);
unsigned char *generate_test_pattern(int height_pels);
unsigned char *build_text_bitmap(const char *text_str, const char *font_path, int *out_height_pels);
unsigned char *build_datamatrix_bitmap(const char *text_str, int *out_height_pels);
unsigned char *build_code128_bitmap(const char *text_str, char mode, const char *font_path, int *out_height_pels);
unsigned char *build_ean13_bitmap(const char *text_str, const char *font_path, int *out_height_pels);
unsigned char *build_ean5_bitmap(const char *text_str, const char *font_path, int *out_height_pels);
bool print_preview(const unsigned char *bitmap_data, bool ruler, int height_pels);

#endif /* LABEL_H */
