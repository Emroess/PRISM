/*
 * net_init.h - Network initialization and Ethernet processing
 *
 * Provides initialization and polling functions for the lwIP-based
 * Ethernet stack on STM32H7.
 */

#ifndef NET_INIT_H
#define NET_INIT_H

#include "lwip/netif.h"

#include "config/network.h"

#ifdef __cplusplus
extern "C" {
#endif

void	ethernet_link_status_updated(struct netif *);
void	ethernet_init(void);
void	ethernet_process(void);

#ifdef __cplusplus
}
#endif

#endif /* NET_INIT_H */




