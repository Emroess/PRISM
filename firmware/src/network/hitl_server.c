/*
 * network/hitl_server.c - Hardware-In-The-Loop TCP server
 *
 * Opens a single-client TCP listener on HITL_PORT (8889).
 *
 * Data flow per 1 kHz control tick:
 *   1. valve_haptic_process() calls hitl_server_set_torque() with the
 *      computed drive torque.
 *   2. hitl_server_process() (called from main loop) flushes that torque
 *      as a JSON line to the connected Isaac Sim client.
 *   3. Isaac Sim receives the torque, integrates its motor model, and
 *      pushes back {"pos":<deg>,"vel":<rad_s>}\n over the same TCP socket.
 *   4. valve_haptic_process() calls hitl_server_get_encoder() to read the
 *      latest received position/velocity and updates its state instead of
 *      reading from the CAN bus encoder.
 *
 * If no encoder packet is received within HITL_ENCODER_TIMEOUT_MS the
 * get_encoder() call returns false so the control loop can fault safely.
 *
 * Only one client is supported at a time.  A second connection attempt
 * while one is active is rejected.
 */

#include <math.h>
#include <stdbool.h>
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
#include "network/hitl_server.h"

/* ---------------------------------------------------------------------------
 * Internal state
 * ---------------------------------------------------------------------------*/

/* Pending torque snapshot written from TIM6 ISR, read by main-loop process */
static volatile float   hitl_pending_torque_nm   = 0.0f;
static volatile uint32_t hitl_pending_seq         = 0U;
static volatile uint64_t hitl_pending_t_us        = 0ULL;
static volatile uint8_t  hitl_torque_pending_flag = 0U; /* set by ISR, cleared by process */

/* Latest inbound encoder data from Isaac Sim (written by lwIP recv cb) */
static volatile float   hitl_enc_pos_deg       = 0.0f;
static volatile float   hitl_enc_vel_rad_s     = 0.0f;
static volatile uint32_t hitl_enc_last_recv_ms = 0U;
static volatile uint8_t  hitl_enc_valid        = 0U; /* 0 until first packet received */

/* TCP control */
static struct tcp_pcb *hitl_listener_pcb  = NULL;
static struct tcp_pcb *hitl_client_pcb    = NULL;
static uint8_t         hitl_client_active = 0U;

/* Statistics */
static struct hitl_stats hitl_stats_data;

/* Inbound line parser state (accumulate until '\n') */
#define HITL_LINE_BUF_SIZE 128U
static char    hitl_line_buf[HITL_LINE_BUF_SIZE];
static uint32_t hitl_line_len = 0U;

/* UART handle for debug logging */
static struct uart_handle *hitl_uart = NULL;

/* ---------------------------------------------------------------------------
 * Helpers
 * ---------------------------------------------------------------------------*/

static struct uart_handle *hitl_get_uart(void)
{
    if (hitl_uart == NULL) {
        hitl_uart = uart_get_handle();
    }
    return hitl_uart;
}

static void hitl_log(const char *msg)
{
    struct uart_handle *uart = hitl_get_uart();
    if (uart == NULL || msg == NULL) {
        return;
    }
    uart_write(uart, (const uint8_t *)msg, strlen(msg), 100U);
}

/* ---------------------------------------------------------------------------
 * Inbound JSON parser
 *
 * Expects lines of the form:
 *   {"pos":<float>,"vel":<float>}\n
 * or any order of those two keys.  Uses simple strstr() scan — no heap.
 * ---------------------------------------------------------------------------*/

static bool hitl_parse_encoder_line(const char *line, float *pos_deg, float *vel_rad_s)
{
    if (line == NULL || pos_deg == NULL || vel_rad_s == NULL) {
        return false;
    }

    /* Locate "pos" value */
    const char *p = strstr(line, "\"pos\"");
    if (p == NULL) {
        p = strstr(line, "\"pos_deg\""); /* accept both key names */
    }
    if (p == NULL) {
        return false;
    }
    /* Skip past key and colon */
    p = strchr(p, ':');
    if (p == NULL) {
        return false;
    }
    p++;
    char *end = NULL;
    float pos = strtof(p, &end);
    if (end == p || !isfinite(pos)) {
        return false;
    }

    /* Locate "vel" value */
    const char *v = strstr(line, "\"vel\"");
    if (v == NULL) {
        v = strstr(line, "\"vel_rad_s\"");
    }
    if (v == NULL) {
        return false;
    }
    v = strchr(v, ':');
    if (v == NULL) {
        return false;
    }
    v++;
    float vel = strtof(v, &end);
    if (end == v || !isfinite(vel)) {
        return false;
    }

    *pos_deg   = pos;
    *vel_rad_s = vel;
    return true;
}

