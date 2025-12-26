/*
 * fdcan.h - FDCAN driver API for FDCAN1
 *
 * Provides CAN bus communication via the STM32H7 FDCAN peripheral.
 * Supports standard CAN 2.0B frames, hardware filtering, and interrupt-driven
 * reception. Bit timing is calculated dynamically from kernel clock.
 */

#ifndef FDCAN_H
#define FDCAN_H

#include <stddef.h>
#include <stdint.h>

#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * CAN loopback modes
 */
#define FDCAN_MODE_NORMAL           0U  /* Normal CAN operation */
#define FDCAN_MODE_INTERNAL_LOOPBACK 1U  /* Internal loopback (no bus) */
#define FDCAN_MODE_EXTERNAL_LOOPBACK 2U  /* External loopback (with transceiver) */

/*
 * CAN frame structure (standard CAN 2.0B format)
 */
struct can_frame {
	uint32_t id;           /* CAN identifier (11 or 29 bits) */
	uint8_t dlc;           /* Data length code (0-8) */
	uint8_t extended;      /* 0 = standard ID, 1 = extended ID */
	uint8_t rtr;           /* Remote transmission request */
	uint8_t reserved;      /* Padding */
	uint8_t data[8];       /* Data bytes */
};

/*
 * FDCAN error codes for error callback
 */
#define FDCAN_ERR_BUS_OFF         1U  /* Bus off state */
#define FDCAN_ERR_ERROR_PASSIVE   2U  /* Error passive state */
#define FDCAN_ERR_ERROR_WARNING   3U  /* Error warning state */
#define FDCAN_ERR_PROTOCOL        4U  /* Protocol error */

/*
 * FDCAN configuration structure
 */
struct fdcan_config {
	uint32_t kernel_hz;            /* FDCAN kernel clock frequency */
	uint32_t bitrate;              /* Nominal bit rate (e.g., 500000) */
	uint32_t sample_point_percent; /* Sample point (e.g., 87 for 87.5%) */
	uint8_t  loopback_mode;        /* FDCAN_MODE_* constant */
};

/*
 * FDCAN handle (opaque to application)
 */
struct fdcan_handle;

typedef void (*fdcan_rx_callback_t)(const struct can_frame *frame, void *context);
typedef void (*fdcan_error_callback_t)(uint8_t error_code, void *context);

/*
 * fdcan_init - Initialize FDCAN peripheral
 *
 * @h: Pointer to FDCAN handle
 * @cfg: Pointer to configuration structure
 *
 * Returns: STATUS_OK on success, error code on failure
 */
status_t fdcan_init(struct fdcan_handle *h, const struct fdcan_config *cfg);

/*
 * fdcan_deinit - Deinitialize FDCAN peripheral
 *
 * @h: Pointer to FDCAN handle
 *
 * Returns: STATUS_OK on success, error code on failure
 */
status_t fdcan_deinit(struct fdcan_handle *h);

/*
 * fdcan_transmit - Transmit a CAN frame
 *
 * @h: Pointer to FDCAN handle
 * @frame: Pointer to CAN frame to transmit
 * @timeout_ms: Timeout in milliseconds
 *
 * Returns: STATUS_OK on success, error code on failure
 */
status_t fdcan_transmit(struct fdcan_handle *h, const struct can_frame *frame,
                        uint32_t timeout_ms);

/*
 * fdcan_receive - Receive a CAN frame
 *
 * This function is non-blocking. Returns immediately with available frame.
 *
 * @h: Pointer to FDCAN handle
 * @frame: Pointer to buffer to receive CAN frame
 *
 * Returns: STATUS_OK if frame received, STATUS_ERROR_BUFFER_EMPTY if no frame
 */
status_t fdcan_receive(struct fdcan_handle *h, struct can_frame *frame);

/*
 * fdcan_get_rx_available - Get number of frames in RX FIFO
 *
 * @h: Pointer to FDCAN handle
 *
 * Returns: Number of frames available
 */
size_t fdcan_get_rx_available(const struct fdcan_handle *h);

/*
 * fdcan_get_tx_free - Get number of free TX buffers
 *
 * @h: Pointer to FDCAN handle
 *
 * Returns: Number of free TX buffers
 */
size_t fdcan_get_tx_free(const struct fdcan_handle *h);

