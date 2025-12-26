/*
 * config/network.h - Network Configuration Defines
 *
 * SINGLE SOURCE OF TRUTH for all network-related configuration.
 * Consolidates defines previously scattered across http_server.c,
 * stream_server.c, rest_api.c, and net_init.h.
 *
 * Compliant with:
 * - MISRA-C:2012
 * - OpenBSD style guide
 */

#ifndef CONFIG_NETWORK_H
#define CONFIG_NETWORK_H

#include <stdint.h>

/*
 * ===========================================================================
 * HTTP Server Configuration
 * ===========================================================================
 */
#define HTTP_PORT               8080U
#define HTTP_MAX_CONNECTIONS    8U
#define HTTP_CONN_TIMEOUT_MS    5000U
#define HTTP_METHOD_MAX_LEN     8U
#define HTTP_URI_MAX_LEN        64U

/*
 * ===========================================================================
 * Stream Server Configuration
 * ===========================================================================
 */
#define STREAM_PORT             8888U
#define MAX_STREAM_CLIENTS      6U

/*
 * ===========================================================================
 * Buffer Sizes (shared across HTTP/REST modules)
 * ===========================================================================
 */
#define MAX_REQ_SIZE            2048U
#define MAX_RESP_SIZE           2048U
#define MAX_JSON_TOKENS         64U

/*
 * ===========================================================================
 * Stream Timing Configuration
 * ===========================================================================
 */
#define ETHERNET_STREAM_MIN_INTERVAL_MS      10U
#define ETHERNET_STREAM_MAX_INTERVAL_MS      1000U
#define ETHERNET_STREAM_DEFAULT_INTERVAL_MS  100U

/*
 * ===========================================================================
 * DHCP State Machine
 * ===========================================================================
 */
#define DHCP_OFF                   ((uint8_t)0)
#define DHCP_START                 ((uint8_t)1)
#define DHCP_WAIT_ADDRESS          ((uint8_t)2)
#define DHCP_ADDRESS_ASSIGNED      ((uint8_t)3)
#define DHCP_TIMEOUT               ((uint8_t)4)
#define DHCP_LINK_DOWN             ((uint8_t)5)

/*
 * ===========================================================================
 * Compile-time Validation
 * ===========================================================================
 */
_Static_assert(HTTP_PORT > 0U && HTTP_PORT <= 65535U,
               "HTTP_PORT must be a valid port number");
_Static_assert(STREAM_PORT > 0U && STREAM_PORT <= 65535U,
               "STREAM_PORT must be a valid port number");
_Static_assert(HTTP_PORT != STREAM_PORT,
               "HTTP_PORT and STREAM_PORT must be different");
_Static_assert(HTTP_MAX_CONNECTIONS > 0U,
               "HTTP_MAX_CONNECTIONS must be at least 1");
_Static_assert(MAX_STREAM_CLIENTS > 0U,
               "MAX_STREAM_CLIENTS must be at least 1");
_Static_assert(ETHERNET_STREAM_MIN_INTERVAL_MS <= ETHERNET_STREAM_DEFAULT_INTERVAL_MS,
               "Default interval must be >= minimum");
_Static_assert(ETHERNET_STREAM_DEFAULT_INTERVAL_MS <= ETHERNET_STREAM_MAX_INTERVAL_MS,
               "Default interval must be <= maximum");

#endif /* CONFIG_NETWORK_H */