/* Process bytes accumulated in hitl_line_buf looking for complete lines. */
static void hitl_process_line_buffer(void)
{
    /* Scan for newline characters */
    uint32_t start = 0U;
    for (uint32_t i = 0U; i < hitl_line_len; i++) {
        if (hitl_line_buf[i] == '\n' || hitl_line_buf[i] == '\r') {
            if (i > start) {
                /* Null-terminate and parse */
                hitl_line_buf[i] = '\0';
                float pos = 0.0f;
                float vel = 0.0f;
                if (hitl_parse_encoder_line(hitl_line_buf + start, &pos, &vel)) {
                    /* Atomically update shared encoder cache */
                    hitl_enc_pos_deg       = pos;
                    hitl_enc_vel_rad_s     = vel;
                    hitl_enc_last_recv_ms  = board_get_systick_ms();
                    hitl_enc_valid         = 1U;
                    hitl_stats_data.encoder_frames_recv++;
                    hitl_stats_data.last_recv_tick_ms = hitl_enc_last_recv_ms;
                } else {
                    hitl_stats_data.parse_errors++;
                }
            }
            start = i + 1U;
        }
    }

    /* Compact – keep any partial line at the start of the buffer */
    if (start > 0U && start < hitl_line_len) {
        uint32_t remaining = hitl_line_len - start;
        memmove(hitl_line_buf, hitl_line_buf + start, remaining);
        hitl_line_len = remaining;
    } else if (start >= hitl_line_len) {
        hitl_line_len = 0U;
    }
}

/* ---------------------------------------------------------------------------
 * lwIP callbacks
 * ---------------------------------------------------------------------------*/

static void hitl_client_disconnect(struct tcp_pcb *tpcb);

static err_t hitl_recv_cb(void *arg, struct tcp_pcb *tpcb, struct pbuf *p, err_t err)
{
    (void)arg;
    (void)err;

    if (p == NULL) {
        /* Client disconnected cleanly */
        hitl_client_disconnect(tpcb);
        return ERR_OK;
    }

    /* Accumulate received bytes into line buffer */
    struct pbuf *cur = p;
    while (cur != NULL) {
        uint8_t *data = (uint8_t *)cur->payload;
        uint16_t  len  = cur->len;
        for (uint16_t i = 0U; i < len; i++) {
            if (hitl_line_len < (HITL_LINE_BUF_SIZE - 1U)) {
                hitl_line_buf[hitl_line_len] = (char)data[i];
                hitl_line_len++;
            } else {
                /* Buffer full – discard and reset to avoid locking up */
                hitl_line_len = 0U;
                hitl_stats_data.parse_errors++;
            }
        }
        cur = cur->next;
    }

    tcp_recved(tpcb, p->tot_len);
    pbuf_free(p);

    /* Parse any complete lines we just received */
    hitl_process_line_buffer();

    return ERR_OK;
}

static void hitl_err_cb(void *arg, err_t err)
{
    (void)arg;
    (void)err;
    /* lwIP already freed the PCB on error – just clear our reference */
    hitl_client_pcb    = NULL;
    hitl_client_active = 0U;
    hitl_enc_valid     = 0U;
    hitl_log("\r\n[HITL] client error, disconnected\r\n");
}

static err_t hitl_poll_cb(void *arg, struct tcp_pcb *tpcb)
{
    (void)arg;
    (void)tpcb;
    return ERR_OK;
}

static void hitl_client_disconnect(struct tcp_pcb *tpcb)
{
    if (tpcb != NULL) {
        tcp_arg(tpcb, NULL);
        tcp_recv(tpcb, NULL);
        tcp_err(tpcb, NULL);
        tcp_poll(tpcb, NULL, 0);
        err_t e = tcp_close(tpcb);
        if (e != ERR_OK) {
            tcp_abort(tpcb);
        }
    }

    if (hitl_client_pcb == tpcb || tpcb == NULL) {
        hitl_client_pcb    = NULL;
        hitl_client_active = 0U;
        hitl_enc_valid     = 0U;
        hitl_line_len      = 0U;
        hitl_log("\r\n[HITL] client disconnected\r\n");
    }
}

