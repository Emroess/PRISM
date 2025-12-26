/*
 * network/manager.h - Network service abstraction
 *
 * Provides clean API for network operations, hiding LwIP implementation details
 * Used by CLI, HTTP server, and other network consumers
 */

#ifndef NETWORK_MANAGER_H
#define NETWORK_MANAGER_H

#include "status.h"
#include <stdint.h>
#include <stdbool.h>

/*
 * Network information structure
 * Populated by network_get_info()
 */
struct network_info {
	char ip_addr[16];       /* IPv4 address string (e.g., "192.168.1.100") */
	char netmask[16];       /* Netmask string */
	char gateway[16];       /* Gateway string */
	char mac_addr[18];      /* MAC address string (e.g., "00:11:22:33:44:55") */
	bool link_up;           /* Physical link status */
	bool netif_up;          /* Network interface up */
	bool dhcp_enabled;      /* DHCP client enabled */
};

/*
 * Get current network configuration and status
 *
 * Returns: STATUS_OK on success, error code on failure
 * info: Output structure to populate
 */
status_t network_get_info(struct network_info *info);

/*
 * Set static IP configuration
 *
 * Returns: STATUS_OK on success, error code on failure
 * ip: IPv4 address string (e.g., "192.168.1.100")
 * netmask: Network mask string (e.g., "255.255.255.0")
 * gateway: Gateway address string (e.g., "192.168.1.1")
 */
status_t network_set_static_ip(const char *ip, const char *netmask,
    const char *gateway);

/*
 * Enable or disable DHCP client
 *
 * Returns: STATUS_OK on success, error code on failure
 * enable: true to enable DHCP, false to disable
 */
status_t network_set_dhcp(bool enable);

/*
 * Ping a remote host (async operation)
 *
 * Returns: STATUS_OK if ping initiated, error code on failure
 * target_ip: Target IP address string
 * count: Number of ping packets to send
 */
status_t network_ping(const char *target_ip, uint32_t count);

/*
 * Network task - must be called periodically from main loop
 * Handles LwIP timeouts and Ethernet processing
 */
void network_task(void);

/*
 * HTTP server control
 */
status_t network_http_start(void);
status_t network_http_stop(void);
status_t network_http_get_status(bool *running);
status_t network_http_set_logging(bool enable);

/*
 * Ethernet stream server control
 */
status_t network_stream_start(uint32_t interval_ms);
status_t network_stream_stop(void);

#endif /* NETWORK_MANAGER_H */
