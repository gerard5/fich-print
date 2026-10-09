/*
 * Experimental Niimbot D11 thermal printer protocol implementation
 * Copyright 2026, Gerard Świderski
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdint.h>
#include <sys/select.h>
#include <termios.h>

#include "printer.h"

#define NIIMBOT_PKG_START 0x55
#define NIIMBOT_PKG_END   0xAA

#define NIIMBOT_CMD_CONNECT 0x01
#define NIIMBOT_CMD_STATUS  0x40
#define NIIMBOT_CMD_DENSITY 0x10
#define NIIMBOT_CMD_PAPER   0x05
#define NIIMBOT_CMD_START   0x14
#define NIIMBOT_CMD_BITMAP  0x85
#define NIIMBOT_CMD_END     0x15

struct niimbot_ctx {
	int fd;
	uint16_t tx_seq;
};

static struct niimbot_ctx niimbot_ctx = { 0 }; /* FIXME: alloc dynamicaly */

static int niimbot_send_raw(int fd, const unsigned char *cmd, size_t size)
{
	size_t written = 0;
	while (written < size) {
		ssize_t n = write(fd, cmd + written, size - written);
		if (n < 0) {
			if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
				continue;
			}
			perror("Critical error writing to Niimbot RFCOMM port");
			return -1;
		}
		written += (size_t)n;
	}
	return 0;
}

