/*
 * PCF font loader, text and bitmap processing
 * Copyright 2026 Gerard Świderski
 */

#ifndef FONT_PCF_H
#define FONT_PCF_H

#include <stdint.h>

struct gplane;

#define FREE_PCF_ALL (-1)

int load_pcf(const char *filename, int slot);
int free_pcf(int slot);

int get_pcf_char_step(int slot, int codepoint, int default_w);

int render_char(struct gplane *plane, int slot, int c, int x, int y, uint8_t color);
void render_text(struct gplane *plane, int slot, const char *text, int x, int y, uint8_t color);
void render_text_format(struct gplane *plane, int slot, const char *format_str);

#endif /* end of FONT_BDF_H */
