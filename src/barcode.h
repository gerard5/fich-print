/*
 * BARCODE EAN5, EAN13, ITF, CODE128 (A,B,C) generator
 * Copyright 2026 Gerard Świderski
 */

#ifndef BARCODE_H
#define BARCODE_H

unsigned char *build_datamatrix_bitmap(const char *text_str, int *out_height_pels);
unsigned char *build_code128_bitmap(const char *text_str, char mode, const char *font_path, int *out_height_pels);
unsigned char *build_ean13_bitmap(const char *text_str, const char *font_path, int *out_height_pels);
unsigned char *build_ean5_bitmap(const char *text_str, const char *font_path, int *out_height_pels);
unsigned char *build_itf_bitmap(const char *text_str, int use_checksum, const char *font_path, int *out_height_pels);

#endif /* end of BARCODE_H */
