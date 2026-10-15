/*
 * BARCODE EAN5, EAN13, ITF, CODE128 (A,B,C) generator
 * Copyright 2026 Gerard Świderski
 */

#ifndef BARCODE_H
#define BARCODE_H

#include <stdbool.h>

struct gplane;

int create_barcode_EAN13(struct gplane *plane, const char *text_str, const char *font_path, int width, int height);
int create_barcode_EAN5(struct gplane *plane, const char *text_str, const char *font_path, int width, int height);
int create_barcode_ITF(struct gplane *plane, const char *text_str, bool use_checksum, const char *font_path, int width, int height);
int create_barcode_CODE128(struct gplane *plane, const char *text_str, char mode, const char *font_path, int width, int height);

#endif /* end of BARCODE_H */
