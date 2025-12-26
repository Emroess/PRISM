/*
 * stream_server.c - TCP server for streaming valve data over Ethernet
 */

#include <math.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stm32h7xx_hal.h"

#include "lwip/api.h"
#include "lwip/arch.h"
#include "lwip/ip.h"
#include "lwip/ip_addr.h"
#include "lwip/opt.h"
#include "lwip/tcp.h"

#include "board.h"
#include "config/network.h"
#include "drivers/uart.h"
#include "network/stream.h"
#include "valve_haptic.h"

/*
 * Stream client connection state
 */
struct stream_client {
	struct tcp_pcb *pcb;
	uint8_t connected;
	uint32_t stream_interval_ms;
	uint32_t last_stream_time;
	uint32_t messages_sent;
	err_t last_error;
};

/* Network defines moved to network_config.h */

/* Module state */
static struct stream_client stream_clients[MAX_STREAM_CLIENTS];
static uint8_t stream_active = 0;
static uint8_t stream_connected_clients = 0;
static uint32_t stream_total_messages = 0;
static uint32_t stream_send_errors = 0;
static uint32_t stream_invalid_samples = 0;
static uint32_t stream_last_send_tick = 0;
static uint32_t stream_default_interval_ms = ETHERNET_STREAM_DEFAULT_INTERVAL_MS;
static struct tcp_pcb *stream_listener_pcb = NULL;
static struct uart_handle *stream_uart = NULL;
static uint8_t stream_invalid_warned = 0;

/* Private function prototypes -----------------------------------------------*/
static err_t stream_accept(void *arg, struct tcp_pcb *newpcb, err_t err);
static err_t stream_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err);
static void stream_error(void *arg, err_t err);
static err_t stream_poll(void *arg, struct tcp_pcb *tpcb);
static void stream_disconnect(struct stream_client *client, struct tcp_pcb *tpcb, const char *reason);
static void stream_send_hello(struct stream_client *client);
static void stream_send_buffer(struct stream_client *client, const char *buffer, size_t len);
static struct uart_handle *stream_get_uart(void);
static void stream_log(const char *fmt, ...);
void ethernet_stream_process(void);
bool ethernet_stream_init(void);
void ethernet_stream_stop(void);

/* Private functions ---------------------------------------------------------*/

static struct uart_handle *
stream_get_uart(void)
{
  if (stream_uart == NULL) {
    stream_uart = uart_get_handle();
  }
  return stream_uart;
}

static int32_t
stream_scale_float(float value, float scale, bool *valid)
{
  if (valid != NULL) {
    *valid = false;
  }
  if (!isfinite(value)) {
    return 0;
  }

  float scaled = value * scale;
  if (scaled > 2147480000.0f) {
    scaled = 2147480000.0f;
  } else if (scaled < -2147480000.0f) {
    scaled = -2147480000.0f;
  }

  if (valid != NULL) {
    *valid = true;
  }
  return (int32_t)scaled;
}

static void u64_to_dec_str(uint64_t value, char *out, size_t out_size)
{
  if (out == NULL || out_size == 0U) {
    return;
  }

  char tmp[24];
  size_t idx = 0U;
  do {
    tmp[idx++] = (char)('0' + (value % 10U));
    value /= 10U;
  } while (value != 0U && idx < sizeof(tmp));

  if (idx >= out_size) {
    idx = out_size - 1U;
  }

  size_t write_idx = 0U;
  while (write_idx < idx) {
    out[write_idx] = tmp[idx - 1U - write_idx];
    write_idx++;
  }
  out[idx] = '\0';
}

static void
stream_log(const char *fmt, ...)
{
  struct uart_handle *uart = stream_get_uart();
  if (uart == NULL || fmt == NULL) {
    return;
  }

  char buffer[128];
  va_list args;
  va_start(args, fmt);
  int len = vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);

  if (len > 0 && (size_t)len < sizeof(buffer)) {
    uart_write(uart, (const uint8_t *)buffer, (size_t)len, 100);
  }
}

