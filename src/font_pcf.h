/*
 * PCF font loader, text and bitmap processing
 * Copyright 2026, Gerard Świderski
 */

#ifndef FONT_PCF_H
#define FONT_PCF_H

#include <stdint.h>

#define FREE_PCF_ALL (-1)

typedef struct {
	uint8_t *pixels;
	unsigned int width, height;
	unsigned int stride;
	size_t stride_bytes;
	size_t size_bytes;
	size_t size_area;
} gplane_t;

int load_pcf(const char *filename, int slot);
int free_pcf(int slot);

int render_char(gplane_t *plane, int typeface_idx, int c, int x, int y, uint8_t color);
int get_pcf_char_step(int typeface_idx, int codepoint, int default_w);
void render_text(gplane_t *plane, int typeface_idx, const char *text, int x, int y, uint8_t color);
void render_text_format(gplane_t *plane, int typeface_idx, const char *format_str);

#endif /* end of FONT_BDF_H */
