/*
 * Fichero D11s thermal printer implementation
 * Copyright 2026, Gerard Świderski
 */

#ifndef FICHERO_H
#define FICHERO_H

#include <stdbool.h>
#include <stddef.h>

#define FICHERO_WIDTH_PELS          96
#define FICHERO_WIDTH_BYTES         12
#define FICHERO_DEFAULT_HEIGHT_PELS 224

enum fichero_status {
	fichero_status_ok = 0,
	fichero_status_error = -1,
	fichero_status_timeout = -2,
};

enum fichero_mask {
	fichero_flag_open_cover = 2,
	fichero_flag_no_paper = 3,
	fichero_flag_battery_low = 4,
	fichero_flag_head_overheat = 5,
	fichero_flag_busy = 6,
};

enum fichero_paper_type {
	fichero_paper_gap = 0,
	fichero_paper_black_mark = 1,
	fichero_paper_continuous = 2,
};

typedef struct fichero {
	int fd;
	bool opened;
} fichero_t;

int fichero_open(fichero_t *p, const char *port_path);
void fichero_close(fichero_t *p);
int fichero_status(fichero_t *p, int *p_status);
int fichero_setup_density(fichero_t *p, int density);
int fichero_setup_paper(fichero_t *p, int paper_type);
int fichero_wakeup(fichero_t *p);
int fichero_image(fichero_t *p, const unsigned char *bitmap_data, int height_pels);
int fichero_formfeed(fichero_t *p);
int fichero_stop_wait(fichero_t *p);

#endif /* FICHERO_H */