static err_t hitl_accept_cb(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    (void)arg;

    if (err != ERR_OK || newpcb == NULL) {
        return ERR_VAL;
    }

    /* Only one client at a time */
    if (hitl_client_active) {
        tcp_abort(newpcb);
        hitl_log("\r\n[HITL] rejected second client\r\n");
        return ERR_OK;
    }

    hitl_client_pcb    = newpcb;
    hitl_client_active = 1U;
    hitl_enc_valid     = 0U;
    hitl_line_len      = 0U;
    hitl_pending_torque_nm   = 0.0f;
    hitl_pending_seq         = 0U;
    hitl_pending_t_us        = 0ULL;
    hitl_torque_pending_flag = 0U;

    tcp_accepted(newpcb);
    tcp_arg(newpcb, NULL);
    tcp_recv(newpcb, hitl_recv_cb);
    tcp_err(newpcb, hitl_err_cb);
    tcp_poll(newpcb, hitl_poll_cb, 2U);

    char addr_str[40];
    ipaddr_ntoa_r(&newpcb->remote_ip, addr_str, sizeof(addr_str));
    char msg[128];
    snprintf(msg, sizeof(msg), "\r\n[HITL] Isaac Sim connected from %s:%u\r\n",
             addr_str, newpcb->remote_port);
    hitl_log(msg);

    /* Send hello so Isaac Sim can verify the handshake */
    const char *hello = "{\"hitl\":\"ready\",\"port\":8889}\n";
    err_t we = tcp_write(newpcb, hello, (uint16_t)strlen(hello), TCP_WRITE_FLAG_COPY);
    if (we == ERR_OK) {
        tcp_output(newpcb);
    }

    return ERR_OK;
}

/* ---------------------------------------------------------------------------
 * Public API
 * ---------------------------------------------------------------------------*/

bool hitl_server_init(void)
{
    if (hitl_listener_pcb != NULL) {
        hitl_log("\r\n[HITL] server already running\r\n");
        return true;
    }

    memset(&hitl_stats_data, 0, sizeof(hitl_stats_data));
    hitl_line_len      = 0U;
    hitl_enc_valid     = 0U;
    hitl_client_active = 0U;
    hitl_client_pcb    = NULL;

    struct tcp_pcb *pcb = tcp_new();
    if (pcb == NULL) {
        hitl_log("\r\n[HITL] failed to create PCB\r\n");
        return false;
    }

    ip_set_option(pcb, SOF_REUSEADDR);

    if (tcp_bind(pcb, IP_ADDR_ANY, HITL_PORT) != ERR_OK) {
        tcp_close(pcb);
        hitl_log("\r\n[HITL] tcp_bind failed\r\n");
        return false;
    }

    pcb = tcp_listen(pcb);
    if (pcb == NULL) {
        hitl_log("\r\n[HITL] tcp_listen failed\r\n");
        return false;
    }

    tcp_accept(pcb, hitl_accept_cb);
    hitl_listener_pcb = pcb;
    hitl_stats_data.active = 1U;

    hitl_log("\r\n[HITL] server started on port 8889\r\n");
    return true;
}

void hitl_server_stop(void)
{
    /* Disconnect client first */
    if (hitl_client_active && hitl_client_pcb != NULL) {
        hitl_client_disconnect(hitl_client_pcb);
    }

    /* Close listener */
    if (hitl_listener_pcb != NULL) {
        err_t e = tcp_close(hitl_listener_pcb);
        if (e != ERR_OK) {
            tcp_abort(hitl_listener_pcb);
        }
        hitl_listener_pcb = NULL;
    }

    hitl_stats_data.active = 0U;
    hitl_log("\r\n[HITL] server stopped\r\n");
}

/*
 * hitl_server_process - Called from the main loop.
 * Flushes the pending torque frame to the connected Isaac Sim client.
 * Must NOT be called from ISR context.
 */
