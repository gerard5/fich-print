/*
 * Experimental Niimbot D11 thermal printer protocol implementation
 * Copyright 2026, Gerard Świderski
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <pthread.h>
#include <time.h>
#include <stdint.h>
#include <stdbool.h>

#include "printer.h"

#define NIIMBOT_PKG_START 0x55
#define NIIMBOT_PKG_END   0xAA

#define NIIMBOT_CMD_CONNECT          0x01
#define NIIMBOT_CMD_CONNECT_RESP     0x02
#define NIIMBOT_CMD_START_PRINT      0x01
#define NIIMBOT_CMD_START_PAGE_PRINT 0x03
#define NIIMBOT_CMD_SET_DIMENSION    0x05
#define NIIMBOT_CMD_SET_DENSITY      0x21
#define NIIMBOT_CMD_SET_LABEL_TYPE   0x23
#define NIIMBOT_CMD_EMPTY_ROW        0x84
#define NIIMBOT_CMD_BITMAP_ROW       0x85
#define NIIMBOT_CMD_END_PAGE_PRINT   0xF3
#define NIIMBOT_CMD_END_PRINT        0xF5
#define NIIMBOT_CMD_HEARTBEAT_RESP   0xDD

#define _log_prefix          "niimbot: "
#define _log(fmt, ...)       printf(_log_prefix fmt "\n", ##__VA_ARGS__)
#define _log_error(fmt, ...) fprintf(stderr, _log_prefix "Error: " fmt "\n", ##__VA_ARGS__)

struct niimbot_ctx {
	int fd;
	pthread_t read_thread;
	volatile bool keep_running;

	/* Temporary/current configuration */
	int density_level;
	int paper_type;

	/* Asynchronous status section (Heartbeat) */
	pthread_mutex_t status_mutex;
	int current_flags;

	/* Command synchronization section REQ-RESP */
	pthread_mutex_t cmd_mutex;
	pthread_cond_t cmd_cond;
	uint8_t awaited_cmd;
	volatile bool cmd_received;
	unsigned char cmd_resp_buf[64];
	int cmd_resp_len;
};

