/*
 * Fichero D11s thermal printer protocol implementation
 * Copyright 2026 Gerard Świderski
 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/select.h>
#include <termios.h>

#include "aiyin_io.h"
#include "../printer.h"

#define _log_prefix          "aiyin: "
#define _log(fmt, ...)       printf(_log_prefix fmt "\n", ##__VA_ARGS__)
#define _log_error(fmt, ...) fprintf(stderr, _log_prefix "Error: " fmt "\n", ##__VA_ARGS__)

#if defined(EWOULDBLOCK) && (EWOULDBLOCK != EAGAIN)
#define CHECK_WOULD_BLOCK(e) ((e) == EAGAIN || (e) == EWOULDBLOCK)
#else
#define CHECK_WOULD_BLOCK(e) ((e) == EAGAIN)
#endif

struct aiyin_ctx {
	void *io_ctx;
	const struct aiyin_io_ops *io;
	int density_level;
	int paper_type;
};

/* Helper function: safe write to the printer handling EINTR and partial writes */
static int aiyin_send_command(struct aiyin_ctx *ctx, const unsigned char *cmd, size_t size)
{
	assert(ctx->io != NULL);
	assert(ctx->io->write != NULL);

	size_t written = 0;
	while (written < size) {
		ssize_t n = ctx->io->write(ctx->io_ctx, cmd + written, size - written);
		if (n < 0) {
			if (errno == EINTR || CHECK_WOULD_BLOCK(errno)) {
				continue;
			}
			perror("Critical error writing to printer transport");
			return -1;
		}
		written += (size_t)n;
	}
	return 0;
}

/* Helper function: read from port with timeout in seconds handling EINTR */
static int aiyin_read_response(struct aiyin_ctx *ctx, unsigned char *buffer, size_t max_len, int timeout_sec)
{
	size_t total_bytes = 0;
	struct timeval tv;

	assert(ctx->io != NULL);
	assert(ctx->io->read != NULL);
	assert(ctx->io->wait_readable != NULL);

	tv.tv_sec = timeout_sec;
	tv.tv_usec = 0;

	while (total_bytes < max_len) {

		if (total_bytes > 0) {
			/* 100ms inter-byte timeout */
			tv.tv_sec = 0;
			tv.tv_usec = 100 * 1000;
		}

		int rv = ctx->io->wait_readable(ctx->io_ctx, &tv);
		if (rv == -1) {
			if (errno == EINTR || CHECK_WOULD_BLOCK(errno)) {
				continue;
			}
			perror("Error in wait_readable() call");
			return -1;
		}
		else if (rv == 0) {
			/* Timeout */
			return (int)total_bytes;
		}

		ssize_t bytes_read = ctx->io->read(ctx->io_ctx, buffer + total_bytes, max_len - total_bytes);
		if (bytes_read < 0) {
			if (errno == EINTR || CHECK_WOULD_BLOCK(errno)) {
				continue;
			}
			perror("Error in read() call");
			return -1;
		}
		else if (bytes_read == 0) {
			return (int)total_bytes;
		}

		total_bytes += bytes_read;
	}

	return (int)total_bytes;
}

static int aiyin_open(printer_t *p, const char *port_path)
{
	struct aiyin_ctx *ctx = calloc(1, sizeof(*ctx));

	if (ctx == NULL) {
		return printer_status_error;
	}

	if (strncmp(port_path, "usb", 3) == 0) {
		ctx->io = &usb_io_ops;
	}
	else {
		ctx->io = &fd_io_ops;
	}

	if (ctx->io->open(&ctx->io_ctx, port_path) < 0) {
		free(ctx);
		return printer_status_error;
	}

	_log("Connected to port %s. Initializing protocol...", port_path);

	p->private = ctx;

	return printer_status_ok;
}

static int aiyin_close(printer_t *p)
{
	struct aiyin_ctx *ctx = p->private;
	if (ctx == NULL) {
		return printer_status_error;
	}

	if (ctx->io && ctx->io->close) {
		ctx->io->close(ctx->io_ctx);
	}

	ctx->io_ctx = NULL;

	free(ctx);
	p->private = NULL;

	return printer_status_ok;
}


