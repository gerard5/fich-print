/*
 * fichero/aiyin file descriptor based IO operations API
 * Copyright 2026, Gerard Świderski
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <errno.h>
#include "aiyin_io.h"

struct fd_io_ctx {
	int fd;
};

static int fd_open(void **io_ctx, const char *port_path)
{
	int fd = open(port_path, O_RDWR | O_NOCTTY);
	if (fd < 0) {
		perror("Cannot open RFCOMM port");
		return -1;
	}

	struct termios t;
	if (tcgetattr(fd, &t) == 0) {
		cfmakeraw(&t);
		if (tcsetattr(fd, TCSANOW, &t) < 0) {
			perror("Warning: Cannot set termios raw mode");
		}
	}

	struct fd_io_ctx *ctx = calloc(1, sizeof(*ctx));
	if (ctx == NULL) {
		close(fd);
		return -1;
	}
	ctx->fd = fd;

	*io_ctx = ctx;
	return 0;
}

static void fd_close(void *io_ctx)
{
	struct fd_io_ctx *ctx = io_ctx;
	if (ctx == NULL) {
		return;
	}

	if (ctx->fd >= 0) {
		close(ctx->fd);
	}

	free(ctx);
}

static ssize_t fd_write(void *io_ctx, const void *buf, size_t count)
{
	struct fd_io_ctx *ctx = io_ctx;
	return write(ctx->fd, buf, count);
}

static ssize_t fd_read(void *io_ctx, void *buf, size_t count)
{
	struct fd_io_ctx *ctx = io_ctx;
	return read(ctx->fd, buf, count);
}

static int fd_wait_readable(void *io_ctx, struct timeval *tv)
{
	struct fd_io_ctx *ctx = io_ctx;
	fd_set set;
	FD_ZERO(&set);
	FD_SET(ctx->fd, &set);
	return select(ctx->fd + 1, &set, NULL, NULL, tv);
}

const struct aiyin_io_ops fd_io_ops = {
	.open = fd_open,
	.close = fd_close,
	.write = fd_write,
	.read = fd_read,
	.wait_readable = fd_wait_readable,
};
