/*
 * Printer communication abstraction layer
 * Copyright 2026, Gerard Świderski
 */

#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

#include "printer.h"

#define PRINTER_CHECK_OPEN(p, is_open) \
	((p) != NULL && (p)->ops != NULL && (p)->opened == (is_open))
#define PRINTER_CHECK(p) PRINTER_CHECK_OPEN(p, true)

#define PRINTER_CALL_OPS(p, op, ...) \
	(((p)->ops->op != NULL) ? (p)->ops->op((p), ##__VA_ARGS__) : printer_status_error)

void printer_init(printer_t *p, const struct printer_ops *driver_ops)
{
	assert(p != NULL);
	assert(driver_ops != NULL);

	p->ops = driver_ops;
	p->opened = false;
	p->private = NULL;
}

bool printer_validate(printer_t *p)
{
	return p != NULL && p->ops != NULL;
}

int printer_open(printer_t *p, const char *port_path)
{
	int res = printer_status_error;
	if (PRINTER_CHECK_OPEN(p, false)) {
		res = PRINTER_CALL_OPS(p, open, port_path);
		if (res == printer_status_ok) {
			p->opened = true;
		}
	}
	return res;
}

int printer_close(printer_t *p)
{
	int res = printer_status_error;
	if (PRINTER_CHECK_OPEN(p, true)) {
		res = PRINTER_CALL_OPS(p, close);
		if (res == printer_status_ok) {
			p->opened = false;
		}
	}

	return res;
}

int printer_status(printer_t *p, int *p_status)
{
	return (PRINTER_CHECK(p)) ?
		PRINTER_CALL_OPS(p, status, p_status) :
		printer_status_error;
}

int printer_set_property(printer_t *p, int key, int value)
{
	return (PRINTER_CHECK(p)) ?
		PRINTER_CALL_OPS(p, set_property, key, value) :
		printer_status_error;
}

int printer_wakeup(printer_t *p)
{
	return (PRINTER_CHECK(p)) ?
		PRINTER_CALL_OPS(p, wakeup) :
		printer_status_error;
}

int printer_send_image(printer_t *p, const unsigned char *bitmap_data, int height_pels)
{
	return (PRINTER_CHECK(p)) ?
		PRINTER_CALL_OPS(p, send_image, bitmap_data, height_pels) :
		printer_status_error;
}

int printer_formfeed(printer_t *p)
{
	return (PRINTER_CHECK(p)) ?
		PRINTER_CALL_OPS(p, formfeed) :
		printer_status_error;
}

int printer_stop_wait(printer_t *p)
{
	return (PRINTER_CHECK(p)) ?
		PRINTER_CALL_OPS(p, stop_wait) :
		printer_status_error;
}

int printer_print_label(printer_t *p, const printer_config_t *config, const unsigned char *bitmap_data, int height_pels)
{
	if (!PRINTER_CHECK(p) || bitmap_data == NULL) {
		return printer_status_error;
	}

	if (PRINTER_CALL_OPS(p, status, NULL) != printer_status_ok) {
		return printer_status_error;
	}

	if (config != NULL) {
		if (config->density_level != -1) {
			if (PRINTER_CALL_OPS(p, set_property, printer_prop_density, config->density_level) != printer_status_ok) {
				return printer_status_error;
			}
		}
		if (config->paper_type != -1) {
			if (PRINTER_CALL_OPS(p, set_property, printer_prop_paper, config->paper_type) != printer_status_ok) {
				return printer_status_error;
			}
		}
	}

	if (PRINTER_CALL_OPS(p, wakeup) != printer_status_ok) {
		return printer_status_error;
	}

	if (PRINTER_CALL_OPS(p, send_image, bitmap_data, height_pels) != printer_status_ok) {
		return printer_status_error;
	}

	if (PRINTER_CALL_OPS(p, formfeed) != printer_status_ok) {
		return printer_status_error;
	}

	if (PRINTER_CALL_OPS(p, stop_wait) != printer_status_ok) {
		return printer_status_error;
	}

	return printer_status_ok;
}
