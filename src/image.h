/*
 * Image plane management
 * Copyright 2026 Gerard Świderski
 */

#ifndef IMAGE_H
#define IMAGE_H

#include <stddef.h>
#include <stdint.h>

struct gplane {
	uint8_t *pixels;
	unsigned int width, height;
	unsigned int stride;
	size_t stride_bytes;
	size_t size_bytes;
	size_t size_area;
};

int plane_alloc(struct gplane *plane, void *mem, int width, int height);
void plane_free(struct gplane *plane);
int plane_load_pbm(struct gplane *plane, const char *filename);
int plane_save_pbm(struct gplane *plane, const char *filename);
unsigned char *plane_to_bitmap(struct gplane *plane);

#endif /* end of IMAGE_H */