static int niimbot_send_raw(int fd, const unsigned char *cmd, size_t size)
{
	size_t written = 0;

	while (written < size) {
		ssize_t n = write(fd, cmd + written, size - written);
		if (n < 0) {
			if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
				continue;
			}

			return -1;
		}
		written += (size_t)n;
	}

	return 0;
}

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

	uint8_t xor_sum = cmd ^ len;
	for (size_t i = 4; i < idx; i++) {
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

static int niimbot_read_raw(int fd, unsigned char *buffer, size_t max_len, int timeout_sec)
{
	fd_set set;
	struct timeval timeout;

	FD_ZERO(&set);
	FD_SET(fd, &set);
	timeout.tv_sec = timeout_sec;
	timeout.tv_usec = 0;

	int rv = select(fd + 1, &set, NULL, NULL, &timeout);
	if (rv <= 0) {
		/* 0 = timeout, -1 = error */
		return rv;
	}

	ssize_t bytes_read = read(fd, buffer, max_len);
	if (bytes_read < 0) {
		return (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) ? 0 : -1;
	}

	return (int)bytes_read;
}

static int niimbot_read_packet(int fd, uint8_t *out_cmd, unsigned char *data_out, size_t max_data_len, int timeout_sec)
{
	unsigned char b;
	int start_state = 0;

	while (start_state < 2) {
		if (niimbot_read_raw(fd, &b, 1, timeout_sec) != 1) {
			return -1;
		}

		if (b == NIIMBOT_PKG_START) {
			start_state++;
		}
		else {
			start_state = 0;
		}
	}

	unsigned char header[2];
	for (int i = 0; i < 2; i++) {
		if (niimbot_read_raw(fd, &header[i], 1, timeout_sec) != 1) {
			return -1;
		}
	}

	uint8_t cmd = header[0];
	uint8_t data_len = header[1];
	uint8_t calc_xor = cmd ^ data_len;

	unsigned char payload_buf[256];
	for (int i = 0; i < data_len; i++) {
		if (niimbot_read_raw(fd, &payload_buf[i], 1, timeout_sec) != 1) {
			return -1;
		}

		calc_xor ^= payload_buf[i];
	}

	uint8_t recv_xor;
	if (niimbot_read_raw(fd, &recv_xor, 1, timeout_sec) != 1) {
		return -1;
	}

	unsigned char tail[2];
	for (int i = 0; i < 2; i++) {
		if (niimbot_read_raw(fd, &tail[i], 1, timeout_sec) != 1) {
			return -1;
		}
	}

	if (tail[0] != NIIMBOT_PKG_END || tail[1] != NIIMBOT_PKG_END) {
		return -1;
	}

	if (calc_xor != recv_xor) {
		return -1;
	}

	if (data_len > 0 && data_out != NULL) {
		size_t copy_len = (data_len > max_data_len) ? max_data_len : data_len;
		memcpy(data_out, payload_buf, copy_len);
	}

	if (out_cmd) {
		*out_cmd = cmd;
	}

	return (int)data_len;
}

static void *niimbot_read_thread_func(void *arg)
{
	unsigned char packet_data[256];

	printer_t *p = arg;
	struct niimbot_ctx *ctx = (struct niimbot_ctx *)p->private;

	while (ctx->keep_running) {
		uint8_t received_cmd = 0;
		int len = niimbot_read_packet(ctx->fd, &received_cmd, packet_data, sizeof(packet_data), 1);

		if (len < 0) {
			/* Connection lost (e.g., printer turned off) */
			pthread_mutex_lock(&ctx->status_mutex);
			ctx->current_flags = printer_flag_busy;
			pthread_mutex_unlock(&ctx->status_mutex);

			break;
		}

		if (len == 0) {
			/* Empty interval (1s timeout) */
			continue;
		}

		/* Handle asynchronous heartbeat (0xDD) */
		if (received_cmd == NIIMBOT_CMD_HEARTBEAT_RESP) {
			if (len >= 3) {
				int new_flags = 0;
				if (packet_data[1] == 1) {
					new_flags |= printer_flag_open_cover;
				}

				if (packet_data[2] == 1) {
					new_flags |= printer_flag_no_paper;
				}

				if (packet_data[0] <= 1) {
					new_flags |= printer_flag_battery_low;
				}

				pthread_mutex_lock(&ctx->status_mutex);
				ctx->current_flags = new_flags;
				pthread_mutex_unlock(&ctx->status_mutex);
			}
		}
		/* Handle synchronous responses (routing to the main thread) */
		else {
			pthread_mutex_lock(&ctx->cmd_mutex);

			if (received_cmd == ctx->awaited_cmd ||
				received_cmd == (uint8_t)(ctx->awaited_cmd + 1) ||
				received_cmd == NIIMBOT_CMD_CONNECT_RESP) {
				ctx->cmd_resp_len = (len > (int)sizeof(ctx->cmd_resp_buf)) ? (int)sizeof(ctx->cmd_resp_buf) : len;
				memcpy(ctx->cmd_resp_buf, packet_data, ctx->cmd_resp_len);
				ctx->cmd_received = true;
				pthread_cond_signal(&ctx->cmd_cond);
			}

			pthread_mutex_unlock(&ctx->cmd_mutex);
		}
	}
	return NULL;
}

static int niimbot_wait_for_response(struct niimbot_ctx *ctx, uint8_t cmd, unsigned char *out_buf, int max_len, int timeout_sec)
{
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	ts.tv_sec += timeout_sec;

	pthread_mutex_lock(&ctx->cmd_mutex);

	ctx->awaited_cmd = cmd;
	ctx->cmd_received = false;

	while (!ctx->cmd_received) {
		int rc = pthread_cond_timedwait(&ctx->cmd_cond, &ctx->cmd_mutex, &ts);
		if (rc == ETIMEDOUT) {
			pthread_mutex_unlock(&ctx->cmd_mutex);
			return printer_status_timeout;
		}
	}

	int copied = (ctx->cmd_resp_len > max_len) ? max_len : ctx->cmd_resp_len;
	if (out_buf && copied > 0) {
		memcpy(out_buf, ctx->cmd_resp_buf, copied);
	}

	pthread_mutex_unlock(&ctx->cmd_mutex);

	return copied;
}

static uint32_t count_black_pixels(const unsigned char *row_data, int bytes_count)
{
	uint32_t count = 0;
	for (int i = 0; i < bytes_count; i++) {
		unsigned char b = row_data[i];
		while (b) {
			count += (b & 1);
			b >>= 1;
		}
	}
	return count;
}

static int niimbot_open(struct printer *p, const char *port_path)
{
	int fd = open(port_path, O_RDWR | O_NOCTTY);
	if (fd < 0) {
		_log_error("Cannot open RFCOMM port");
		return printer_status_error;
	}
	_log("Connected to port %s. Initializing protocol...", port_path);

	struct termios t;
	if (tcgetattr(fd, &t) == 0) {
		cfmakeraw(&t);
		if (tcsetattr(fd, TCSANOW, &t) < 0) {
			_log_error("Warning: Cannot set termios raw mode");
		}
	}

	struct niimbot_ctx *ctx = calloc(1, sizeof(*ctx));
	if (ctx == NULL) {
		close(fd);
		_log_error("Unable to allocate context data");
		return printer_status_error;
	}

	ctx->fd = fd;
	ctx->density_level = 2; /* 1: light, 2: medium, 3: dark */
	ctx->paper_type = printer_paper_gap;

	pthread_mutex_init(&ctx->status_mutex, NULL);
	pthread_mutex_init(&ctx->cmd_mutex, NULL);
	pthread_cond_init(&ctx->cmd_cond, NULL);

	p->private = ctx;

	ctx->keep_running = true;
	if (pthread_create(&ctx->read_thread, NULL, niimbot_read_thread_func, p) != 0) {
		_log_error("Cannot create background thread");
		close(fd);
		free(ctx);
		p->private = NULL;
		return printer_status_error;
	}

	/* Handshake (prepend): Send prefix 0x03 */
	if (niimbot_send_raw(fd, (const unsigned char *)"\x03", 1) < 0) {
		_log_error("Send failed");
		goto onError;
	}

	/* Handshake: send CONNECT (0x01) with client version 0x01 */
	uint8_t client_ver = 0x01;
	if (niimbot_send_packet(fd, NIIMBOT_CMD_CONNECT, &client_ver, 1) < 0) {
		_log_error("Failed while sending connect command");
		goto onError;
	}

	/* Safely wait until the background thread catches the authorization response (0x02) */
	unsigned char handshake_resp[16];
	if (niimbot_wait_for_response(ctx, NIIMBOT_CMD_CONNECT_RESP, handshake_resp, sizeof(handshake_resp), 2) < 0) {
		_log_error("Handshake failed (No response from printer).");
		goto onError;
	}

	_log("Handshake successful. Device authorized.");

	return printer_status_ok;

onError:
	ctx->keep_running = false;
	pthread_join(ctx->read_thread, NULL);

	close(fd);
	free(ctx);
	p->private = NULL;

	return printer_status_error;
}

static int niimbot_close(struct printer *p)
{
	struct niimbot_ctx *ctx = (struct niimbot_ctx *)p->private;

	if (ctx == NULL || ctx->fd < 0) {
		return printer_status_error;
	}

	ctx->keep_running = false;
	pthread_join(ctx->read_thread, NULL);

	pthread_mutex_destroy(&ctx->status_mutex);
	pthread_mutex_destroy(&ctx->cmd_mutex);
	pthread_cond_destroy(&ctx->cmd_cond);

	close(ctx->fd);
	ctx->fd = -1;

	free(p->private);
	p->private = NULL;

	return printer_status_ok;
}

static int niimbot_status(struct printer *p, int *p_status)
{
	struct niimbot_ctx *ctx = (struct niimbot_ctx *)p->private;
	if (ctx == NULL) {
		return printer_status_error;
	}

	pthread_mutex_lock(&ctx->status_mutex);
	int status = ctx->current_flags;
	pthread_mutex_unlock(&ctx->status_mutex);

	if (p_status) {
		*p_status = status;
	}

	if (status == 0x00) {
		_log("printer ready.");
	}

	if (status & printer_flag_busy) {
		_log("Printer is currently busy...");
	}
	if (status & printer_flag_open_cover) {
		_log_error("Printer cover is open!");
	}
	if (status & printer_flag_no_paper) {
		_log_error("Out of paper!");
	}
	if (status & printer_flag_battery_low) {
		_log_error("Low battery!");
	}

	if (p_status != NULL) {
		*p_status = status;
	}

	return (status == 0) ?
		printer_status_ok :
		status;
}

static int niimbot_set_property(struct printer *p, int key, int value)
{
	struct niimbot_ctx *ctx = (struct niimbot_ctx *)p->private;

	if (ctx == NULL) {
		return printer_status_error;
	}

	switch (key) {
		case printer_prop_density:
			/* Niimbot density values are typically 1, 2, 3 */
			if (value <= 0) {
				ctx->density_level = 1;
			}
			else if (value > 3) {
				ctx->density_level = 3;
			}
			else {
				ctx->density_level = value;
			}
			return printer_status_ok;

		case printer_prop_paper:
			ctx->paper_type = value;
			return printer_status_ok;

		default:
			break;
	}

	return printer_status_error;
}

static int niimbot_wakeup(struct printer *p)
{
	(void)p;
	/*
	 * Niimbot wakes up automatically upon receiving any valid packet
	 */

	return printer_status_ok;
}

static int niimbot_send_image(struct printer *p, const unsigned char *bitmap_data, int height_pels)
{
	unsigned char pkt[32];
	struct niimbot_ctx *ctx = (struct niimbot_ctx *)p->private;

	if (ctx == NULL) {
		return printer_status_error;
	}

	if (height_pels <= 0) {
		height_pels = FICHERO_DEFAULT_HEIGHT_PELS;
	}

	/*
	 * Set paper type (Command 0x23)
	 * 1: gap, 2: black mark, 3: continuous
	 */
	switch (ctx->paper_type) {
		case printer_paper_continuous:
			pkt[0] = 3;
			break;

		case printer_paper_black_mark:
			pkt[0] = 2;
			break;

		case printer_paper_gap:
		default:
			pkt[0] = 1;
			break;
	}

	if (niimbot_send_packet(ctx->fd, NIIMBOT_CMD_SET_LABEL_TYPE, pkt, 1) < 0) {
		return printer_status_error;
	}
	niimbot_wait_for_response(ctx, NIIMBOT_CMD_SET_LABEL_TYPE + 1, pkt, sizeof(pkt), 1);

	/* Set density (Command 0x21: values 1-3) */
	pkt[0] = (uint8_t)ctx->density_level;
	if (niimbot_send_packet(ctx->fd, NIIMBOT_CMD_SET_DENSITY, pkt, 1) < 0) {
		return printer_status_error;
	}
	niimbot_wait_for_response(ctx, NIIMBOT_CMD_SET_DENSITY + 1, pkt, sizeof(pkt), 1);

	/* START PRINT command (0x01). Payload: [0x00, 0x01] (1 page) */
	pkt[0] = 0x00;
	pkt[1] = 0x01;
	if (niimbot_send_packet(ctx->fd, NIIMBOT_CMD_START_PRINT, pkt, 2) < 0) {
		return printer_status_error;
	}
	niimbot_wait_for_response(ctx, NIIMBOT_CMD_CONNECT_RESP, pkt, sizeof(pkt), 2);

	/* START PAGE PRINT command (0x03). Payload: 0x01 */
	pkt[0] = 0x01;
	if (niimbot_send_packet(ctx->fd, NIIMBOT_CMD_START_PAGE_PRINT, pkt, 1) < 0) {
		return printer_status_error;
	}
	niimbot_wait_for_response(ctx, NIIMBOT_CMD_START_PAGE_PRINT + 1, pkt, sizeof(pkt), 2);

	/* SET DIMENSION command (0x05). Payload: [height_H] [height_L] */
	pkt[0] = (uint8_t)((height_pels >> 8) & 0xFF);
	pkt[1] = (uint8_t)(height_pels & 0xFF);
	if (niimbot_send_packet(ctx->fd, NIIMBOT_CMD_SET_DIMENSION, pkt, 2) < 0) {
		return printer_status_error;
	}
	niimbot_wait_for_response(ctx, NIIMBOT_CMD_SET_DIMENSION + 1, pkt, sizeof(pkt), 2);

	/* Transmitting bitmap rows */
	for (int y = 0; y < height_pels; y++) {
		const unsigned char *current_row_ptr = &bitmap_data[y * FICHERO_WIDTH_BYTES];

		bool is_row_empty = true;
		for (int b = 0; b < FICHERO_WIDTH_BYTES; b++) {
			if (current_row_ptr[b] != 0x00) {
				is_row_empty = false;
				break;
			}
		}

		if (is_row_empty) {
			/* Empty line (0x84). Payload: [y_H] [y_L] [repeat=1] -> 3 bytes */
			pkt[0] = (unsigned char)((y >> 8) & 0xFF);
			pkt[1] = (unsigned char)(y & 0xFF);
			pkt[2] = 1;

			if (niimbot_send_packet(ctx->fd, NIIMBOT_CMD_EMPTY_ROW, pkt, 3) < 0) {
				return printer_status_error;
			}
		}
		else {
			/* Data line (0x85). Payload: [y_H][y_L] [C1][C2][C3] [NN=1] [12B data] -> 18 bytes */
			uint32_t black_pixels = count_black_pixels(current_row_ptr, FICHERO_WIDTH_BYTES);
			pkt[0] = (unsigned char)((y >> 8) & 0xFF);
			pkt[1] = (unsigned char)(y & 0xFF);
			pkt[2] = (unsigned char)((black_pixels >> 16) & 0xFF);
			pkt[3] = (unsigned char)((black_pixels >> 8) & 0xFF);
			pkt[4] = (unsigned char)(black_pixels & 0xFF);
			pkt[5] = 1;

			memcpy(&pkt[6], current_row_ptr, FICHERO_WIDTH_BYTES);

			if (niimbot_send_packet(ctx->fd, NIIMBOT_CMD_BITMAP_ROW, pkt, 18) < 0) {
				return printer_status_error;
			}
		}
	}

	return printer_status_ok;
}

static int niimbot_formfeed(struct printer *p)
{
	(void)p;
	/*
	 * In the Niimbot protocol, positioning to the next label happens
	 * automatically during the END_PAGE_PRINT (0xF3) operation.
	 */
	return printer_status_ok;
}

static int niimbot_stop_wait(struct printer *p)
{
	unsigned char pkt[16];
	struct niimbot_ctx *ctx = (struct niimbot_ctx *)p->private;

	if (ctx == NULL) {
		return printer_status_error;
	}

	/* END PAGE PRINT (0xF3). Payload: 0x01 */
	pkt[0] = 0x01;
	if (niimbot_send_packet(ctx->fd, NIIMBOT_CMD_END_PAGE_PRINT, pkt, 1) < 0) {
		return printer_status_error;
	}
	niimbot_wait_for_response(ctx, NIIMBOT_CMD_END_PAGE_PRINT + 1, pkt, sizeof(pkt), 5);

	/* END PRINT (0xF5). Payload: 0x01 */
	pkt[0] = 0x01;
	if (niimbot_send_packet(ctx->fd, NIIMBOT_CMD_END_PRINT, pkt, 1) < 0) {
		return printer_status_error;
	}
	niimbot_wait_for_response(ctx, NIIMBOT_CMD_END_PRINT + 1, pkt, sizeof(pkt), 5);

	return printer_status_ok;
}

const struct printer_ops printer_ops_niimbot = {
	.open = niimbot_open,
	.close = niimbot_close,
	.status = niimbot_status,
	.set_property = niimbot_set_property,
	.wakeup = niimbot_wakeup,
	.send_image = niimbot_send_image,
	.formfeed = niimbot_formfeed,
	.stop_wait = niimbot_stop_wait,
};
