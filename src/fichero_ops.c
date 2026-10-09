/*
 * Fichero D11s thermal printer protocol implementation
 * Copyright 2026, Gerard Świderski
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/select.h>
#include <termios.h>

#include "printer.h"

#define _log_prefix          "fichero: "
#define _log(fmt, ...)       printf(_log_prefix fmt "\n", ##__VA_ARGS__)
#define _log_error(fmt, ...) fprintf(stderr, _log_prefix "Error: " fmt "\n", ##__VA_ARGS__)

struct fichero_ctx {
	int fd;
};

static struct fichero_ctx fichero_ctx = { .fd = -1 };

/* Helper function: safe write to the printer handling EINTR and partial writes */
static int fichero_send_command(int fd, const unsigned char *cmd, size_t size)
{
	size_t written = 0;
	while (written < size) {
		ssize_t n = write(fd, cmd + written, size - written);
		if (n < 0) {
			if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
				continue;
			}
			perror("Critical error writing to RFCOMM port");
			return -1;
		}
		written += (size_t)n;
	}
	return 0;
}

/* Helper function: read from port with timeout in seconds handling EINTR */
static int fichero_read_response(int fd, unsigned char *buffer, size_t max_len, int timeout_sec)
{
	fd_set set;
	struct timeval timeout;

	FD_ZERO(&set);
	FD_SET(fd, &set);

	timeout.tv_sec = timeout_sec;
	timeout.tv_usec = 0;

	int rv = select(fd + 1, &set, NULL, NULL, &timeout);
	if (rv == -1) {
		if (errno == EINTR) {
			return 0;
		}
		perror("Error in select() call");
		return -1;
	}
	else if (rv == 0) {
		/* Timeout - no data */
		return 0;
	}

	ssize_t bytes_read = read(fd, buffer, max_len);
	if (bytes_read < 0) {
		if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
			return 0;
		}
		return -1;
	}
	return (int)bytes_read;
}

static int fichero_open(printer_t *p, const char *port_path)
{
	int fd = open(port_path, O_RDWR | O_NOCTTY);
	if (fd < 0) {
		perror("Cannot open RFCOMM port");
		return printer_status_error;
	}
	_log("Connected to port %s. Initializing protocol...", port_path);

	/* Configure port in raw mode */
	struct termios t;
	if (tcgetattr(fd, &t) == 0) {
		cfmakeraw(&t);
		if (tcsetattr(fd, TCSANOW, &t) < 0) {
			perror("Warning: Cannot set termios raw mode");
		}
	}

	fichero_ctx.fd = fd;
	p->private = &fichero_ctx;

	return printer_status_ok;
}

static int fichero_close(printer_t *p)
{
	struct fichero_ctx *ctx = (struct fichero_ctx *)p->private;

	if (ctx == NULL || ctx->fd < 0) {
		return printer_status_error;
	}

	close(ctx->fd);
	ctx->fd = -1;

	p->private = NULL;

	return printer_status_ok;
}