static void
stream_send_buffer(struct stream_client *client, const char *buffer, size_t len)
{
  if (client == NULL || buffer == NULL || len == 0U || client->pcb == NULL || !client->connected) {
    return;
  }

  err_t err = tcp_write(client->pcb, buffer, len, TCP_WRITE_FLAG_COPY);
  if (err == ERR_OK) {
    err = tcp_output(client->pcb);
    if (err == ERR_OK) {
      client->last_error = ERR_OK;
      client->messages_sent++;
      stream_total_messages++;
      stream_last_send_tick = board_get_systick_ms();
      return;
    }
  }

  stream_send_errors++;
  if (client->last_error != err) {
    client->last_error = err;
    stream_log("\r\n[ETH_STREAM] slot %ld send error %d\r\n", (long)(client - stream_clients), (int)err);
  }
}

static void
stream_send_hello(struct stream_client *client)
{
  if (client == NULL) {
    return;
  }

  char msg[120];
  int len = snprintf(msg, sizeof(msg),
                     "{\"stream\":\"valve\",\"message\":\"connected\",\"interval_ms\":%lu}\n",
                     (unsigned long)client->stream_interval_ms);

  /* Safety-critical error handling */
  if (len < 0) {
    stream_log("\r\n[ETH_STREAM] hello message snprintf error\r\n");
    return;
  }
  
  if ((size_t)len >= sizeof(msg)) {
    stream_log("\r\n[ETH_STREAM] hello message buffer overflow\r\n");
    return;
  }

  /* Verify null termination */
  if (msg[len] != '\0') {
    stream_log("\r\n[ETH_STREAM] hello message not null-terminated\r\n");
    return;
  }

  /* Basic structure validation */
  if (len < 5 || msg[0] != '{' || strncmp(msg, "{\"stream\"", 9) != 0) {
    stream_log("\r\n[ETH_STREAM] hello message structure invalid\r\n");
    return;
  }

  if (len > 0 && (size_t)len < sizeof(msg)) {
    stream_send_buffer(client, msg, (size_t)len);
  }
}

static void
stream_disconnect(struct stream_client *client, struct tcp_pcb *tpcb, const char *reason)
{
  if (client == NULL) {
    return;
  }

  struct tcp_pcb *pcb = (tpcb != NULL) ? tpcb : client->pcb;

  if (pcb != NULL) {
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_err(pcb, NULL);
    tcp_poll(pcb, NULL, 0);
    err_t err = tcp_close(pcb);
    if (err != ERR_OK) {
      tcp_abort(pcb);
    }
  }

  client->pcb = NULL;
  if (client->connected && stream_connected_clients > 0U) {
    stream_connected_clients--;
  }
  client->connected = 0U;
  client->messages_sent = 0U;
  client->last_error = ERR_OK;
  client->stream_interval_ms = stream_default_interval_ms;
	client->last_stream_time = board_get_systick_ms();

	if (reason != NULL) {
		stream_log("\r\n[ETH_STREAM] client %ld disconnected (%s)\r\n",
		    (long)(client - stream_clients), reason);
	}
}

/*
 * Transmits real-time valve telemetry to a connected client as JSON.
 * The streaming interface allows external tools (oscilloscopes, loggers)
 * to monitor valve behavior without polling the REST API, which would
 * add latency and load to the control system.
 */