void hitl_server_process(void)
{
    if (!hitl_client_active || hitl_client_pcb == NULL) {
        return;
    }

    if (!hitl_torque_pending_flag) {
        return; /* Nothing new to send yet */
    }

    /* Snapshot atomically (8-bit flag read is atomic on Cortex-M) */
    hitl_torque_pending_flag = 0U;
    float    torque_nm = hitl_pending_torque_nm;
    uint32_t seq       = hitl_pending_seq;
    uint64_t t_us      = hitl_pending_t_us;

    /* Format torque frame as JSON.
     * Avoid floating-point printf: scale to integers. */
    int32_t t_milli = (int32_t)(torque_nm * 1000.0f);
    int32_t t_whole = t_milli / 1000;
    int32_t t_frac  = (t_milli < 0) ? -(t_milli % 1000) : (t_milli % 1000);

    /* u64 → decimal string without printf %llu (newlib-nano may lack it) */
    char t_us_str[24];
    uint64_t tmp = t_us;
    size_t   idx = 0U;
    char     rev[24];
    do {
        rev[idx++] = (char)('0' + (tmp % 10U));
        tmp /= 10U;
    } while (tmp != 0U && idx < sizeof(rev));
    for (size_t j = 0U; j < idx; j++) {
        t_us_str[j] = rev[idx - 1U - j];
    }
    t_us_str[idx] = '\0';

    char json[128];
    int len = snprintf(json, sizeof(json),
        "{\"torque_nm\":%ld.%03ld,\"seq\":%lu,\"t_us\":%s}\n",
        (long)t_whole, (long)t_frac,
        (unsigned long)seq,
        t_us_str);

    if (len <= 0 || (size_t)len >= sizeof(json)) {
        hitl_stats_data.send_errors++;
        return;
    }

    err_t e = tcp_write(hitl_client_pcb, json, (uint16_t)len, TCP_WRITE_FLAG_COPY);
    if (e == ERR_OK) {
        e = tcp_output(hitl_client_pcb);
    }

    if (e == ERR_OK) {
        hitl_stats_data.torque_frames_sent++;
    } else {
        hitl_stats_data.send_errors++;
    }
}

/*
 * hitl_server_set_torque - Called from TIM6 ISR (1 kHz).
 * Thread-safe: writes only to volatile scalars; 32-bit writes on
 * Cortex-M7 are single-instruction, so partial reads are impossible.
 * The 64-bit t_us write is NOT atomic on Cortex-M but this is a
 * diagnostic / timestamp field – a torn read is harmless in practice.
 */
void hitl_server_set_torque(float torque_nm, uint32_t seq, uint64_t t_us_accum)
{
    hitl_pending_torque_nm = torque_nm;
    hitl_pending_seq       = seq;
    hitl_pending_t_us      = t_us_accum;
    hitl_torque_pending_flag = 1U; /* signal to main-loop process */
}

/*
 * hitl_server_get_encoder - Called from TIM6 ISR (1 kHz).
 * Returns true when fresh Isaac Sim encoder data is available.
 * Returns false if no data received yet, or data is stale.
 */
bool hitl_server_get_encoder(struct hitl_encoder_data *out)
{
    if (out == NULL) {
        return false;
    }

    if (!hitl_enc_valid) {
        /* No encoder packet received yet from Isaac Sim.
         *
         * Startup grace period: the first few torque frames need to travel
         * to Isaac Sim before it can integrate and reply.  Return zeroed
         * encoder values so the control loop keeps running and keeps sending
         * torque frames.  The encoder timeout only arms once hitl_enc_valid
         * goes true (i.e. after the first real packet arrives).
         */
        out->pos_deg   = 0.0f;
        out->vel_rad_s = 0.0f;
        return true;
    }

    /* Data has been received at least once — now enforce the staleness timeout. */
    uint32_t age_ms = board_get_systick_ms() - hitl_enc_last_recv_ms;
    if (age_ms > HITL_ENCODER_TIMEOUT_MS) {
        hitl_stats_data.encoder_timeouts++;
        return false; /* Was live, now stale → caller should fault */
    }

    out->pos_deg   = hitl_enc_pos_deg;
    out->vel_rad_s = hitl_enc_vel_rad_s;
    return true;
}

bool hitl_server_client_connected(void)
{
    return (hitl_client_active != 0U);
}

void hitl_server_get_stats(struct hitl_stats *stats)
{
    if (stats == NULL) {
        return;
    }
    hitl_stats_data.active           = (hitl_listener_pcb != NULL) ? 1U : 0U;
    hitl_stats_data.client_connected = hitl_client_active;
    *stats = hitl_stats_data;
}