static int fichero_status(printer_t *p, int *p_status)
{
	struct fichero_ctx *ctx = (struct fichero_ctx *)p->private;
	unsigned char cmd_status[] = { 0x10, 0xFF, 0x40 };
	unsigned char resp[10];
	int status = 0;

	_log("Checking printer state...");
	if (fichero_send_command(ctx->fd, cmd_status, sizeof(cmd_status)) < 0) {
		return printer_status_error;
	}

	int len = fichero_read_response(ctx->fd, resp, sizeof(resp), 2);
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

static int fichero_send_config_cmd(int fd, const unsigned char *cmd, size_t size, const char *cmd_name)
{
	unsigned char resp[16];
	memset(resp, 0, sizeof(resp));

	if (fichero_send_command(fd, cmd, size) < 0) {
		return printer_status_error;
	}

	int len = fichero_read_response(fd, resp, sizeof(resp), 2);
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

static int fichero_setup_density(printer_t *p, int density)
{
	struct fichero_ctx *ctx = (struct fichero_ctx *)p->private;
	unsigned char cmd_density[] = { 0x10, 0xFF, 0x10, 0x00, 0x01 };
	if (density == 0) {
		cmd_density[4] = 0x00;
	}
	else if (density == 2) {
		cmd_density[4] = 0x02;
	}
	return fichero_send_config_cmd(ctx->fd, cmd_density, sizeof(cmd_density), "Density setup");
}

static int fichero_setup_paper(printer_t *p, int paper_type)
{
	struct fichero_ctx *ctx = (struct fichero_ctx *)p->private;
	unsigned char cmd_paper[] = { 0x10, 0xFF, 0x84, 0x00 };
	if (paper_type >= 0 && paper_type <= 2) {
		cmd_paper[3] = (unsigned char)paper_type;
	}
	return fichero_send_config_cmd(ctx->fd, cmd_paper, sizeof(cmd_paper), "Paper type setup");
}

static int fichero_set_property(printer_t *p, int key, int value)
{
	switch (key) {
		case printer_prop_density: return fichero_setup_density(p, value);
		case printer_prop_paper: return fichero_setup_paper(p, value);
		default: break;
	}

	return printer_status_error;
}

static int fichero_wakeup(printer_t *p)
{
	struct fichero_ctx *ctx = (struct fichero_ctx *)p->private;
	unsigned char cmd_wakeup[12] = { 0 };
	fichero_send_command(ctx->fd, cmd_wakeup, sizeof(cmd_wakeup));

	unsigned char cmd_enable[] = { 0x10, 0xFF, 0xFE, 0x01 };
	if (fichero_send_command(ctx->fd, cmd_enable, sizeof(cmd_enable)) < 0) {
		return printer_status_error;
	}
	return printer_status_ok;
}

static int fichero_send_image(printer_t *p, const unsigned char *bitmap_data, int height_pels)
{
	struct fichero_ctx *ctx = (struct fichero_ctx *)p->private;
	if (height_pels <= 0) {
		height_pels = FICHERO_DEFAULT_HEIGHT_PELS;
	}

	/* Raster image header: 1D 76 30 mm xL xH yL yH */
	unsigned char img_header[] = {
		0x1D, 0x76, 0x30, 0x00,
		FICHERO_WIDTH_BYTES & 0xFF, (FICHERO_WIDTH_BYTES >> 8) & 0xFF,
		height_pels & 0xFF, (height_pels >> 8) & 0xFF
	};

	if (fichero_send_command(ctx->fd, img_header, sizeof(img_header)) < 0) {
		return printer_status_error;
	}

	size_t bitmap_size = (size_t)FICHERO_WIDTH_BYTES * (size_t)height_pels;
	_log("Sending bitmap buffer (%zu bytes, height: %d)...", bitmap_size, height_pels);
	if (fichero_send_command(ctx->fd, bitmap_data, bitmap_size) < 0) {
		return printer_status_error;
	}

	return printer_status_ok;
}

static int fichero_formfeed(printer_t *p)
{
	struct fichero_ctx *ctx = (struct fichero_ctx *)p->private;
	unsigned char cmd_feed[] = { 0x1D, 0x0C };
	if (fichero_send_command(ctx->fd, cmd_feed, sizeof(cmd_feed)) < 0) {
		return printer_status_error;
	}
	return printer_status_ok;
}

static int fichero_stop_wait(printer_t *p)
{
	struct fichero_ctx *ctx = (struct fichero_ctx *)p->private;
	unsigned char cmd_stop[] = { 0x10, 0xFF, 0xFE, 0x45 };
	_log("Triggering print mechanism. Waiting for completion signal...");
	if (fichero_send_command(ctx->fd, cmd_stop, sizeof(cmd_stop)) < 0) {
		return printer_status_error;
	}

	unsigned char final_resp[16];
	memset(final_resp, 0, sizeof(final_resp));

	int final_len = fichero_read_response(ctx->fd, final_resp, sizeof(final_resp), 30);

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

const struct printer_ops printer_ops_fichero = {
	.open = fichero_open,
	.close = fichero_close,
	.status = fichero_status,
	.set_property = fichero_set_property,
	.wakeup = fichero_wakeup,
	.send_image = fichero_send_image,
	.formfeed = fichero_formfeed,
	.stop_wait = fichero_stop_wait,
};