/*
 * fdcan_get_error_count - Get TX and RX error counters
 *
 * @h: Pointer to FDCAN handle
 * @tx_errors: Pointer to receive TX error counter
 * @rx_errors: Pointer to receive RX error counter
 *
 * Returns: STATUS_OK on success
 */
status_t fdcan_get_error_count(const struct fdcan_handle *h,
                               uint8_t *tx_errors, uint8_t *rx_errors);

/*
 * fdcan_get_status_regs - Get FDCAN status registers for debugging
 *
 * @h: Pointer to FDCAN handle
 * @cccr: Pointer to receive CCCR register value
 * @test: Pointer to receive TEST register value
 * @psr: Pointer to receive PSR register value
 * @rxf0s: Pointer to receive RXF0S register value
 * @txfqs: Pointer to receive TXFQS register value
 *
 * Returns: STATUS_OK on success
 */
status_t fdcan_get_status_regs(const struct fdcan_handle *h,
                               uint32_t *cccr, uint32_t *test, uint32_t *psr,
                               uint32_t *rxf0s, uint32_t *txfqs);

/*
 * fdcan_get_config_regs - Get FDCAN configuration registers for debugging
 *
 * @h: Pointer to FDCAN handle
 * @txbc: Pointer to receive TXBC register value
 * @rxf0c: Pointer to receive RXF0C register value
 *
 * Returns: STATUS_OK on success
 */
status_t fdcan_get_config_regs(const struct fdcan_handle *h,
                               uint32_t *txbc, uint32_t *rxf0c);

/*
 * fdcan_get_handle - Get FDCAN1 handle
 *
 * Returns: Pointer to FDCAN1 handle
 */
struct fdcan_handle *fdcan_get_handle(void);

/*
 * fdcan_abort_all_tx - Cancel all pending TX buffers
 *
 * @h: Pointer to FDCAN handle
 *
 * Clears TX queue so new frames can be queued immediately.
 */
void fdcan_abort_all_tx(struct fdcan_handle *h);

void fdcan_set_rx_callback(struct fdcan_handle *h,
                          fdcan_rx_callback_t callback,
                          void *context);

/*
 * fdcan_set_error_callback - Set error callback function
 *
 * @h: Pointer to FDCAN handle
 * @callback: Error callback function (called from ISR context)
 * @context: User context pointer passed to callback
 *
 * Callback is invoked when bus-off, error passive, error warning,
 * or protocol errors occur. Handler must be fast and non-blocking.
 */
void fdcan_set_error_callback(struct fdcan_handle *h,
                              fdcan_error_callback_t callback,
                              void *context);

status_t fdcan_enable_interrupts(struct fdcan_handle *h);

/*
 * fdcan_get_error_counters - Get transmit and receive error counters
 *
 * @h: Pointer to FDCAN handle
 * @tec: Pointer to receive transmit error counter (0-255)
 * @rec: Pointer to receive error counter (0-127)
 *
 * Returns: STATUS_OK on success
 */
status_t fdcan_get_error_counters(const struct fdcan_handle *h,
                                  uint8_t *tec, uint8_t *rec);

/*
 * fdcan_get_protocol_status - Get protocol status register
 *
 * @h: Pointer to FDCAN handle
 * @psr: Pointer to receive PSR register value
 *
 * PSR bits of interest:
 *  [2:0] LEC - Last Error Code (0=no error, 1-7=various errors)
 *  [5] EP - Error Passive flag
 *  [6] EW - Error Warning flag  
 *  [7] BO - Bus Off flag
 *
 * Returns: STATUS_OK on success
 */
status_t fdcan_get_protocol_status(const struct fdcan_handle *h,
                                   uint32_t *psr);

/*
 * fdcan_set_std_range_filter - Configure standard ID range filter
 *
 * @h: Pointer to FDCAN handle
 * @id_start: Start of ID range (0x000-0x7FF)
 * @id_end: End of ID range (0x000-0x7FF)
 * @fifo: Target FIFO (0 or 1)
 *
 * Configures hardware filter to accept only CAN IDs in [id_start, id_end].
 * All other frames are rejected by hardware (no interrupt).
 * GFC is set to reject non-matching standard and extended IDs.
 *
 * Returns: STATUS_OK on success, error code on invalid parameters
 */
status_t fdcan_set_std_range_filter(struct fdcan_handle *h,
                                    uint16_t id_start, uint16_t id_end,
                                    uint8_t fifo);

#ifdef __cplusplus
}
#endif

#endif /* FDCAN_H */
