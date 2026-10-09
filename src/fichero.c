/*
 * Fichero D11s thermal printer implementation
 * Copyright 2026, Gerard Świderski
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/select.h>
#include "fichero.h"

void printer_init(fichero_t *p, const printer_ops_t *driver_ops)
{
	p->ops = driver_ops;
	p->opened = false;
	p->fd = -1;
}

int printer_open(fichero_t *p, const char *port_path)
{
	if (p == NULL || p->ops == NULL || p->ops->open == NULL) {
		return fichero_status_error;
	}

	return p->ops->open(p, port_path);
}

void printer_close(fichero_t *p)
{
	if (p == NULL || p->ops == NULL || p->ops->close == NULL) {
		return;
	}

	p->ops->close(p);
}

int printer_status(fichero_t *p, int *p_status)
{
	if (p == NULL || p->opened == false || p->ops == NULL || p->ops->status == NULL) {
		return fichero_status_error;
	}

	return p->ops->status(p, p_status);
}

int printer_setup_density(fichero_t *p, int density)
{
	if (p == NULL || p->opened == false || p->ops == NULL || p->ops->setup_density == NULL) {
		return fichero_status_error;
	}

	return p->ops->setup_density(p, density);
}

int printer_setup_paper(fichero_t *p, int paper_type)
{
	if (p == NULL || p->opened == false || p->ops == NULL || p->ops->setup_paper == NULL) {
		return fichero_status_error;
	}

	return p->ops->setup_paper(p, paper_type);
}

int printer_wakeup(fichero_t *p)
{
	if (p == NULL || p->opened == false || p->ops == NULL || p->ops->wakeup == NULL) {
		return fichero_status_error;
	}

	return p->ops->wakeup(p);
}

int printer_image(fichero_t *p, const unsigned char *bitmap_data, int height_pels)
{
	if (p == NULL || p->opened == false || p->ops == NULL || p->ops->image == NULL) {
		return fichero_status_error;
	}

	return p->ops->image(p, bitmap_data, height_pels);
}

int printer_formfeed(fichero_t *p)
{
	if (p == NULL || p->opened == false || p->ops == NULL || p->ops->formfeed == NULL) {
		return fichero_status_error;
	}

	return p->ops->formfeed(p);
}

int printer_stop_wait(fichero_t *p)
{
	if (p == NULL || p->opened == false || p->ops == NULL || p->ops->stop_wait == NULL) {
		return fichero_status_error;
	}

	return p->ops->stop_wait(p);
}
