/**
  * @file    network/stream.h
  * @author  STEVE firmware team
  * @brief   Ethernet stream server for real-time valve data
  */

#ifndef NETWORK_STREAM_H
#define NETWORK_STREAM_H

#include <stdint.h>
#include <stdbool.h>
#include "config/network.h"

#ifdef __cplusplus
 extern "C" {
#endif

/* Stream interval defines are in config/network.h */

typedef struct {
  uint8_t active;
  uint8_t connected_clients;
  uint32_t default_interval_ms;
  uint32_t messages_sent;
  uint32_t send_errors;
  uint32_t invalid_samples;
  uint32_t last_send_tick_ms;
} ethernet_stream_stats_t;

bool ethernet_stream_init(void);
void ethernet_stream_stop(void);
void ethernet_stream_process(void);
uint32_t ethernet_stream_set_default_interval(uint32_t interval_ms);
void ethernet_stream_get_stats(ethernet_stream_stats_t *stats);

#ifdef __cplusplus
}
#endif

#endif /* NETWORK_STREAM_H */
