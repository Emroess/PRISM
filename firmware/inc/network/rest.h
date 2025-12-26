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

#endif /* NETWORK_REST_H */