static void
stream_send_data(struct stream_client *client)
{
	struct valve_context *ctx;
	char json_buffer[384];
	uint32_t timestamp_ms;
	int32_t pos_turns_milli = 0;
	int32_t pos_deg_tenths = 0;
	int32_t torque_milli_nm = 0;
	int32_t filtered_torque_milli_nm = 0;
	int32_t vel_rad_milli = 0;
	int32_t passivity_energy_milli_j = 0;
	/* New timing fields from control loop */
	uint64_t t_us = 0ULL;
	uint32_t loop_time_us = 0U;
	uint32_t seq = 0U;
	uint32_t last_error_code = 0U;
	uint32_t heartbeat_age_ms = 0U;
	char t_us_str[24];
	const char *status_str = "NO_DATA";
	bool pos_turns_valid = false;
	bool pos_deg_valid = false;
	bool torque_valid = false;
	bool filtered_torque_valid = false;
	bool vel_valid = false;
	bool passivity_valid = false;
	bool data_valid = false;
	bool quiet_active = false;

	timestamp_ms = board_get_systick_ms();
	ctx = valve_haptic_get_context();
  if (ctx != NULL) {
    struct valve_state *state = valve_haptic_get_state(ctx);
    if (state != NULL) {
      pos_turns_milli = stream_scale_float(state->raw_position_turns, 1000.0f, &pos_turns_valid);
      pos_deg_tenths = stream_scale_float(state->position_deg, 10.0f, &pos_deg_valid);
      torque_milli_nm = stream_scale_float(state->torque_nm, 1000.0f, &torque_valid);
      filtered_torque_milli_nm = stream_scale_float(state->filtered_torque_nm, 1000.0f, &filtered_torque_valid);
      vel_rad_milli = stream_scale_float(state->omega_rad_s, 1000.0f, &vel_valid);
      passivity_energy_milli_j = stream_scale_float(state->passivity_energy_j, 1000.0f, &passivity_valid);
      
      /* Pull timing directly from diagnostics */
      t_us = state->diag.t_us_accum;
      loop_time_us = state->diag.last_loop_time_us;
      seq = state->diag.sample_seq;
      last_error_code = state->diag.safety.last_error_code;
      heartbeat_age_ms = state->diag.heartbeat_age_ms;
      quiet_active = (state->quiet_active != 0);
      
      u64_to_dec_str(t_us, t_us_str, sizeof(t_us_str));
      status_str = (state->status == VALVE_STATE_RUNNING) ? "RUNNING" :
                   (state->status == VALVE_STATE_IDLE) ? "IDLE" : "ERROR";
      data_valid = pos_turns_valid && pos_deg_valid && torque_valid && vel_valid;
    }
  }

  if (!data_valid) {
    stream_invalid_samples++;
    if (!stream_invalid_warned) {
      stream_invalid_warned = 1;
      stream_log("\r\n[ETH_STREAM] warning: invalid sample (NaN or overflow)\r\n");
    }
  }

  int len = snprintf(json_buffer, sizeof(json_buffer),
    "{\"timestamp_ms\":%lu,\"t_us\":%s,\"loop_time_us\":%lu,\"seq\":%lu,"
    "\"position_turns\":%ld.%03ld,\"position_deg\":%ld.%01ld,"
    "\"torque_nm\":%ld.%03ld,\"filt_torque_nm\":%ld.%03ld,"
    "\"status\":\"%s\",\"omega_rad_s\":%ld.%03ld,"
    "\"passivity_mj\":%ld,\"quiet\":%s,\"err\":%lu,\"hb_age\":%lu,"
    "\"data_valid\":%s}\n",
    (unsigned long)timestamp_ms,
    t_us_str,
    (unsigned long)loop_time_us,
    (unsigned long)seq,
    (long)(pos_turns_milli / 1000),
    (long)labs(pos_turns_milli % 1000),
    (long)(pos_deg_tenths / 10),
    (long)labs(pos_deg_tenths % 10),
    (long)(torque_milli_nm / 1000),
    (long)labs(torque_milli_nm % 1000),
    (long)(filtered_torque_milli_nm / 1000),
    (long)labs(filtered_torque_milli_nm % 1000),
    status_str,
    (long)(vel_rad_milli / 1000),
    (long)labs(vel_rad_milli % 1000),
    (long)passivity_energy_milli_j,
    quiet_active ? "true" : "false",
    (unsigned long)last_error_code,
    (unsigned long)heartbeat_age_ms,
    data_valid ? "true" : "false");

  /* Safety-critical error handling */
  if (len < 0) {
    /* snprintf error - format string or encoding issue */
    stream_send_errors++;
    stream_log("\r\n[ETH_STREAM] JSON snprintf error\r\n");
    return;
  }
  
  if ((size_t)len >= sizeof(json_buffer)) {
    /* Buffer overflow - data truncated */
    stream_send_errors++;
    stream_log("\r\n[ETH_STREAM] JSON buffer overflow\r\n");
    return;
  }

  /* Verify null termination for safety */
  if (json_buffer[len] != '\0') {
    stream_send_errors++;
    stream_log("\r\n[ETH_STREAM] JSON not null-terminated\r\n");
    return;
  }

  /* Additional validation: ensure JSON structure integrity */
  if (len < 10 || json_buffer[0] != '{' || json_buffer[len-1] != '\n') {
    stream_send_errors++;
	stream_log("\r\n[ETH_STREAM] JSON structure invalid\r\n");
	return;
  }

  stream_send_buffer(client, json_buffer, (size_t)len);
}

