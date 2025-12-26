/*
 * net_init.c - Ethernet initialization for bare-metal STM32H7
 *
 * Initializes the lwIP network stack and Ethernet interface.
 * Loads network configuration from NVM or uses hardcoded defaults.
 */

#include "lwip/init.h"
#include "lwip/mem.h"
#include "lwip/memp.h"
#include "lwip/opt.h"
#include "lwip/tcp.h"
#include "lwip/udp.h"
#include "netif/etharp.h"

#include "board.h"
#include "drivers/uart.h"
#include "ethernetif.h"
#include "network/http.h"
#include "network/net_init.h"
#include "network/nvm.h"

struct netif gnetif;

/*
 * ethernet_link_status_updated - Notify user of link state changes
 *
 * Called by lwIP when the Ethernet link comes up or goes down.
 * Prints status to UART for debugging.
 */
void
ethernet_link_status_updated(struct netif *netif)
{
	if (netif_is_up(netif))
		uart_write_string(NULL, "\r\nEthernet link up\r\n", 100);
	else
		uart_write_string(NULL, "\r\nEthernet link down\r\n", 100);
}

/*
 * ethernet_link_thread - Configure network interface with IP settings
 *
 * Loads IP configuration from NVM if valid, otherwise uses hardcoded
 * defaults. Sets up the lwIP netif and registers link callbacks.
 * Named "thread" for historical reasons but runs synchronously.
 */
void
ethernet_link_thread(void const *argument)
{
	struct network_nvm_config config;
	struct uart_handle *uart;
	ip4_addr_t ipaddr, netmask, gw;
	bool loaded;

	(void)argument;

	/* Try loading from NVM first */
	loaded = network_nvm_load(&config);
	if (loaded && config.magic == NETWORK_NVM_MAGIC &&
	    config.version == NETWORK_NVM_VERSION) {
		/* Validate checksum */
		if (network_nvm_calculate_checksum(&config) == config.checksum) {
			/* Use NVM values */
			ip4addr_aton(config.ip_addr, &ipaddr);
			ip4addr_aton(config.netmask, &netmask);
			ip4addr_aton(config.gateway, &gw);
			uart = uart_get_handle();
			if (uart)
				uart_write_string(uart,
				    "\r\nLoaded network config from NVM\r\n",
				    100);
		} else {
			loaded = false;
			uart = uart_get_handle();
			if (uart)
				uart_write_string(uart,
				    "\r\nNVM checksum mismatch\r\n", 100);
		}
	} else {
		loaded = false;
		uart = uart_get_handle();
		if (uart) {
			if (!loaded)
				uart_write_string(uart,
				    "\r\nNVM load failed\r\n", 100);
			else if (config.magic != NETWORK_NVM_MAGIC)
				uart_write_string(uart,
				    "\r\nNVM magic invalid\r\n", 100);
			else if (config.version != NETWORK_NVM_VERSION)
				uart_write_string(uart,
				    "\r\nNVM version invalid\r\n", 100);
		}
	}

	if (!loaded) {
		/* Fallback to hardcoded defaults */
		IP4_ADDR(&ipaddr, 10, 0, 1, 17);
		IP4_ADDR(&netmask, 255, 255, 255, 0);
		IP4_ADDR(&gw, 10, 0, 1, 1);
		uart = uart_get_handle();
		if (uart)
			uart_write_string(uart,
			    "\r\nUsing default network config\r\n", 100);
	}

	/* Add the network interface */
	netif_add(&gnetif, &ipaddr, &netmask, &gw, NULL, &ethernetif_init,
	    &ethernet_input);

	/* Ensure IP is set correctly (in case init function overwrites it) */
	netif_set_addr(&gnetif, &ipaddr, &netmask, &gw);

	/* Registers the default network interface */
	netif_set_default(&gnetif);

	if (netif_is_link_up(&gnetif)) {
		/* When the netif is fully configured this function must be called */
		netif_set_up(&gnetif);
		ethernet_link_status_updated(&gnetif);
	} else {
		/* When the netif link is down this function must be called */
		netif_set_down(&gnetif);
		uart_write_string(NULL,
		    "\r\nEthernet link down (PHY not connected)\r\n", 100);
	}

	/* Set the link callback function */
	netif_set_link_callback(&gnetif, ethernet_link_status_updated);
}

/*
 * ethernet_init - Initialize Ethernet subsystem
 *
 * Initializes lwIP stack, configures the network interface,
 * and starts the HTTP server for web-based configuration.
 */
void
ethernet_init(void)
{
	/* Initialize the LwIP stack */
	lwip_init();

	/* Configure the Network interface */
	ethernet_link_thread(NULL);

	/* Initialize HTTP server */
	ethernet_http_init();
}

/*
 * ethernet_process - Poll Ethernet for received packets
 *
 * Must be called periodically from main loop. Processes incoming
 * packets and checks PHY link status every 500ms.
 */
void
ethernet_process(void)
{
	static uint32_t last_link_check_ms = 0;
	uint32_t now;

	now = board_get_systick_ms();

	/* Process any received Ethernet packets */
	ethernetif_input(&gnetif);

	/* Periodically poll PHY/link status to bring the interface up/down */
	if ((now - last_link_check_ms) >= 500U) {
		ethernet_link_check_state(&gnetif);
		last_link_check_ms = now;
	}
}
