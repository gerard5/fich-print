/*
 * fichero/aiyin USB based IO operations API
 * Copyright 2026 Gerard Świderski
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/time.h>
#include <libusb-1.0/libusb.h>
#include "aiyin_io.h"

#define AIYIN_VID 0x09c5
#define AIYIN_PID 0x0200
#define EP_OUT    0x01
#define EP_IN     0x81

struct usb_io_ctx {
	libusb_device_handle *usb_handle;
	int curr_timeout_ms;
};

static int usb_open(void **io_ctx, const char *port_path)
{
	(void)port_path;

	if (libusb_init(NULL) < 0) {
		return -1;
	}

	libusb_device_handle *usb_handle = libusb_open_device_with_vid_pid(NULL, AIYIN_VID, AIYIN_PID);
	if (usb_handle == NULL) {
		libusb_exit(NULL);
		return -1;
	}

	/* unplug usblp kernel driver */
	if (libusb_kernel_driver_active(usb_handle, 0) == 1) {
		if (libusb_detach_kernel_driver(usb_handle, 0) < 0) {
			perror("Warning: Could not detach kernel driver");
		}
	}

	if (libusb_claim_interface(usb_handle, 0) < 0) {
		libusb_close(usb_handle);
		libusb_exit(NULL);
		return -1;
	}

	struct usb_io_ctx *ctx = calloc(1, sizeof(*ctx));
	if (ctx == NULL) {
		libusb_release_interface(usb_handle, 0);
		libusb_close(usb_handle);
		libusb_exit(NULL);
		return -1;
	}
	ctx->usb_handle = usb_handle;
	ctx->curr_timeout_ms = 5 * 1000;

	*io_ctx = ctx;
	return 0;
}

static void usb_close(void *io_ctx)
{
	struct usb_io_ctx *ctx = io_ctx;
	if (ctx == NULL) {
		return;
	}

	if (ctx->usb_handle) {
		libusb_release_interface(ctx->usb_handle, 0);
		libusb_attach_kernel_driver(ctx->usb_handle, 0);
		libusb_close(ctx->usb_handle);
	}
	libusb_exit(NULL);
	free(ctx);
}

static ssize_t usb_write(void *io_ctx, const void *buf, size_t count)
{
	struct usb_io_ctx *ctx = io_ctx;
	int transferred = 0;
	int rc = libusb_bulk_transfer(ctx->usb_handle, EP_OUT, (unsigned char *)buf, count, &transferred, 5000);
	if (rc != 0) {
		errno = EIO;
		return -1;
	}
	return transferred;
}

static ssize_t usb_read(void *io_ctx, void *buf, size_t count)
{
	struct usb_io_ctx *ctx = io_ctx;
	int transferred = 0;

	int rc = libusb_bulk_transfer(ctx->usb_handle, EP_IN, (unsigned char *)buf, count, &transferred, ctx->curr_timeout_ms);

	if (rc != 0) {
		if (rc == LIBUSB_ERROR_TIMEOUT) {
			return 0;
		}
		fprintf(stderr, "[USB ERROR] Bulk read failed: %s\n", libusb_error_name(rc));
		errno = EIO;
		return -1;
	}
	return transferred;
}

static int usb_wait_readable(void *io_ctx, struct timeval *tv)
{
	struct usb_io_ctx *ctx = io_ctx;

	if (tv) {
		ctx->curr_timeout_ms = (tv->tv_sec * 1000) + (tv->tv_usec / 1000);
		if (ctx->curr_timeout_ms == 0) {
			ctx->curr_timeout_ms = 1;
		}
	}
	else {
		ctx->curr_timeout_ms = 0;
	}

	return 1;
}


const struct aiyin_io_ops usb_io_ops = {
	.open = usb_open,
	.close = usb_close,
	.write = usb_write,
	.read = usb_read,
	.wait_readable = usb_wait_readable,
};