static int niimbot_read_raw(int fd, unsigned char *buffer, size_t max_len, int timeout_sec)
{
	fd_set set;
	struct timeval timeout;

	FD_ZERO(&set);
	FD_SET(fd, &set);

	timeout.tv_sec = timeout_sec;
	timeout.tv_usec = 0;

	int rv = select(fd + 1, &set, NULL, NULL, &timeout);
	if (rv == -1) {
		if (errno == EINTR)
			return 0;
		perror("Error in select() call for Niimbot");
		return -1;
	}
	else if (rv == 0) {
		return 0; /* Timeout */
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

/*
 * Format: [55 55] [CMD] [LEN] [DATA...] [XOR] [AA AA]
 */
static int niimbot_send_packet(int fd, uint8_t cmd, const uint8_t *data, uint8_t len)
{
	unsigned char pkt[512];
	size_t idx = 0;

	pkt[idx++] = NIIMBOT_PKG_START;
	pkt[idx++] = NIIMBOT_PKG_START;
	pkt[idx++] = cmd;
	pkt[idx++] = len;

	if (data != NULL && len > 0) {
		memcpy(&pkt[idx], data, len);
		idx += len;
	}

	uint8_t xor_sum = 0;
	for (size_t i = 2; i < idx; i++) {
		xor_sum ^= pkt[i];
	}
	pkt[idx++] = xor_sum;

	pkt[idx++] = NIIMBOT_PKG_END;
	pkt[idx++] = NIIMBOT_PKG_END;

	if (niimbot_send_raw(fd, pkt, idx) < 0) {
		return -1;
	}

	usleep(1500);
	return 0;
}

static int niimbot_read_packet(int fd, uint8_t expected_cmd, unsigned char *data_out, size_t max_data_len, int timeout_sec)
{
	unsigned char resp[512];
	int len = niimbot_read_raw(fd, resp, sizeof(resp), timeout_sec);
	if (len <= 0)
		return len; /* Timeout (0) or error (-1) */

	/* min packet: 55 55 [CMD] [LEN] [XOR] AA AA -> 7 bytes */
	if (len < 7)
		return -1;
	if (resp[0] != NIIMBOT_PKG_START || resp[1] != NIIMBOT_PKG_START)
		return -1;
	if (resp[len - 2] != NIIMBOT_PKG_END || resp[len - 1] != NIIMBOT_PKG_END)
		return -1;

	/* uint8_t cmd = resp[2]; XXX */
	uint8_t data_len = resp[3];

	(void)expected_cmd;

	if ((size_t)(data_len + 7) != (size_t)len)
		return -1;

	uint8_t calc_xor = 0;
	for (int i = 2; i < len - 3; i++) {
		calc_xor ^= resp[i];
	}
	if (calc_xor != resp[len - 3]) {
		fprintf(stderr, "[Niimbot] Checksum error (XOR mismatch)!\n");
		return -1;
	}

	if (data_len > 0 && data_out != NULL) {
		if (data_len > max_data_len)
			data_len = (uint8_t)max_data_len;
		memcpy(data_out, &resp[4], data_len);
	}

	return (int)data_len;
}

static int niimbot_open(printer_t *p, const char *port_path)
{
	int fd;

	fd = open(port_path, O_RDWR | O_NOCTTY);
	if (fd < 0) {
		perror("Cannot open Niimbot RFCOMM port");
		return printer_status_error;
	}
	printf("Connected to Niimbot port %s. Executing handshake...\n", port_path);

	struct termios t;
	if (tcgetattr(fd, &t) == 0) {
		cfmakeraw(&t);
		if (tcsetattr(fd, TCSANOW, &t) < 0) {
			perror("Warning: Cannot set termios raw mode for Niimbot");
		}
	}

	/* Handshake: CONNECT (0x01) client version 0x01 */
	uint8_t client_ver = 0x01;
	if (niimbot_send_packet(fd, NIIMBOT_CMD_CONNECT, &client_ver, 1) < 0) {
		close(fd);
		return printer_status_error;
	}

	/* Wait for authorization (0x02) */
	unsigned char handshake_resp[64];
	int r_len = niimbot_read_packet(fd, 0x02, handshake_resp, sizeof(handshake_resp), 2);
	if (r_len <= 0) {
		fprintf(stderr, "[Niimbot] Handshake failed (No response from printer).\n");
		close(fd);
		return printer_status_error;
	}

	niimbot_ctx.fd = fd;
	niimbot_ctx.tx_seq = 0;
	p->private = &niimbot_ctx;

	printf("[Niimbot] Handshake successful. Device authorized.\n");
	return printer_status_ok;
}

static int niimbot_close(printer_t *p)
{
	struct niimbot_ctx *ctx = (struct niimbot_ctx *)p->private;

	if (ctx->fd < 0) {
		return printer_status_error;
	}

	close(ctx->fd);
	ctx->fd = -1;

	p->private = NULL;
	close(ctx->fd);

	return printer_status_ok;
}

static int niimbot_status(printer_t *p, int *p_status)
{
	struct niimbot_ctx *ctx = (struct niimbot_ctx *)p->private;
	uint8_t cmd_status_data = 0x01;
	unsigned char resp_data[32];
	int status = 0;

	printf("[Status] Requesting Niimbot printer state...\n");
	if (niimbot_send_packet(ctx->fd, NIIMBOT_CMD_STATUS, &cmd_status_data, 1) < 0) {
		return printer_status_error;
	}

	int len = niimbot_read_packet(ctx->fd, 0x41, resp_data, sizeof(resp_data), 2);
	if (len <= 0) {
		fprintf(stderr, "[Error] No status response from Niimbot (Timeout).\n");
		return printer_status_timeout;
	}

	uint8_t status_byte = resp_data[0];

	if (status_byte == 0x00) {
		printf("[Status] Niimbot printer ready.\n");
		if (p_status)
			*p_status = printer_status_ok;
		return printer_status_ok;
	}

	if (status_byte & 0x01) {
		printf("[Status] Niimbot is currently busy...\n");
		status |= printer_flag_busy;
	}
	if (status_byte & 0x02) {
		fprintf(stderr, "[Error] Niimbot cover is open!\n");
		status |= printer_flag_open_cover;
	}
	if (status_byte & 0x04) {
		fprintf(stderr, "[Error] Out of paper!\n");
		status |= printer_flag_no_paper;
	}
	if (status_byte & 0x08) {
		fprintf(stderr, "[Error] Low battery!\n");
		status |= printer_flag_battery_low;
	}
	if (status_byte & 0x10 || status_byte & 0x40) {
		fprintf(stderr, "[Error] Niimbot head overheated!\n");
		status |= printer_flag_head_overheat;
	}

	if (p_status != NULL) {
		*p_status = status;
	}

	return status;
}

static int niimbot_setup_density(printer_t *p, int density)
{
	struct niimbot_ctx *ctx = (struct niimbot_ctx *)p->private;

	/* Map: 1 (low), 2 (medium), 3 (high) */
	uint8_t d_val = 2;
	if (density == 0)
		d_val = 1;
	else if (density == 2)
		d_val = 3;

	printf("[Config] Setting Niimbot density to %d...\n", d_val);
	if (niimbot_send_packet(ctx->fd, NIIMBOT_CMD_DENSITY, &d_val, 1) < 0) {
		return printer_status_error;
	}

	return printer_status_ok;
}

static int niimbot_setup_paper(printer_t *p, int paper_type)
{
	struct niimbot_ctx *ctx = (struct niimbot_ctx *)p->private;

	/* Map: 1 = Gap/Label, 2 = Black Mark, 3 = Continuous */
	uint8_t p_val = 1;
	if (paper_type == 1)
		p_val = 2;
	else if (paper_type == 2)
		p_val = 3;

	printf("[Config] Setting Niimbot paper type to %d...\n", p_val);
	if (niimbot_send_packet(ctx->fd, NIIMBOT_CMD_PAPER, &p_val, 1) < 0) {
		return printer_status_error;
	}

	return printer_status_ok;
}

static int niimbot_set_property(printer_t *p, int key, int value)
{
	switch (key) {
		case printer_prop_density: return niimbot_setup_density(p, value);
		case printer_prop_paper: return niimbot_setup_paper(p, value);
		default: break;
	}

	return printer_status_error;
}

static int niimbot_wakeup(printer_t *p)
{
	struct niimbot_ctx *ctx = (struct niimbot_ctx *)p->private;

	uint8_t start_data[] = { 0x00, 0x01 };

	printf("[Print] Sending Print-Start initialization to Niimbot...\n");
	if (niimbot_send_packet(ctx->fd, NIIMBOT_CMD_START, start_data, sizeof(start_data)) < 0) {
		return printer_status_error;
	}

	ctx->tx_seq = 0;

	return printer_status_ok;
}

static int niimbot_send_image(printer_t *p, const unsigned char *bitmap_data, int height_pels)
{
	struct niimbot_ctx *ctx = (struct niimbot_ctx *)p->private;

	if (height_pels <= 0) {
		height_pels = FICHERO_DEFAULT_HEIGHT_PELS;
	}

	printf("[Data] Strumieniowanie bitmapy do Niimbota (Wysokość: %d rzędów)...\n", height_pels);

	/* (CMD 0x85) [line_no_H] [line_no_L] [repeats=1] [12 bytes of data] -> 15 bytes total */
	unsigned char line_pkt[15];

	for (int y = 0; y < height_pels; y++) {
		uint16_t current_line = ctx->tx_seq;

		line_pkt[0] = (unsigned char)((current_line >> 8) & 0xFF);
		line_pkt[1] = (unsigned char)(current_line & 0xFF);
		line_pkt[2] = 1;

		memcpy(&line_pkt[3], &bitmap_data[y * FICHERO_WIDTH_BYTES], FICHERO_WIDTH_BYTES);

		if (niimbot_send_packet(ctx->fd, NIIMBOT_CMD_BITMAP, line_pkt, sizeof(line_pkt)) < 0) {
			return printer_status_error;
		}

		ctx->tx_seq++;
	}

	return printer_status_ok;
}

static int niimbot_formfeed(printer_t *p)
{
	struct niimbot_ctx *ctx = (struct niimbot_ctx *)p->private;

	uint8_t end_val = 0x01;
	printf("[Print] Sending End-Of-Print / Formfeed command to Niimbot...\n");
	if (niimbot_send_packet(ctx->fd, NIIMBOT_CMD_END, &end_val, 1) < 0) {
		return printer_status_error;
	}
	return printer_status_ok;
}

static int niimbot_stop_wait(printer_t *p)
{
	struct niimbot_ctx *ctx = (struct niimbot_ctx *)p->private;

	printf("[Print] Waiting for Niimbot to finish mechanical movement...\n");

	unsigned char final_data[16];
	int final_len = niimbot_read_packet(ctx->fd, 0x16, final_data, sizeof(final_data), 15);

	if (final_len >= 0) {
		printf("[Success] Niimbot finished physical label printing successfully.\n");
		return printer_status_ok;
	}
	else {
		fprintf(stderr, "[Error] Timeout or packet error waiting for Niimbot completion.\n");
		return printer_status_timeout;
	}
}

struct printer_ops printer_ops_niimbot = {
	.open = niimbot_open,
	.close = niimbot_close,
	.status = niimbot_status,
	.set_property = niimbot_set_property,
	.wakeup = niimbot_wakeup,
	.send_image = niimbot_send_image,
	.formfeed = niimbot_formfeed,
	.stop_wait = niimbot_stop_wait,
};