static int aiyin_status(printer_t *p, int *p_status)
{
	struct aiyin_ctx *ctx = (struct aiyin_ctx *)p->private;

	if (ctx == NULL) {
		return printer_status_error;
	}

	unsigned char cmd_status[] = { 0x10, 0xFF, 0x40 };
	unsigned char resp[10];
	int status = 0;

	_log("Checking printer state...");
	if (aiyin_send_command(ctx, cmd_status, sizeof(cmd_status)) < 0) {
		return printer_status_error;
	}

	int len = aiyin_read_response(ctx, resp, sizeof(resp), 2);
	if (len <= 0) {
		_log_error("No status response from printer (Timeout).");
		return printer_status_timeout;
	}

	unsigned char status_byte = resp[0];

	if (len >= 2 && resp[0] == 0xFF) {
		status_byte = resp[1];
	}

	if (status_byte == 0x00) {
		_log("Printer ready for operation.");
		return printer_status_ok;
	}

	if (status_byte & 0x02) {
		_log_error("Printer cover is open!");
		status |= printer_flag_open_cover;
	}
	if (status_byte & 0x04) {
		_log_error("Out of paper!");
		status |= printer_flag_no_paper;
	}
	if (status_byte & 0x08) {
		_log_error("Low battery!");
		status |= printer_flag_battery_low;
	}
	if (status_byte & 0x40 || status_byte & 0x10) {
		_log_error("Printer head overheated!");
		status |= printer_flag_head_overheat;
	}
	if (status_byte & 0x01) {
		_log_error("Printer is currently busy...");
		status |= printer_flag_busy;
	}

	if (p_status != NULL) {
		*p_status = status;
	}

	return status;
}

static int aiyin_send_config_cmd(struct aiyin_ctx *ctx, const unsigned char *cmd, size_t size, const char *cmd_name)
{
	unsigned char resp[16];
	memset(resp, 0, sizeof(resp));

	if (aiyin_send_command(ctx, cmd, size) < 0) {
		return printer_status_error;
	}

	int len = aiyin_read_response(ctx, resp, sizeof(resp), 2);
	if (len <= 0) {
		_log_error("%s: no 'OK' response (Timeout)", cmd_name);
		return printer_status_timeout;
	}

	if (strstr((char *)resp, "OK") != NULL) {
		return printer_status_ok;
	}

	_log_error("%s: Unexpected printer response", cmd_name);
	return printer_status_error;
}

static int aiyin_setup_density(printer_t *p, int density)
{
	struct aiyin_ctx *ctx = (struct aiyin_ctx *)p->private;

	if (ctx == NULL) {
		return printer_status_error;
	}

	if (density < 0 || density > 2) {
		return printer_status_error;
	}

	unsigned char cmd_density[] = { 0x10, 0xFF, 0x10, 0x00, 0x00 };

	cmd_density[4] = density;
	ctx->density_level = density;

	return aiyin_send_config_cmd(ctx, cmd_density, sizeof(cmd_density), "Density setup");
}

static int aiyin_setup_paper(printer_t *p, int paper_type)
{
	struct aiyin_ctx *ctx = (struct aiyin_ctx *)p->private;

	if (ctx == NULL) {
		return printer_status_error;
	}

	if (paper_type < 0 || paper_type > 2) {
		return printer_status_error;
	}

	unsigned char cmd_paper[] = { 0x10, 0xFF, 0x84, 0x00 };

	cmd_paper[3] = paper_type;
	ctx->paper_type = paper_type;

	return aiyin_send_config_cmd(ctx, cmd_paper, sizeof(cmd_paper), "Paper type setup");
}

static int aiyin_set_property(printer_t *p, int key, int value)
{
	switch (key) {
		case printer_prop_density: return aiyin_setup_density(p, value);
		case printer_prop_paper: return aiyin_setup_paper(p, value);
		default: break;
	}

	return printer_status_error;
}

