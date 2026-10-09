/*
 * Fichero D11s thermal printer protocol implementation
 * Copyright 2026, Gerard Świderski
 */

#include "fichero.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/select.h>
#include <termios.h>

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
		return 0; /* Timeout - no data */
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

static int fichero_open(fichero_t *p, const char *port_path)
{
	int fd;

	if (p->opened) {
		return 0;
	}

	fd = open(port_path, O_RDWR | O_NOCTTY);
	if (fd < 0) {
		perror("Cannot open RFCOMM port");
		return -1;
	}
	printf("Connected to port %s. Initializing protocol...\n", port_path);

	/* Konfiguracja portu w tryb całkowicie surowy (RAW) */
	struct termios t;
	if (tcgetattr(fd, &t) == 0) {
		cfmakeraw(&t); /* Czyści flagi echo, translacji znaków i sygnałów błędu */
		if (tcsetattr(fd, TCSANOW, &t) < 0) {
			perror("Warning: Cannot set termios raw mode");
		}
	}

	p->fd = fd;
	p->opened = true;
	return 0;
}

static void fichero_close(fichero_t *p)
{
	if (p->opened) {
		close(p->fd);
		p->opened = false;
	}
}

static int fichero_status(fichero_t *p, int *p_status)
{
	unsigned char cmd_status[] = { 0x10, 0xFF, 0x40 };
	unsigned char resp[10];
	int status = 0;

	printf("[Status] Checking printer state...\n");
	if (fichero_send_command(p->fd, cmd_status, sizeof(cmd_status)) < 0) {
		return fichero_status_error;
	}

	int len = fichero_read_response(p->fd, resp, sizeof(resp), 2);
	if (len <= 0) {
		fprintf(stderr, "[Error] No status response from printer (Timeout).\n");
		return fichero_status_timeout;
	}

	unsigned char status_byte = resp[0];

	if (len >= 2 && resp[0] == 0xFF) {
		status_byte = resp[1];
	}

	if (status_byte == 0x00) {
		printf("[Status] Printer ready for operation.\n");
		return fichero_status_ok;
	}

	if (status_byte & 0x02) {
		fprintf(stderr, "[Error] Printer cover is open!\n");
		status |= fichero_flag_open_cover;
	}
	if (status_byte & 0x04) {
		fprintf(stderr, "[Error] Out of paper!\n");
		status |= fichero_flag_no_paper;
	}
	if (status_byte & 0x08) {
		fprintf(stderr, "[Error] Low battery!\n");
		status |= fichero_flag_battery_low;
	}
	if (status_byte & 0x40 || status_byte & 0x10) {
		fprintf(stderr, "[Error] Printer head overheated!\n");
		status |= fichero_flag_head_overheat;
	}
	if (status_byte & 0x01) {
		printf("[Status] Printer is currently busy...\n");
		status |= fichero_flag_busy;
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
		return fichero_status_error;
	}

	int len = fichero_read_response(fd, resp, sizeof(resp), 2);
	if (len <= 0) {
		fprintf(stderr, "[Config] %s: no 'OK' response (Timeout)\n", cmd_name);
		return fichero_status_timeout;
	}

	if (strstr((char *)resp, "OK") != NULL) {
		return fichero_status_ok;
	}

	fprintf(stderr, "[Config] %s: Unexpected printer response\n", cmd_name);
	return fichero_status_error;
}

static int fichero_setup_density(fichero_t *p, int density)
{
	unsigned char cmd_density[] = { 0x10, 0xFF, 0x10, 0x00, 0x01 };
	if (density == 0) {
		cmd_density[4] = 0x00;
	}
	else if (density == 2) {
		cmd_density[4] = 0x02;
	}
	return fichero_send_config_cmd(p->fd, cmd_density, sizeof(cmd_density), "Density setup");
}

static int fichero_setup_paper(fichero_t *p, int paper_type)
{
	unsigned char cmd_paper[] = { 0x10, 0xFF, 0x84, 0x00 };
	if (paper_type >= 0 && paper_type <= 2) {
		cmd_paper[3] = (unsigned char)paper_type;
	}
	return fichero_send_config_cmd(p->fd, cmd_paper, sizeof(cmd_paper), "Paper type setup");
}

static int fichero_wakeup(fichero_t *p)
{
	unsigned char cmd_wakeup[12] = { 0 };
	fichero_send_command(p->fd, cmd_wakeup, sizeof(cmd_wakeup));

	unsigned char cmd_enable[] = { 0x10, 0xFF, 0xFE, 0x01 };
	if (fichero_send_command(p->fd, cmd_enable, sizeof(cmd_enable)) < 0) {
		return fichero_status_error;
	}
	return fichero_status_ok;
}

static int fichero_image(fichero_t *p, const unsigned char *bitmap_data, int height_pels)
{
	if (height_pels <= 0) {
		height_pels = FICHERO_DEFAULT_HEIGHT_PELS;
	}

	/* Raster image header: 1D 76 30 mm xL xH yL yH */
	unsigned char img_header[] = {
		0x1D, 0x76, 0x30, 0x00,
		FICHERO_WIDTH_BYTES & 0xFF, (FICHERO_WIDTH_BYTES >> 8) & 0xFF,
		height_pels & 0xFF, (height_pels >> 8) & 0xFF
	};

	if (fichero_send_command(p->fd, img_header, sizeof(img_header)) < 0) {
		return fichero_status_error;
	}

	size_t bitmap_size = (size_t)FICHERO_WIDTH_BYTES * (size_t)height_pels;
	printf("[Data] Sending bitmap buffer (%zu bytes, height: %d)...\n", bitmap_size, height_pels);
	if (fichero_send_command(p->fd, bitmap_data, bitmap_size) < 0) {
		return fichero_status_error;
	}

	return fichero_status_ok;
}

static int fichero_formfeed(fichero_t *p)
{
	unsigned char cmd_feed[] = { 0x1D, 0x0C };
	if (fichero_send_command(p->fd, cmd_feed, sizeof(cmd_feed)) < 0) {
		return fichero_status_error;
	}
	return fichero_status_ok;
}

static int fichero_stop_wait(fichero_t *p)
{
	unsigned char cmd_stop[] = { 0x10, 0xFF, 0xFE, 0x45 };
	printf("[Print] Triggering print mechanism. Waiting for completion signal...\n");
	if (fichero_send_command(p->fd, cmd_stop, sizeof(cmd_stop)) < 0) {
		return fichero_status_error;
	}

	unsigned char final_resp[16];
	memset(final_resp, 0, sizeof(final_resp));

	int final_len = fichero_read_response(p->fd, final_resp, sizeof(final_resp), 30);

	if (final_len > 0) {
		if (final_resp[0] == 0xAA || strstr((char *)final_resp, "OK") != NULL) {
			printf("[Success] Label printed successfully. Device released lock.\n");
			return fichero_status_ok;
		}
		else {
			printf("[Warning] Printer finished work, but returned an unknown response code.\n");
			return fichero_status_error;
		}
	}
	else {
		fprintf(stderr, "[Error] Print timeout. Printer did not confirm operation completion.\n");
		return fichero_status_timeout;
	}

	return fichero_status_error;
}

printer_ops_t printer_ops_fichero = {
	.open = fichero_open,
	.close = fichero_close,
	.status = fichero_status,
	.setup_density = fichero_setup_density,
	.setup_paper = fichero_setup_paper,
	.wakeup = fichero_wakeup,
	.image = fichero_image,
	.formfeed = fichero_formfeed,
	.stop_wait = fichero_stop_wait,
};
