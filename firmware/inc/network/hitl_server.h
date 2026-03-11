/*
 * network/hitl_server.h - Hardware-In-The-Loop TCP server (port 8889)
 *
 * Bidirectional channel between STM32 firmware and Isaac Sim:
 *   OUT  -> {"torque_nm":<f>,"seq":<u32>,"t_us":<u64>}\n  (per control tick)
 *   IN   <- {"pos":<deg>,"vel":<rad_s>}\n                  (from Isaac Sim)
 *
 * When HITL mode is active the control loop calls:
 *   1. hitl_server_set_torque()  - queue the torque command for transmission
 *   2. hitl_server_get_encoder() - read back the latest simulated encoder data
 *
 * If no encoder packet arrives within HITL_ENCODER_TIMEOUT_MS the getter
 * returns false so the caller can handle it like a CAN encoder timeout.
 */

#ifndef NETWORK_HITL_SERVER_H
#define NETWORK_HITL_SERVER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Simulated encoder data received from Isaac Sim */
struct hitl_encoder_data {
    float pos_deg;      /* Joint angle (degrees) from Isaac Sim integrator */
    float vel_rad_s;    /* Angular velocity (rad/s) from Isaac Sim integrator */
};

/* Statistics exposed to REST API and CLI */
struct hitl_stats {
    uint8_t  active;                /* Server listening (1) or not (0) */
    uint8_t  client_connected;      /* Isaac Sim client connected (1) or not (0) */
    uint32_t torque_frames_sent;    /* Torque JSON packets transmitted to Isaac Sim */
    uint32_t encoder_frames_recv;   /* Encoder JSON packets received from Isaac Sim */
    uint32_t send_errors;           /* TCP send failures */
    uint32_t parse_errors;          /* Malformed inbound JSON lines */
    uint32_t encoder_timeouts;      /* Times encoder data was declared stale */
    uint32_t last_recv_tick_ms;     /* Board time of last good encoder packet */
};

/* Lifecycle ----------------------------------------------------------------*/

/**
 * hitl_server_init - Start the HITL TCP listener on HITL_PORT.
 * Call once after lwIP is up (same as ethernet_stream_init / ethernet_http_init).
 * Returns true on success.
 */
bool hitl_server_init(void);

/**
 * hitl_server_stop - Tear down all connections and the listener PCB.
 */
void hitl_server_stop(void);

/**
 * hitl_server_process - Housekeeping: flush pending torque frame to the
 * connected client (if any).  Call from the main loop alongside
 * ethernet_stream_process().
 */
void hitl_server_process(void);

/* Control-loop interface ---------------------------------------------------*/

/**
 * hitl_server_set_torque - Called from TIM6 ISR context to queue the
 * current torque command for the next hitl_server_process() flush.
 *
 * @torque_nm  Torque to send to Isaac Sim (N·m, signed)
 * @seq        Control-loop sample sequence number
 * @t_us_accum Monotonic accumulated time (µs) from diagnostics
 */
void hitl_server_set_torque(float torque_nm, uint32_t seq, uint64_t t_us_accum);

/**
 * hitl_server_get_encoder - Retrieve the latest simulated encoder data
 * received from Isaac Sim.
 *
 * @out  Output struct filled on success.
 * Returns true if data is valid and fresh (< HITL_ENCODER_TIMEOUT_MS old).
 * Returns false if no data or data has timed out – caller should fault.
 */
bool hitl_server_get_encoder(struct hitl_encoder_data *out);

/**
 * hitl_server_client_connected - Returns true if an Isaac Sim client is
 * currently connected (useful for status queries).
 */
bool hitl_server_client_connected(void);

/**
 * hitl_server_get_stats - Fill @stats with current counters.
 */
void hitl_server_get_stats(struct hitl_stats *stats);

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_HITL_SERVER_H */