static int aiyin_wakeup(printer_t *p)
{
	struct aiyin_ctx *ctx = (struct aiyin_ctx *)p->private;

	if (ctx == NULL) {
		return printer_status_error;
	}

	unsigned char cmd_wakeup[12] = { 0 };
	aiyin_send_command(ctx, cmd_wakeup, sizeof(cmd_wakeup));

	unsigned char cmd_enable[] = { 0x10, 0xFF, 0xFE, 0x01 };
	if (aiyin_send_command(ctx, cmd_enable, sizeof(cmd_enable)) < 0) {
		return printer_status_error;
	}

	return printer_status_ok;
}

static int aiyin_send_image(printer_t *p, const unsigned char *bitmap_data, int height_pels)
{
	struct aiyin_ctx *ctx = (struct aiyin_ctx *)p->private;

	if (ctx == NULL) {
		return printer_status_error;
	}

	if (height_pels <= 0) {
		height_pels = PRINTER_DEFAULT_HEIGHT_PELS;
	}

	/* Raster image header: 1D 76 30 mm xL xH yL yH */
	unsigned char img_header[] = {
		0x1D, 0x76, 0x30, 0x00,
		PRINTER_WIDTH_BYTES & 0xFF, (PRINTER_WIDTH_BYTES >> 8) & 0xFF,
		height_pels & 0xFF, (height_pels >> 8) & 0xFF
	};

	if (aiyin_send_command(ctx, img_header, sizeof(img_header)) < 0) {
		return printer_status_error;
	}

	size_t bitmap_size = (size_t)PRINTER_WIDTH_BYTES * (size_t)height_pels;
	_log("Sending bitmap buffer (%zu bytes, height: %d)...", bitmap_size, height_pels);
	if (aiyin_send_command(ctx, bitmap_data, bitmap_size) < 0) {
		return printer_status_error;
	}

	return printer_status_ok;
}

static int aiyin_formfeed(printer_t *p)
{
	struct aiyin_ctx *ctx = (struct aiyin_ctx *)p->private;

	if (ctx == NULL) {
		return printer_status_error;
	}

	unsigned char cmd_feed[] = { 0x1D, 0x0C };
	if (aiyin_send_command(ctx, cmd_feed, sizeof(cmd_feed)) < 0) {
		return printer_status_error;
	}
	return printer_status_ok;
}

static int aiyin_stop_wait(printer_t *p)
{
	struct aiyin_ctx *ctx = (struct aiyin_ctx *)p->private;

	if (ctx == NULL) {
		return printer_status_error;
	}

	unsigned char cmd_stop[] = { 0x10, 0xFF, 0xFE, 0x45 };
	_log("Triggering print mechanism. Waiting for completion signal...");
	if (aiyin_send_command(ctx, cmd_stop, sizeof(cmd_stop)) < 0) {
		return printer_status_error;
	}

	unsigned char final_resp[16];
	memset(final_resp, 0, sizeof(final_resp));

	int final_len = aiyin_read_response(ctx, final_resp, sizeof(final_resp), 30);

	if (final_len > 0) {
		if (final_resp[0] == 0xAA || strstr((char *)final_resp, "OK") != NULL) {
			_log("Label printed successfully. Device released lock.");
			return printer_status_ok;
		}
		else {
			_log("Printer finished work, but returned an unknown response code.");
			return printer_status_error;
		}
	}
	else {
		_log_error("Print timeout. Printer did not confirm operation completion.");
		return printer_status_timeout;
	}

	return printer_status_error;
}

const struct printer_ops printer_ops_aiyin = {
	.open = aiyin_open,
	.close = aiyin_close,
	.status = aiyin_status,
	.set_property = aiyin_set_property,
	.wakeup = aiyin_wakeup,
	.send_image = aiyin_send_image,
	.formfeed = aiyin_formfeed,
	.stop_wait = aiyin_stop_wait,
};
