/**
  * @file    network/rest.h
  * @author  STEVE firmware team
  * @brief   REST API handlers for valve configuration and control
  */

#ifndef NETWORK_REST_H
#define NETWORK_REST_H

#include "lwip/tcp.h"

/**
 * @brief Handle GET / request (serve index HTML)
 * @param tpcb TCP PCB for the connection
 */
void rest_api_handle_get_index(struct tcp_pcb *tpcb);

/**
 * @brief Handle GET /api/v1/config request
 * @param tpcb TCP PCB for the connection
 */
void rest_api_handle_get_config(struct tcp_pcb *tpcb);

/**
 * @brief Handle GET /api/v1/status request
 * @param tpcb TCP PCB for the connection
 */
void rest_api_handle_get_status(struct tcp_pcb *tpcb);

/**
 * @brief Handle GET /api/v1/presets request
 * @param tpcb TCP PCB for the connection
 */
void rest_api_handle_get_presets(struct tcp_pcb *tpcb);

/**
 * @brief Handle POST /api/v1/config request
 * @param tpcb TCP PCB for the connection
 * @param body Request body
 * @param len Body length
 */
void rest_api_handle_post_config(struct tcp_pcb *tpcb, char *body, int len);

/**
 * @brief Handle POST /api/v1/control request
 * @param tpcb TCP PCB for the connection
 * @param body Request body
 * @param len Body length
 */
void rest_api_handle_post_control(struct tcp_pcb *tpcb, char *body, int len);

/**
 * @brief Handle POST /api/v1/presets request
 * @param tpcb TCP PCB for the connection
 * @param body Request body
 * @param len Body length
 */
void rest_api_handle_post_presets(struct tcp_pcb *tpcb, char *body, int len);

/**
 * @brief Handle GET /api/v1/odrive request - ODrive status
 * @param tpcb TCP PCB for the connection
 */
void rest_api_handle_get_odrive(struct tcp_pcb *tpcb);

/**
 * @brief Handle POST /api/v1/odrive request - ODrive commands
 * @param tpcb TCP PCB for the connection
 * @param body Request body
 * @param len Body length
 */
void rest_api_handle_post_odrive(struct tcp_pcb *tpcb, char *body, int len);

/**
 * @brief Handle GET /api/v1/can request - CAN bus status and data
 * @param tpcb TCP PCB for the connection
 */
void rest_api_handle_get_can(struct tcp_pcb *tpcb);

/**
 * @brief Handle GET /api/v1/performance request - Performance statistics
 * @param tpcb TCP PCB for the connection
 */
void rest_api_handle_get_performance(struct tcp_pcb *tpcb);

/**
 * @brief Handle GET /api/v1/stream request - Stream server status
 * @param tpcb TCP PCB for the connection
 */
void rest_api_handle_get_stream(struct tcp_pcb *tpcb);

/**
 * @brief Handle POST /api/v1/stream request - Stream server control
 * @param tpcb TCP PCB for the connection
 * @param body Request body
 * @param len Body length
 */
void rest_api_handle_post_stream(struct tcp_pcb *tpcb, char *body, int len);

/**
 * @brief Handle GET /api/v1/calibration request - Get calibration status
 * @param tpcb TCP PCB for the connection
 */
void rest_api_handle_get_calibration(struct tcp_pcb *tpcb);

/**
 * @brief Handle POST /api/v1/calibration request - Set calibration
 * @param tpcb TCP PCB for the connection
 * @param body Request body (JSON with action, value fields)
 * @param len Body length
 * 
 * Supported actions:
 * - set_zero / set_zero_here: Set current position as zero reference
 * - set_zero_at: Set zero to specific encoder value (requires "value")
 * - validate: Check if calibration is still valid
 */
void rest_api_handle_post_calibration(struct tcp_pcb *tpcb, char *body, int len);

/**
 * @brief Handle DELETE /api/v1/calibration request - Clear all calibration
 * @param tpcb TCP PCB for the connection
 */
void rest_api_handle_delete_calibration(struct tcp_pcb *tpcb);

#endif /* NETWORK_REST_H */