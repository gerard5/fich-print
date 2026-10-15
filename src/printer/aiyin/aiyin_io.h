/*
 * fichero/aiyin IO operations API
 * Copyright 2026 Gerard Świderski
 */

#ifndef AIYIN_IO_H
#define AIYIN_IO_H

#include <sys/types.h> /* ssize_t, size_t */
#include <sys/time.h>  /* struct timeval */

struct aiyin_io_ops {
	int (*open)(void **io_ctx, const char *port_path);
	void (*close)(void *io_ctx);
	ssize_t (*write)(void *io_ctx, const void *buf, size_t count);
	ssize_t (*read)(void *io_ctx, void *buf, size_t count);
	int (*wait_readable)(void *io_ctx, struct timeval *tv);
};

extern const struct aiyin_io_ops fd_io_ops;
extern const struct aiyin_io_ops usb_io_ops;

#endif /* AIYIN_IO_H */