/*
 * Called from the main loop to push telemetry to all connected clients.
 * Separated from the timer ISR to avoid blocking the control loop -
 * TCP transmission can take variable time depending on network conditions.
 */
void
ethernet_stream_process(void)
{
	struct stream_client *client;
	uint32_t current_time;
	uint32_t i;

	if (!stream_active)
		return;

	current_time = board_get_systick_ms();

	for (i = 0; i < MAX_STREAM_CLIENTS; i++) {
		client = &stream_clients[i];
		if (client->connected && client->pcb != NULL) {
			if ((current_time - client->last_stream_time) >=
			    client->stream_interval_ms) {
				stream_send_data(client);
				client->last_stream_time = current_time;
			}
		} else if (client->connected && client->pcb == NULL) {
			stream_disconnect(client, NULL, "connection lost");
		}
	}
}

/*
 * Handles new client connections to the streaming server.
 * Limits concurrent connections to MAX_STREAM_CLIENTS to prevent
 * memory exhaustion and ensure fair bandwidth allocation.
 */
static err_t
stream_accept(void *arg, struct tcp_pcb *newpcb, err_t err)
{
	uint32_t i;

	(void)arg;
	if (err != ERR_OK) {
		stream_log("\r\n[ETH_STREAM] accept error %d\r\n", err);
		return err;
	}

	/* Find free client slot */
	for (i = 0; i < MAX_STREAM_CLIENTS; i++) {
		if (!stream_clients[i].connected) {
			stream_clients[i].pcb = newpcb;
			stream_clients[i].connected = 1;
      stream_clients[i].stream_interval_ms = stream_default_interval_ms;
      stream_clients[i].last_stream_time = board_get_systick_ms();
      stream_clients[i].messages_sent = 0;
      stream_clients[i].last_error = ERR_OK;

      tcp_arg(newpcb, &stream_clients[i]);
      tcp_recv(newpcb, stream_recv);
      tcp_err(newpcb, stream_error);
      tcp_poll(newpcb, stream_poll, 2); /* Poll every 500ms */

			tcp_accepted(newpcb);
			stream_connected_clients++;

			char addr_str[40];
			ipaddr_ntoa_r(&newpcb->remote_ip, addr_str,
			    sizeof(addr_str));
			stream_log(
			    "\r\n[ETH_STREAM] client %lu connected from %s:%u\r\n",
			    (unsigned long)i, addr_str, newpcb->remote_port);
			stream_send_hello(&stream_clients[i]);
			return ERR_OK;
		}
	}

	/* No free slots */
	tcp_close(newpcb);
	stream_log("\r\n[ETH_STREAM] no free client slots\r\n");
	return ERR_OK;
}

/*
 * Processes commands from streaming clients (interval changes, sync requests).
 * The sync command enables precise time correlation between the embedded
 * controller's microsecond timestamps and the host's wall-clock time.
 */
