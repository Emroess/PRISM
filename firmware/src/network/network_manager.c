/*
 * network_manager.c - Network service implementation
 *
 * Provides clean API abstraction over LwIP and Ethernet driver.
 */

#include <stdio.h>
#include <string.h>

#include "lwip/dhcp.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"

#include "network/http.h"
#include "network/manager.h"
#include "network/net_init.h"
#include "network/nvm.h"
#include "network/ping.h"
#include "network/stream.h"

extern struct netif gnetif;

status_t
network_get_info(struct network_info *info)
{
	if (info == NULL)
		return STATUS_ERROR_INVALID_PARAM;
	
	/* Format IP address */
	snprintf(info->ip_addr, sizeof(info->ip_addr), "%s",
	    ip4addr_ntoa(netif_ip4_addr(&gnetif)));
	
	/* Format netmask */
	snprintf(info->netmask, sizeof(info->netmask), "%s",
	    ip4addr_ntoa(netif_ip4_netmask(&gnetif)));
	
	/* Format gateway */
	snprintf(info->gateway, sizeof(info->gateway), "%s",
	    ip4addr_ntoa(netif_ip4_gw(&gnetif)));
	
	/* Format MAC address */
	snprintf(info->mac_addr, sizeof(info->mac_addr),
	    "%02x:%02x:%02x:%02x:%02x:%02x",
	    gnetif.hwaddr[0], gnetif.hwaddr[1], gnetif.hwaddr[2],
	    gnetif.hwaddr[3], gnetif.hwaddr[4], gnetif.hwaddr[5]);
	
	/* Link status */
	info->link_up = netif_is_link_up(&gnetif);
	info->netif_up = netif_is_up(&gnetif);
	
	/* DHCP status - may not be available depending on LwIP config */
	info->dhcp_enabled = false;
	
	return STATUS_OK;
}

status_t
network_set_static_ip(const char *ip, const char *netmask, const char *gateway)
{
	ip4_addr_t ipaddr, netmask_addr, gw_addr;
	
	if (ip == NULL || netmask == NULL || gateway == NULL)
		return STATUS_ERROR_INVALID_PARAM;
	
	/* Parse IP addresses */
	if (!ip4addr_aton(ip, &ipaddr))
		return STATUS_ERROR_INVALID_PARAM;
	if (!ip4addr_aton(netmask, &netmask_addr))
		return STATUS_ERROR_INVALID_PARAM;
	if (!ip4addr_aton(gateway, &gw_addr))
		return STATUS_ERROR_INVALID_PARAM;
	
	/* Set static IP */
	netif_set_addr(&gnetif, &ipaddr, &netmask_addr, &gw_addr);
	
	/* Save to NVM */
	struct network_nvm_config config = {0};
	config.magic = NETWORK_NVM_MAGIC;
	config.version = NETWORK_NVM_VERSION;
	strncpy(config.ip_addr, ip, sizeof(config.ip_addr) - 1);
	config.ip_addr[sizeof(config.ip_addr) - 1] = '\0';
	strncpy(config.netmask, netmask, sizeof(config.netmask) - 1);
	config.netmask[sizeof(config.netmask) - 1] = '\0';
	strncpy(config.gateway, gateway, sizeof(config.gateway) - 1);
	config.gateway[sizeof(config.gateway) - 1] = '\0';
	config.checksum = network_nvm_calculate_checksum(&config);
	
	if (!network_nvm_save(&config)) {
		/* Log error but don't fail - runtime config still works */
		return STATUS_ERROR;
	}
	
	return STATUS_OK;
}

status_t
network_set_dhcp(bool enable)
{
	/* DHCP control not implemented - would require DHCP support in LwIP config */
	(void)enable;
	return STATUS_ERROR_NOT_SUPPORTED;
}

status_t
network_ping(const char *target_ip, uint32_t count)
{
	ip4_addr_t target;
	
	if (target_ip == NULL)
		return STATUS_ERROR_INVALID_PARAM;
	
	if (!ip4addr_aton(target_ip, &target))
		return STATUS_ERROR_INVALID_PARAM;
	
	ping_set_count(count);
	ping_init(&target);
	
	return STATUS_OK;
}

void
network_task(void)
{
	/* Handle LwIP timeouts */
	sys_check_timeouts();
	
	/* Process Ethernet */
	ethernet_process();
}

status_t
network_http_start(void)
{
	if (ethernet_http_init())
		return STATUS_OK;
	return STATUS_ERROR;
}

status_t
network_http_stop(void)
{
	ethernet_http_stop();
	return STATUS_OK;
}

status_t
network_http_get_status(bool *running)
{
	if (running == NULL)
		return STATUS_ERROR_INVALID_PARAM;
	uint32_t active_connections;
	bool logging_enabled;
	ethernet_http_get_status(running, &active_connections, &logging_enabled);
	return STATUS_OK;
}

status_t
network_http_set_logging(bool enable)
{
	ethernet_http_enable_logging(enable);
	return STATUS_OK;
}

status_t
network_stream_start(uint32_t interval_ms)
{
	ethernet_stream_set_default_interval(interval_ms);
	ethernet_stream_init();
	return STATUS_OK;
}

status_t
network_stream_stop(void)
{
	ethernet_stream_stop();
	return STATUS_OK;
}
