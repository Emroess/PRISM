/*
 * http.h - HTTP server for valve configuration and control
 *
 * Provides REST API and web UI for valve state queries,
 * configuration changes, and real-time telemetry.
 */

#ifndef NETWORK_HTTP_H
#define NETWORK_HTTP_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Initialize HTTP server on port 8080 */
bool	ethernet_http_init(void);

/* Stop HTTP server */
void	ethernet_http_stop(void);

/* Enable or disable HTTP request logging to UART */
void	ethernet_http_enable_logging(bool);

/* Returns true if logging is enabled */
bool	ethernet_http_get_logging_enabled(void);

/* Get server status: running state, connection count, logging state */
void	ethernet_http_get_status(bool *, uint32_t *, bool *);

/* Process HTTP server tasks (periodic, most work done in callbacks) */
void	ethernet_http_process(void);

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_HTTP_H */