static err_t
stream_recv(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
	struct stream_client *client = (struct stream_client *)arg;

	(void)err;
	if (p == NULL) {
		stream_disconnect(client, tpcb, "remote closed");
		return ERR_OK;
	}

	/* Process received data (commands) */
	if (p->len > 0) {
		char *data = (char *)p->payload;

		/* Simple command parsing */
		if (strncmp(data, "interval ", 9) == 0) {
			int interval = atoi(data + 9);
			if (interval >= 10 && interval <= 1000) {
				client->stream_interval_ms = interval;
			}
		} else if (strncmp(data, "sync", 4) == 0) {
			/* Optional token after space */
			const char *token = NULL;
			if (p->len > 5) {
				/* Skip command and space */
				token = data + 5;
			}

			uint64_t t_us = 0ULL;
			uint32_t seq = 0U;
			char t_us_str[24];
			struct valve_context *ctx = valve_haptic_get_context();
			if (ctx != NULL) {
				struct valve_state *state;
				state = valve_haptic_get_state(ctx);
				if (state != NULL) {
					t_us = state->diag.t_us_accum;
					seq = state->diag.sample_seq;
				}
			}
			u64_to_dec_str(t_us, t_us_str, sizeof(t_us_str));

			char resp[160];
			int len = 0;
			if (token != NULL && *token != '\0' &&
			    *token != '\n' && *token != '\r') {
				/* Trim trailing newlines */
				size_t tok_len = strcspn(token, "\r\n");
        char tokbuf[48];
        if (tok_len >= sizeof(tokbuf)) tok_len = sizeof(tokbuf) - 1;
        memcpy(tokbuf, token, tok_len);
        tokbuf[tok_len] = '\0';
        len = snprintf(resp, sizeof(resp),
                       "{\"sync\":\"ok\",\"t_us\":%s,\"seq\":%lu,\"timestamp_ms\":%lu,\"token\":\"%s\"}\n",
                       t_us_str,
                       (unsigned long)seq,
                       (unsigned long)board_get_systick_ms(),
                       tokbuf);
      } else {
        len = snprintf(resp, sizeof(resp),
                       "{\"sync\":\"ok\",\"t_us\":%s,\"seq\":%lu,\"timestamp_ms\":%lu}\n",
                       t_us_str,
                       (unsigned long)seq,
                       (unsigned long)board_get_systick_ms());
      }

      /* Safety-critical error handling */
      if (len < 0) {
        stream_log("\r\n[ETH_STREAM] sync response snprintf error\r\n");
        return ERR_OK;
      }
      
      if ((size_t)len >= sizeof(resp)) {
        stream_log("\r\n[ETH_STREAM] sync response buffer overflow\r\n");
        return ERR_OK;
      }

      /* Verify null termination */
      if (resp[len] != '\0') {
        stream_log("\r\n[ETH_STREAM] sync response not null-terminated\r\n");
			return ERR_OK;
		}

		/* Basic structure validation */
		if (len < 5 || resp[0] != '{' ||
		    strncmp(resp, "{\"sync\"", 7) != 0) {
			stream_log(
			    "\r\n[ETH_STREAM] sync response structure invalid\r\n");
			return ERR_OK;
		}

		if (len > 0 && (size_t)len < sizeof(resp))
			stream_send_buffer(client, resp, (size_t)len);
		}
	}

	tcp_recved(tpcb, p->tot_len);
	pbuf_free(p);
	return ERR_OK;
}

/*
 * Cleans up client state when lwIP detects a connection failure.
 * Without this, stale client slots would never be reclaimed.
 */
static void
stream_error(void *arg, err_t err)
{
	struct stream_client *client = (struct stream_client *)arg;

	(void)err;
	stream_disconnect(client, NULL, "lwIP error");
}

/*
 * Periodic callback from lwIP for connection health checks.
 * Currently unused but required by the lwIP callback interface.
 */
