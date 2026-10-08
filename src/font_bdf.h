/*
 * BDF font loader, text and bitmap processing
 * Copyright 2026, Gerard Świderski
 */

#ifndef FONT_BDF_H
#define FONT_BDF_H

#include <stdint.h>

typedef struct {
	uint8_t *pixels;
	unsigned int width, height;
	unsigned int stride;
	size_t stride_bytes;
	size_t size_bytes;
	size_t size_area;
} gplane_t;

#define MAX_GLYPHS  512
#define MAX_HEIGHT  64

int load_bdf(const char *filename);
void render_char(gplane_t *plane, int c, int x, int y, uint8_t color);
void render_text(gplane_t *plane, const char *text, int x, int y, uint8_t color);
int save_pbm(gplane_t *plane, const char *filename, int obrot_90_cw);

void draw_label(gplane_t *plane, const char *label_str);

#endif /* end of FONT_BDF_H */
