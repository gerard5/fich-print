/*
 * Printer communication abstraction layer
 * Copyright 2026, Gerard Świderski
 */

#ifndef FICHERO_H
#define FICHERO_H

#include <stdbool.h>
#include <stddef.h>

#define PRINTER_WIDTH_PELS          96
#define PRINTER_WIDTH_BYTES         12
#define PRINTER_DEFAULT_HEIGHT_PELS 224

enum printer_status {
	printer_status_ok = 0,
	printer_status_error = -1,
	printer_status_timeout = -2,
};

enum printer_mask {
	printer_flag_open_cover = 2,
	printer_flag_no_paper = 3,
	printer_flag_battery_low = 4,
	printer_flag_head_overheat = 5,
	printer_flag_busy = 6,
};

enum {
	printer_prop_density = 1,
	printer_prop_paper,
};

enum printer_paper_type {
	printer_paper_gap = 0,
	printer_paper_black_mark = 1,
	printer_paper_continuous = 2,
};

struct printer;

typedef struct {
	int density_level;
	int paper_type;
} printer_config_t;

struct printer_ops {
	int (*open)(struct printer *p, const char *port_path);
	int (*close)(struct printer *p);
	int (*status)(struct printer *p, int *p_status);
	int (*set_property)(struct printer *p, int key, int value);
	int (*wakeup)(struct printer *p);
	int (*send_image)(struct printer *p, const unsigned char *bitmap_data, int height_pels);
	int (*formfeed)(struct printer *p);
	int (*stop_wait)(struct printer *p);
};

typedef struct printer {
	const struct printer_ops *ops;
	bool opened;
	void *private;
} printer_t;

void printer_init(printer_t *p, const struct printer_ops *driver_ops);
bool printer_validate(printer_t *p);

int printer_open(printer_t *p, const char *port_path);
int printer_close(printer_t *p);
int printer_status(printer_t *p, int *p_status);
int printer_set_property(printer_t *p, int prop, int value);
int printer_wakeup(printer_t *p);
int printer_send_image(printer_t *p, const unsigned char *bitmap_data, int height_pels);
int printer_formfeed(printer_t *p);
int printer_stop_wait(printer_t *p);

int printer_print_label(printer_t *p, const printer_config_t *config, const unsigned char *bitmap_data, int height_pels);

extern const struct printer_ops printer_ops_aiyin;
extern const struct printer_ops printer_ops_niimbot;

#endif /* FICHERO_H */