static err_t
stream_poll(void *arg, struct tcp_pcb *tpcb)
{
	(void)arg;
	(void)tpcb;
	return ERR_OK;
}

/*
 * Creates the TCP listener for real-time telemetry streaming.
 * Must be called after lwIP initialization but before the main loop
 * begins, so clients can connect as soon as the network is up.
 */
bool
ethernet_stream_init(void)
{
	struct tcp_pcb *pcb;

	if (stream_active) {
		stream_log("\r\n[ETH_STREAM] server already running\r\n");
		return true;
	}

	stream_total_messages = 0;
	stream_send_errors = 0;
	stream_invalid_samples = 0;
	stream_last_send_tick = 0;
	stream_connected_clients = 0;
	stream_listener_pcb = NULL;
	stream_invalid_warned = 0;

	/* Initialize client array */
	memset(stream_clients, 0, sizeof(stream_clients));

	/* Create TCP PCB */
	pcb = tcp_new();
	if (pcb == NULL) {
		stream_log("\r\n[ETH_STREAM] failed to create PCB\r\n");
		return false;
	}

	ip_set_option(pcb, SOF_REUSEADDR);

	/* Bind to port */
	if (tcp_bind(pcb, IP_ADDR_ANY, STREAM_PORT) != ERR_OK) {
		tcp_close(pcb);
		stream_log("\r\n[ETH_STREAM] tcp_bind failed\r\n");
		return false;
	}

	/* Listen for connections */
	pcb = tcp_listen(pcb);
	if (pcb == NULL) {
		stream_log("\r\n[ETH_STREAM] tcp_listen failed\r\n");
		return false;
	}

	/* Set accept callback */
	tcp_accept(pcb, stream_accept);
	stream_listener_pcb = pcb;

	stream_active = 1;
	stream_log("\r\n[ETH_STREAM] server started on port %d (interval %lu ms)\r\n",
	    STREAM_PORT, (unsigned long)stream_default_interval_ms);
	return true;
}

/*
 * Gracefully shuts down all streaming connections before system reset.
 * Ensures clients receive proper TCP FIN rather than RST, allowing
 * them to distinguish intentional shutdown from network failure.
 */
void
ethernet_stream_stop(void)
{
	uint32_t i;

	/* Close all client connections */
	for (i = 0; i < MAX_STREAM_CLIENTS; i++) {
		if (stream_clients[i].connected) {
			stream_disconnect(&stream_clients[i],
			    stream_clients[i].pcb, "server stop");
		}
	}

  if (stream_listener_pcb != NULL) {
    err_t err = tcp_close(stream_listener_pcb);
    if (err != ERR_OK) {
      tcp_abort(stream_listener_pcb);
    }
    stream_listener_pcb = NULL;
  }

  stream_active = 0;
  stream_log("\r\n[ETH_STREAM] server stopped\r\n");
  stream_invalid_warned = 0;
}

uint32_t ethernet_stream_set_default_interval(uint32_t interval_ms)
{
  if (interval_ms < ETHERNET_STREAM_MIN_INTERVAL_MS) {
    interval_ms = ETHERNET_STREAM_MIN_INTERVAL_MS;
  } else if (interval_ms > ETHERNET_STREAM_MAX_INTERVAL_MS) {
    interval_ms = ETHERNET_STREAM_MAX_INTERVAL_MS;
  }

  stream_default_interval_ms = interval_ms;
  return stream_default_interval_ms;
}

void ethernet_stream_get_stats(ethernet_stream_stats_t *stats)
{
  if (stats == NULL) {
    return;
  }

  stats->active = stream_active;
  stats->connected_clients = stream_connected_clients;
  stats->default_interval_ms = stream_default_interval_ms;
  stats->messages_sent = stream_total_messages;
  stats->send_errors = stream_send_errors;
  stats->invalid_samples = stream_invalid_samples;
  stats->last_send_tick_ms = stream_last_send_tick;
}
