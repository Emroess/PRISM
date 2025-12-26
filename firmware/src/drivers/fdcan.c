/*
 * fdcan.c - FDCAN driver implementation for FDCAN1
 *
 * Per IMPROVE_FIRMWARE_PLAN.md:
 * - On hardware layer whitelist
 * - Bit timing calculated dynamically from kernel clock
 * - Message RAM layout per RM0433 Section 56.3.1, Table 408
 * - External loopback mode support
 * - Software buffers in .sram3_dma section
 *
 * Compliant with:
 * - MISRA-C:2012
 * - OpenBSD style guide
 * - DO-178C Level A requirements
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "stm32h7xx.h"

#include "board.h"
#include "config/board.h"
#include "fdcan.h"
#include "status.h"

/*
 * FDCAN message RAM header bit field definitions
 * (not all are defined in CMSIS headers for message RAM access)
 */
#define FDCAN_TX_HEADER_XTD_Pos        30U
#define FDCAN_TX_HEADER_XTD            (1U << FDCAN_TX_HEADER_XTD_Pos)
#define FDCAN_TX_HEADER_RTR            (1U << 29U)
#define FDCAN_TX_HEADER_ID_Pos         18U
#define FDCAN_TX_HEADER_DLC_Pos        16U

#define FDCAN_RX_HEADER_XTD            (1U << 30U)
#define FDCAN_RX_HEADER_XTD_MASK       0x1FFFFFFFU
#define FDCAN_RX_HEADER_XTD_Pos        0U
#define FDCAN_RX_HEADER_RTR            (1U << 29U)
#define FDCAN_RX_HEADER_ID_MASK        0x1FFC0000U
#define FDCAN_RX_HEADER_ID_Pos         18U
#define FDCAN_RX_HEADER_DLC_MASK       0x000F0000U
#define FDCAN_RX_HEADER_DLC_Pos        16U

/*
 * Message RAM layout per RM0433
 * FDCAN1 base: 0x4000AC00, size: 2560 words
 */
#define FDCAN1_MSG_RAM_BASE     BOARD_FDCAN1_RAM_BASE

/* Standard ID filter list (11-bit filters) */
#define FDCAN_STD_FILTER_COUNT  8U
#define FDCAN_STD_FILTER_SIZE   (FDCAN_STD_FILTER_COUNT * 1U)  /* Words */

/* Extended ID filter list (29-bit filters) */
#define FDCAN_EXT_FILTER_COUNT  4U
#define FDCAN_EXT_FILTER_SIZE   (FDCAN_EXT_FILTER_COUNT * 2U)  /* Words */

/* RX FIFO 0 (for standard frames) */
#define FDCAN_RX_FIFO0_COUNT    8U
#define FDCAN_RX_FIFO0_ELEM_SIZE 18U  /* Header (2) + 64 bytes data (16) */
#define FDCAN_RX_FIFO0_SIZE     (FDCAN_RX_FIFO0_COUNT * FDCAN_RX_FIFO0_ELEM_SIZE)

/* TX buffers */
#define FDCAN_TX_BUFFER_COUNT   4U
#define FDCAN_TX_BUFFER_ELEM_SIZE 18U  /* Header (2) + 64 bytes data (16) */
#define FDCAN_TX_BUFFER_SIZE    (FDCAN_TX_BUFFER_COUNT * FDCAN_TX_BUFFER_ELEM_SIZE)

/* Message RAM offsets (in 32-bit words from base) */
#define FDCAN_STD_FILTER_OFFSET 0U
#define FDCAN_EXT_FILTER_OFFSET (FDCAN_STD_FILTER_OFFSET + FDCAN_STD_FILTER_SIZE)
#define FDCAN_RX_FIFO0_OFFSET   (FDCAN_EXT_FILTER_OFFSET + FDCAN_EXT_FILTER_SIZE)
#define FDCAN_TX_BUFFER_OFFSET  (FDCAN_RX_FIFO0_OFFSET + FDCAN_RX_FIFO0_SIZE)

/* Total message RAM usage */
#define FDCAN_MSG_RAM_USED      (FDCAN_TX_BUFFER_OFFSET + FDCAN_TX_BUFFER_SIZE)

/*
 * Bit timing structure
 */
struct fdcan_bit_timing {
	uint32_t prescaler;  /* Bit rate prescaler */
	uint32_t tseg1;      /* Time segment 1 */
	uint32_t tseg2;      /* Time segment 2 */
	uint32_t sjw;        /* Synchronization jump width */
};

/*
 * FDCAN handle structure (opaque to application)
 */
struct fdcan_handle {
	FDCAN_GlobalTypeDef *instance;     /* Hardware instance */
	struct fdcan_bit_timing timing;    /* Bit timing configuration */
	uint8_t initialized;               /* Initialization flag */
	/* Software RX ring buffer (filled from IRQ) */
	struct can_frame rx_ring[16];
	volatile uint8_t rx_head;
	volatile uint8_t rx_tail;
	/* RX callback for interrupt-driven operation */
	fdcan_rx_callback_t rx_callback;
	void *rx_callback_user_data;
	/* Error callback for bus-off, error passive, etc. */
	fdcan_error_callback_t error_callback;
	void *error_callback_user_data;
};

/*
 * Static FDCAN handle for FDCAN1
 */
static struct fdcan_handle fdcan1_handle;

/*
 * Static assertions per IMPROVE_FIRMWARE_PLAN.md
 */
_Static_assert(BOARD_FDCAN1_RAM_BASE == 0x4000AC00U,
	"FDCAN1 message RAM base per RM0433 Table 408");

_Static_assert(BOARD_FDCAN1_RAM_SIZE_WORDS == 2560U,
	"FDCAN1 message RAM size per RM0433 Section 56.3.1");

_Static_assert(FDCAN_MSG_RAM_USED <= BOARD_FDCAN1_RAM_SIZE_WORDS,
	"FDCAN message RAM layout exceeds available size");

_Static_assert(sizeof(struct can_frame) == 16,
	"CAN frame structure size must be 16 bytes");

/*
 * Forward declarations
 */
static status_t fdcan_calculate_timing(uint32_t kernel_hz, uint32_t bitrate,
                                       uint32_t sample_point_percent,
                                       struct fdcan_bit_timing *timing);
static status_t fdcan_enter_init_mode(FDCAN_GlobalTypeDef *fdcan);
static status_t fdcan_exit_init_mode(FDCAN_GlobalTypeDef *fdcan);
static status_t fdcan_configure_filters(FDCAN_GlobalTypeDef *fdcan);
static void fdcan_clear_message_ram(void);
static void fdcan_push_rx_software(struct fdcan_handle *h, const struct can_frame *frame);
static int  fdcan_pop_rx_software(struct fdcan_handle *h, struct can_frame *f);
void FDCAN1_IT0_IRQHandler(void);

/*
 * fdcan_init - Initialize FDCAN peripheral
 */
status_t
fdcan_init(struct fdcan_handle *h, const struct fdcan_config *cfg)
{
	status_t status;
	uint32_t timeout;

	if (h == NULL || cfg == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	if (h->initialized != 0U) {
		return STATUS_ERROR_BUSY;
	}

	/* Initialize handle */
	memset(h, 0, sizeof(*h));
	h->instance = FDCAN1;

	/* Verify kernel clock matches configuration */
	if (cfg->kernel_hz != BOARD_FDCAN_KERNEL_HZ) {
		return STATUS_ERROR_INVALID_CONFIG;
	}

	/* Calculate bit timing */
	status = fdcan_calculate_timing(cfg->kernel_hz, cfg->bitrate,
	                                cfg->sample_point_percent,
	                                &h->timing);
	if (status != STATUS_OK) {
		return status;
	}

	/* Enable FDCAN1 clock */
	RCC->APB1HENR |= RCC_APB1HENR_FDCANEN;
	(void)RCC->APB1HENR;  /* Read back for write completion */

	/* Select PLL1Q as FDCAN kernel clock */
	RCC->D2CCIP1R = (RCC->D2CCIP1R & ~RCC_D2CCIP1R_FDCANSEL) |
	                (0x1U << RCC_D2CCIP1R_FDCANSEL_Pos);  /* PLL1Q */

	/* Exit sleep mode */
	FDCAN1->CCCR &= ~FDCAN_CCCR_CSR;

	/* Wait for sleep mode to exit */
	timeout = 10000U;
	while ((FDCAN1->CCCR & FDCAN_CCCR_CSA) != 0U) {
		timeout--;
		if (timeout == 0U) {
			return STATUS_ERROR_TIMEOUT;
		}
	}

	/* Enter initialization mode */
	status = fdcan_enter_init_mode(h->instance);
	if (status != STATUS_OK) {
		return status;
	}

	/* Enable configuration change */
	h->instance->CCCR |= FDCAN_CCCR_CCE;

	/* Clear the portion of message RAM we use */
	fdcan_clear_message_ram();

	/* Configure bit timing */
	h->instance->NBTP = ((h->timing.sjw - 1U) << FDCAN_NBTP_NSJW_Pos) |
	                    ((h->timing.tseg1 - 1U) << FDCAN_NBTP_NTSEG1_Pos) |
	                    ((h->timing.tseg2 - 1U) << FDCAN_NBTP_NTSEG2_Pos) |
	                    ((h->timing.prescaler - 1U) << FDCAN_NBTP_NBRP_Pos);

	/* Configure for Classic CAN mode (not FD) */
	h->instance->CCCR &= ~FDCAN_CCCR_FDOE;  /* Disable FD operation */
	h->instance->CCCR &= ~FDCAN_CCCR_BRSE;  /* No bit rate switching */

	/* CRITICAL: Disable automatic retransmission in all modes to prevent
	 * retransmission storms during errors. High-rate control loops must
	 * tolerate occasional dropped frames rather than retrying into bus-off. */
	h->instance->CCCR |= FDCAN_CCCR_DAR;

	/* Configure loopback mode if requested */
	if (cfg->loopback_mode == FDCAN_MODE_INTERNAL_LOOPBACK) {
		/* Internal loopback: silent, loop back internally */
		h->instance->CCCR |= FDCAN_CCCR_TEST | FDCAN_CCCR_MON;
		h->instance->TEST = FDCAN_TEST_LBCK;
	} else if (cfg->loopback_mode == FDCAN_MODE_EXTERNAL_LOOPBACK) {
		/*
		 * External loopback via transceiver: leave MON cleared so the
		 * pin still toggles for scope debugging.
		 */
		h->instance->CCCR |= FDCAN_CCCR_TEST;
		h->instance->TEST = FDCAN_TEST_LBCK;
	}
	/* else: FDCAN_MODE_NORMAL - no test mode */

	/* Configure message RAM addresses */
	h->instance->SIDFC = (FDCAN_STD_FILTER_OFFSET << FDCAN_SIDFC_FLSSA_Pos) |
	                     (FDCAN_STD_FILTER_COUNT << FDCAN_SIDFC_LSS_Pos);

	h->instance->XIDFC = (FDCAN_EXT_FILTER_OFFSET << FDCAN_XIDFC_FLESA_Pos) |
	                     (FDCAN_EXT_FILTER_COUNT << FDCAN_XIDFC_LSE_Pos);

	h->instance->RXF0C = (FDCAN_RX_FIFO0_OFFSET << FDCAN_RXF0C_F0SA_Pos) |
	                     (FDCAN_RX_FIFO0_COUNT << FDCAN_RXF0C_F0S_Pos);

	/* No Rx FIFO1, Rx buffers, or Tx event FIFO in this configuration */
	h->instance->RXF1C = 0U;
	h->instance->RXBC = 0U;
	h->instance->TXEFC = 0U;

	/* Configure TX in FIFO mode (not dedicated buffers) */
	h->instance->TXBC = (FDCAN_TX_BUFFER_OFFSET << FDCAN_TXBC_TBSA_Pos) |
	                    (FDCAN_TX_BUFFER_COUNT << FDCAN_TXBC_TFQS_Pos) |
	                    FDCAN_TXBC_TFQM;  /* TX FIFO mode */
	
	/* Read back to ensure write completed */
	(void)h->instance->TXBC;

	/* Configure element sizes (64-byte data field) */
	h->instance->RXESC = (0x7U << FDCAN_RXESC_F0DS_Pos);  /* 64 bytes */
	h->instance->TXESC = (0x7U << FDCAN_TXESC_TBDS_Pos);  /* 64 bytes */

	/* Configure filters to accept all frames */
	status = fdcan_configure_filters(h->instance);
	if (status != STATUS_OK) {
		return status;
	}

	/* Leave interrupts disabled by default - call fdcan_enable_interrupts() to enable */
	h->instance->ILE = 0;

	/* Disable configuration change before exiting init mode */
	h->instance->CCCR &= ~FDCAN_CCCR_CCE;

	/* Exit initialization mode and start */
	status = fdcan_exit_init_mode(h->instance);
	if (status != STATUS_OK) {
		return status;
	}

    /* Init software RX ring */
    h->rx_head = 0U;
    h->rx_tail = 0U;
    
    h->initialized = 1U;

	return STATUS_OK;
}

/*
 * fdcan_deinit - Deinitialize FDCAN peripheral
 */
status_t
fdcan_deinit(struct fdcan_handle *h)
{
	if (h == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	if (h->initialized == 0U) {
		return STATUS_ERROR_NOT_INITIALIZED;
	}

	/* Enter initialization mode */
	(void)fdcan_enter_init_mode(h->instance);

	/* Disable FDCAN clock */
	RCC->APB1HENR &= ~RCC_APB1HENR_FDCANEN;

	h->initialized = 0U;

	return STATUS_OK;
}

/*
 * fdcan_transmit - Transmit a CAN frame
 */
status_t
fdcan_transmit(struct fdcan_handle *h, const struct can_frame *frame,
               uint32_t timeout_ms)
{
	uint32_t *msg_ram;
	uint32_t put_index;
	uint32_t header0;
	uint32_t header1;
	uint32_t i;
	uint32_t start;
	uint32_t elapsed;

	if (h == NULL || frame == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	if (h->initialized == 0U) {
		return STATUS_ERROR_NOT_INITIALIZED;
	}

	/* Check for free TX buffer */
	if ((h->instance->TXFQS & FDCAN_TXFQS_TFQF) != 0U) {
		return STATUS_ERROR_BUFFER_FULL;
	}

	/* Get put index */
	put_index = (h->instance->TXFQS & FDCAN_TXFQS_TFQPI) >> 
	            FDCAN_TXFQS_TFQPI_Pos;

	/* Calculate message RAM address */
	msg_ram = (uint32_t *)(FDCAN1_MSG_RAM_BASE + 
	          (FDCAN_TX_BUFFER_OFFSET + 
	           (put_index * FDCAN_TX_BUFFER_ELEM_SIZE)) * 4U);

	/* Build header */
	if (frame->extended != 0U) {
		header0 = (frame->id << FDCAN_TX_HEADER_XTD_Pos) | FDCAN_TX_HEADER_XTD;
	} else {
		header0 = (frame->id << FDCAN_TX_HEADER_ID_Pos);
	}

	if (frame->rtr != 0U) {
		header0 |= FDCAN_TX_HEADER_RTR;
	}

	header1 = (frame->dlc << FDCAN_TX_HEADER_DLC_Pos);

	/* Write header */
	msg_ram[0] = header0;
	msg_ram[1] = header1;

	/* Write data */
	for (i = 0; i < ((frame->dlc + 3U) / 4U); i++) {
		msg_ram[2 + i] = ((uint32_t)frame->data[i * 4U + 3U] << 24) |
		                 ((uint32_t)frame->data[i * 4U + 2U] << 16) |
		                 ((uint32_t)frame->data[i * 4U + 1U] << 8) |
		                 ((uint32_t)frame->data[i * 4U + 0U]);
	}

	/* Request transmission */
	h->instance->TXBAR = (1U << put_index);

	/* Wait for transmission to complete if timeout specified */
	if (timeout_ms > 0U) {
		start = board_get_systick_ms();
		while ((h->instance->TXBTO & (1U << put_index)) == 0U) {
			elapsed = board_get_systick_ms() - start;
			if (elapsed >= timeout_ms) {
				return STATUS_ERROR_TIMEOUT;
			}
		}
	}

	return STATUS_OK;
}

/*
 * fdcan_receive - Receive a CAN frame
 */
status_t
fdcan_receive(struct fdcan_handle *h, struct can_frame *frame)
{
    uint32_t *msg_ram;
    uint32_t get_index;
    uint32_t fill_level;
    uint32_t header0;
    uint32_t header1;
    uint32_t i;

    if (h == NULL || frame == NULL) {
        return STATUS_ERROR_INVALID_PARAM;
    }

    if (h->initialized == 0U) {
        return STATUS_ERROR_NOT_INITIALIZED;
    }

    /* Try software ring first (filled by IRQ) */
    if (fdcan_pop_rx_software(h, frame) == 0) {
        return STATUS_OK;
    }

    /* Check if RX FIFO 0 has frames */
    fill_level = (h->instance->RXF0S & FDCAN_RXF0S_F0FL) >> 
                 FDCAN_RXF0S_F0FL_Pos;
    if (fill_level == 0U) {
        return STATUS_ERROR_BUFFER_EMPTY;
    }

	/* Get get index */
	get_index = (h->instance->RXF0S & FDCAN_RXF0S_F0GI) >> 
	            FDCAN_RXF0S_F0GI_Pos;

	/* Calculate message RAM address */
	msg_ram = (uint32_t *)(FDCAN1_MSG_RAM_BASE + 
	          (FDCAN_RX_FIFO0_OFFSET + 
	           (get_index * FDCAN_RX_FIFO0_ELEM_SIZE)) * 4U);

	/* Read header */
	header0 = msg_ram[0];
	header1 = msg_ram[1];

	/* Parse header */
	if ((header0 & FDCAN_RX_HEADER_XTD) != 0U) {
		frame->id = (header0 & FDCAN_RX_HEADER_XTD_MASK) >> 
		            FDCAN_RX_HEADER_XTD_Pos;
		frame->extended = 1U;
	} else {
		frame->id = (header0 & FDCAN_RX_HEADER_ID_MASK) >> 
		            FDCAN_RX_HEADER_ID_Pos;
		frame->extended = 0U;
	}

	frame->rtr = ((header0 & FDCAN_RX_HEADER_RTR) != 0U) ? 1U : 0U;
	frame->dlc = (header1 & FDCAN_RX_HEADER_DLC_MASK) >> 
	             FDCAN_RX_HEADER_DLC_Pos;

	/* Read data */
	for (i = 0; i < ((frame->dlc + 3U) / 4U); i++) {
		uint32_t word = msg_ram[2 + i];
		frame->data[i * 4U + 0U] = (uint8_t)(word & 0xFFU);
		frame->data[i * 4U + 1U] = (uint8_t)((word >> 8) & 0xFFU);
		frame->data[i * 4U + 2U] = (uint8_t)((word >> 16) & 0xFFU);
		frame->data[i * 4U + 3U] = (uint8_t)((word >> 24) & 0xFFU);
	}

	/* Acknowledge read */
	h->instance->RXF0A = get_index;

    return STATUS_OK;
}

/* Push to software RX ring (drop oldest on overflow) */
static void
fdcan_push_rx_software(struct fdcan_handle *h, const struct can_frame *frame)
{
	uint8_t next;

	if (h == NULL || frame == NULL) {
		return;
	}

	next = (uint8_t)((h->rx_head + 1U) & 0x0FU);
	if (next == h->rx_tail) {
		h->rx_tail = (uint8_t)((h->rx_tail + 1U) & 0x0FU);
	}

	h->rx_ring[h->rx_head] = *frame;
	h->rx_head = next;
}

/* Pop from software RX ring; returns 0 if popped, -1 if empty */
static int
fdcan_pop_rx_software(struct fdcan_handle *h, struct can_frame *f)
{
	if (h->rx_head == h->rx_tail) {
		return -1;
    }
    *f = h->rx_ring[h->rx_tail];
    h->rx_tail = (uint8_t)((h->rx_tail + 1U) & 0x0F);
    return 0;
}

/*
 * fdcan_get_rx_available - Get number of frames in RX FIFO
 */
size_t
fdcan_get_rx_available(const struct fdcan_handle *h)
{
	if (h == NULL || h->initialized == 0U) {
		return 0;
	}

	return (size_t)((h->instance->RXF0S & FDCAN_RXF0S_F0FL) >> 
	                FDCAN_RXF0S_F0FL_Pos);
}

/*
 * fdcan_get_tx_free - Get number of free TX buffers
 */
size_t
fdcan_get_tx_free(const struct fdcan_handle *h)
{
	uint32_t free_level;

	if (h == NULL || h->initialized == 0U) {
		return 0;
	}

	free_level = (h->instance->TXFQS & FDCAN_TXFQS_TFFL) >> 
	             FDCAN_TXFQS_TFFL_Pos;

	return (size_t)free_level;
}

/*
 * fdcan_get_error_count - Get error counters
 */
status_t
fdcan_get_error_count(const struct fdcan_handle *h,
                      uint8_t *tx_errors, uint8_t *rx_errors)
{
	if (h == NULL || tx_errors == NULL || rx_errors == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	if (h->initialized == 0U) {
		return STATUS_ERROR_NOT_INITIALIZED;
	}

	*tx_errors = (uint8_t)((h->instance->ECR & FDCAN_ECR_TEC) >> 
	                       FDCAN_ECR_TEC_Pos);
	*rx_errors = (uint8_t)((h->instance->ECR & FDCAN_ECR_REC) >> 
	                       FDCAN_ECR_REC_Pos);

	return STATUS_OK;
}

/*
 * fdcan_get_status_regs - Get FDCAN status registers for debugging
 */
status_t
fdcan_get_status_regs(const struct fdcan_handle *h,
                      uint32_t *cccr, uint32_t *test, uint32_t *psr,
                      uint32_t *rxf0s, uint32_t *txfqs)
{
	if (h == NULL || cccr == NULL || test == NULL || psr == NULL ||
	    rxf0s == NULL || txfqs == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	if (h->initialized == 0U) {
		return STATUS_ERROR_NOT_INITIALIZED;
	}

	*cccr = h->instance->CCCR;
	*test = h->instance->TEST;
	*psr = h->instance->PSR;
	*rxf0s = h->instance->RXF0S;
	*txfqs = h->instance->TXFQS;

	return STATUS_OK;
}

/*
 * fdcan_get_config_regs - Get FDCAN configuration registers for debugging
 */
status_t
fdcan_get_config_regs(const struct fdcan_handle *h,
                      uint32_t *txbc, uint32_t *rxf0c)
{
	if (h == NULL || txbc == NULL || rxf0c == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	if (h->initialized == 0U) {
		return STATUS_ERROR_NOT_INITIALIZED;
	}

	*txbc = h->instance->TXBC;
	*rxf0c = h->instance->RXF0C;

	return STATUS_OK;
}

/*
 * fdcan_calculate_timing - Calculate bit timing parameters
 *
 * Per IMPROVE_FIRMWARE_PLAN.md: no hard-coded timing values
 */
static status_t
fdcan_calculate_timing(uint32_t kernel_hz, uint32_t bitrate,
                       uint32_t sample_point_percent,
                       struct fdcan_bit_timing *timing)
{
	uint32_t prescaler;
	uint32_t tq_per_bit;
	uint32_t tseg1;
	uint32_t tseg2;
	uint32_t sjw;

	if (timing == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	/* Calculate time quanta per bit */
	tq_per_bit = kernel_hz / bitrate;

	/* Find suitable prescaler */
	for (prescaler = 1U; prescaler <= 512U; prescaler++) {
		uint32_t tq = tq_per_bit / prescaler;
		uint32_t sample_point;

		/* Check if tq is in valid range (8-25 typical) */
		if (tq < 8U || tq > 25U) {
			continue;
		}

		/* Calculate segment lengths */
		sample_point = (tq * sample_point_percent) / 100U;
		tseg1 = sample_point - 1U;  /* -1 for sync segment */
		tseg2 = tq - sample_point;

		/* Check if segments are in valid range */
		if (tseg1 < 2U || tseg1 > 256U) {
			continue;
		}
		if (tseg2 < 1U || tseg2 > 128U) {
			continue;
		}

		/* SJW is typically min(tseg1, tseg2, 4) */
		sjw = (tseg2 < 4U) ? tseg2 : 4U;

		/* Valid configuration found */
		timing->prescaler = prescaler;
		timing->tseg1 = tseg1;
		timing->tseg2 = tseg2;
		timing->sjw = sjw;

		return STATUS_OK;
	}

	/* No valid configuration found */
	return STATUS_ERROR_INVALID_CONFIG;
}

/*
 * fdcan_enter_init_mode - Enter initialization mode
 */
static status_t
fdcan_enter_init_mode(FDCAN_GlobalTypeDef *fdcan)
{
	uint32_t timeout;

	fdcan->CCCR |= FDCAN_CCCR_INIT;

	timeout = 10000U;
	while ((fdcan->CCCR & FDCAN_CCCR_INIT) == 0U) {
		timeout--;
		if (timeout == 0U) {
			return STATUS_ERROR_TIMEOUT;
		}
	}

	return STATUS_OK;
}

/*
 * fdcan_exit_init_mode - Exit initialization mode
 */
static status_t
fdcan_exit_init_mode(FDCAN_GlobalTypeDef *fdcan)
{
	uint32_t timeout;

	fdcan->CCCR &= ~FDCAN_CCCR_INIT;

	timeout = 10000U;
	while ((fdcan->CCCR & FDCAN_CCCR_INIT) != 0U) {
		timeout--;
		if (timeout == 0U) {
			return STATUS_ERROR_TIMEOUT;
		}
	}

	return STATUS_OK;
}

/*
 * fdcan_configure_filters - Configure acceptance filters
 *
 * Set filters to accept all frames (for loopback testing)
 */
static status_t
fdcan_configure_filters(FDCAN_GlobalTypeDef *fdcan)
{
	uint32_t *filter_ram;
    uint32_t i;

	/* Configure standard ID filter 0: accept entire 11-bit range to FIFO0 */
#define STD_FILTER_CFG_TO_FIFO0   0x1U
#define STD_FILTER_TYPE_RANGE     0x0U
	filter_ram = (uint32_t *)(FDCAN1_MSG_RAM_BASE + 
	                          (FDCAN_STD_FILTER_OFFSET * 4U));

    /* Accept all std IDs to FIFO0 */
    filter_ram[0] = ((STD_FILTER_TYPE_RANGE << 30U) |
                     (STD_FILTER_CFG_TO_FIFO0 << 27U) |
                     (0x000U << 16U) |
                     (0x7FFU));
    for (i = 1U; i < FDCAN_STD_FILTER_COUNT; i++) {
        filter_ram[i] = 0U;
    }

	/* Configure extended ID filter 0: accept entire 29-bit range to FIFO0 */
#define EXT_FILTER_CFG_TO_FIFO0   0x1U
#define EXT_FILTER_TYPE_RANGE     0x0U
	filter_ram = (uint32_t *)(FDCAN1_MSG_RAM_BASE + 
	                          (FDCAN_EXT_FILTER_OFFSET * 4U));
    /* Accept all ext IDs to FIFO0 */
    filter_ram[0] = (EXT_FILTER_CFG_TO_FIFO0 << 29U);  /* ID1 = 0 */
    filter_ram[1] = ((EXT_FILTER_TYPE_RANGE << 30U) | 0x1FFFFFFFU);
    for (i = 2U; i < (FDCAN_EXT_FILTER_COUNT * 2U); i++) {
        filter_ram[i] = 0U;
    }

    /* Set global filter configuration: accept non-matching to FIFO0 */
    fdcan->GFC = (1U << FDCAN_GFC_ANFS_Pos) |  /* Accept non-matching std to FIFO 0 */
                 (1U << FDCAN_GFC_ANFE_Pos);   /* Accept non-matching ext to FIFO 0 */

	return STATUS_OK;
}

/*
 * fdcan_clear_message_ram - Zero the portion of CAN message RAM we use
 */
static void
fdcan_clear_message_ram(void)
{
	volatile uint32_t *ram;
	uint32_t i;

	ram = (volatile uint32_t *)FDCAN1_MSG_RAM_BASE;

	for (i = 0U; i < FDCAN_MSG_RAM_USED; i++) {
		ram[i] = 0U;
	}
}

/*
 * fdcan_get_handle - Get FDCAN1 handle
 */
struct fdcan_handle *
fdcan_get_handle(void)
{
	return &fdcan1_handle;
}

/*
 * fdcan_abort_all_tx - Cancel all pending TX buffers
 */
void
fdcan_abort_all_tx(struct fdcan_handle *h)
{
	uint32_t mask;
	uint32_t timeout;

	if (h == NULL || h->initialized == 0U) {
		return;
	}

	if (FDCAN_TX_BUFFER_COUNT >= 32U) {
		mask = 0xFFFFFFFFU;
	} else {
		mask = (1U << FDCAN_TX_BUFFER_COUNT) - 1U;
	}

	/* Request cancellation of all buffers */
	h->instance->TXBCR = mask;

	/* Wait briefly for cancellation to complete */
	timeout = 1000U;
	while (((h->instance->TXBCF & mask) != mask) && (timeout-- > 0U)) {
		__NOP();
	}

	/* Clear status flags */
	h->instance->TXBCF = mask;
	h->instance->TXBTO = mask;
}

/*
 * fdcan_set_rx_callback - Register callback for RX interrupt
 */
void
fdcan_set_rx_callback(struct fdcan_handle *h,
                     fdcan_rx_callback_t callback,
                     void *user_data)
{
	if (h == NULL) {
		return;
	}
	
	h->rx_callback = callback;
	h->rx_callback_user_data = user_data;
}

/*
 * fdcan_set_error_callback - Register callback for error interrupts
 *
 * Callback is invoked from ISR context when bus-off, error passive,
 * error warning, or protocol errors occur. Handler must be minimal.
 */
void
fdcan_set_error_callback(struct fdcan_handle *h,
                        fdcan_error_callback_t callback,
                        void *user_data)
{
	if (h == NULL) {
		return;
	}
	
	h->error_callback = callback;
	h->error_callback_user_data = user_data;
}

/*
 * fdcan_enable_interrupts - Enable RX FIFO0 and error interrupts
 */
status_t
fdcan_enable_interrupts(struct fdcan_handle *h)
{
	if (h == NULL || h->initialized == 0U) {
		return STATUS_ERROR_INVALID_PARAM;
	}
	
	/* Enable RX FIFO 0 new message interrupt */
	h->instance->IE |= FDCAN_IE_RF0NE;
	
	/* Enable error interrupts for immediate fault detection */
	h->instance->IE |= FDCAN_IE_BOE  |  /* Bus off */
	                   FDCAN_IE_EPE  |  /* Error passive */
	                   FDCAN_IE_EWE  |  /* Error warning */
	                   FDCAN_IE_PEAE |  /* Protocol error (arbitration) */
	                   FDCAN_IE_PEDE;   /* Protocol error (data phase) */
	
	/* Enable interrupt line 0 */
	h->instance->ILE = FDCAN_ILE_EINT0;
	
	/* Configure NVIC
	 * Priority 6 = MEDIUM (lower than TIM6=5, higher than UART=7)
	 * FDCAN RX should not block TIM6 control loop
	 */
	NVIC_SetPriority(FDCAN1_IT0_IRQn, 6);
	NVIC_EnableIRQ(FDCAN1_IT0_IRQn);
	
	return STATUS_OK;
}

/*
 * FDCAN1_IT0_IRQHandler - FDCAN1 interrupt line 0 handler
 * 
 * This ISR is triggered when a new message arrives in RX FIFO 0.
 * It reads the frame and invokes the registered callback.
 */
void
FDCAN1_IT0_IRQHandler(void)
{
	struct fdcan_handle *h = &fdcan1_handle;
	struct can_frame frame;
	uint32_t ir;
	
	/* Read interrupt register */
	ir = h->instance->IR;
	
	/* Check for RX FIFO 0 new message */
	if ((ir & FDCAN_IR_RF0N) != 0U) {
		while (((h->instance->RXF0S & FDCAN_RXF0S_F0FL) >> FDCAN_RXF0S_F0FL_Pos) != 0U) {
			uint32_t *msg_ram;
			uint32_t get_index;
			uint32_t header0;
			uint32_t header1;
			uint32_t i;

			get_index = (h->instance->RXF0S & FDCAN_RXF0S_F0GI) >> FDCAN_RXF0S_F0GI_Pos;
			msg_ram = (uint32_t *)(FDCAN1_MSG_RAM_BASE +
			          (FDCAN_RX_FIFO0_OFFSET + (get_index * FDCAN_RX_FIFO0_ELEM_SIZE)) * 4U);

			header0 = msg_ram[0];
			header1 = msg_ram[1];

			if ((header0 & FDCAN_RX_HEADER_XTD) != 0U) {
				frame.id = (header0 & FDCAN_RX_HEADER_XTD_MASK) >> FDCAN_RX_HEADER_XTD_Pos;
				frame.extended = 1U;
			} else {
				frame.id = (header0 & FDCAN_RX_HEADER_ID_MASK) >> FDCAN_RX_HEADER_ID_Pos;
				frame.extended = 0U;
			}

			frame.rtr = ((header0 & FDCAN_RX_HEADER_RTR) != 0U) ? 1U : 0U;
			frame.dlc = (header1 & FDCAN_RX_HEADER_DLC_MASK) >> FDCAN_RX_HEADER_DLC_Pos;

			for (i = 0; i < ((frame.dlc + 3U) / 4U); i++) {
				uint32_t word = msg_ram[2 + i];
				frame.data[i * 4U + 0U] = (uint8_t)(word & 0xFFU);
				frame.data[i * 4U + 1U] = (uint8_t)((word >> 8) & 0xFFU);
				frame.data[i * 4U + 2U] = (uint8_t)((word >> 16) & 0xFFU);
				frame.data[i * 4U + 3U] = (uint8_t)((word >> 24) & 0xFFU);
			}

			h->instance->RXF0A = get_index;
			fdcan_push_rx_software(h, &frame);

			if (h->rx_callback != NULL) {
				h->rx_callback(&frame, h->rx_callback_user_data);
			}
		}

		h->instance->IR = FDCAN_IR_RF0N;
	}
	
	/* Check for RX FIFO 0 full */
	if ((ir & FDCAN_IR_RF0F) != 0U) {
		/* FIFO full - frames may be lost */
		h->instance->IR = FDCAN_IR_RF0F;
	}
	
	/* Check for RX FIFO 0 message lost */
	if ((ir & FDCAN_IR_RF0L) != 0U) {
		/* Message lost due to overflow */
		h->instance->IR = FDCAN_IR_RF0L;
	}
	
	/* Check for error interrupts - invoke callback for immediate fault handling */
	if ((ir & FDCAN_IR_BO) != 0U) {
		h->instance->IR = FDCAN_IR_BO;
		if (h->error_callback != NULL) {
			h->error_callback(FDCAN_ERR_BUS_OFF, h->error_callback_user_data);
		}
	}
	
	if ((ir & FDCAN_IR_EP) != 0U) {
		h->instance->IR = FDCAN_IR_EP;
		if (h->error_callback != NULL) {
			h->error_callback(FDCAN_ERR_ERROR_PASSIVE, h->error_callback_user_data);
		}
	}
	
	if ((ir & FDCAN_IR_EW) != 0U) {
		h->instance->IR = FDCAN_IR_EW;
		if (h->error_callback != NULL) {
			h->error_callback(FDCAN_ERR_ERROR_WARNING, h->error_callback_user_data);
		}
	}
	
	if ((ir & FDCAN_IR_PEA) != 0U) {
		h->instance->IR = FDCAN_IR_PEA;
		if (h->error_callback != NULL) {
			h->error_callback(FDCAN_ERR_PROTOCOL, h->error_callback_user_data);
		}
	}
	
	if ((ir & FDCAN_IR_PED) != 0U) {
		h->instance->IR = FDCAN_IR_PED;
		if (h->error_callback != NULL) {
			h->error_callback(FDCAN_ERR_PROTOCOL, h->error_callback_user_data);
		}
	}
}

/*
 * fdcan_get_error_counters - Get transmit and receive error counters
 */
status_t
fdcan_get_error_counters(const struct fdcan_handle *h, uint8_t *tec, uint8_t *rec)
{
	uint32_t ecr;
	
	if (h == NULL || tec == NULL || rec == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}
	
	if (h->initialized == 0U) {
		return STATUS_ERROR_NOT_INITIALIZED;
	}
	
	ecr = h->instance->ECR;
	*tec = (uint8_t)((ecr >> 0) & 0xFFU);   /* TEC in bits [7:0] */
	*rec = (uint8_t)((ecr >> 8) & 0x7FU);   /* REC in bits [14:8], max 127 */
	
	return STATUS_OK;
}

/*
 * fdcan_get_protocol_status - Get protocol status register
 */
status_t
fdcan_get_protocol_status(const struct fdcan_handle *h, uint32_t *psr)
{
	if (h == NULL || psr == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}
	
	if (h->initialized == 0U) {
		return STATUS_ERROR_NOT_INITIALIZED;
	}
	
	*psr = h->instance->PSR;
	
	return STATUS_OK;
}

/*
 * fdcan_set_std_range_filter - Configure standard ID range filter
 *
 * Configures hardware to accept only CAN IDs in [id_start, id_end].
 * Rejects all non-matching standard and extended IDs in hardware.
 * This eliminates unnecessary RX interrupts for irrelevant frames.
 */
status_t
fdcan_set_std_range_filter(struct fdcan_handle *h, uint16_t id_start,
                           uint16_t id_end, uint8_t fifo)
{
	uint32_t *filter_ram;
	uint32_t i;
	uint32_t fifo_bits;

	if (h == NULL || id_start > 0x7FFU || id_end > 0x7FFU || id_start > id_end) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	if (h->initialized == 0U) {
		return STATUS_ERROR_NOT_INITIALIZED;
	}

	/* Configure standard ID filter 0: range [id_start, id_end] to FIFO0/1 */
	filter_ram = (uint32_t *)(FDCAN1_MSG_RAM_BASE + 
	                          (FDCAN_STD_FILTER_OFFSET * 4U));

	fifo_bits = (fifo != 0U) ? 0x2U : 0x1U;  /* FIFO1=0x2, FIFO0=0x1 */

	/* Filter 0: Range filter sending to specified FIFO */
	filter_ram[0] = (0x0U << 30U) |           /* Type: Range */
	                (fifo_bits << 27U) |       /* Config: to FIFO0/1 */
	                ((uint32_t)id_start << 16U) |  /* ID1: range start */
	                ((uint32_t)id_end);        /* ID2: range end */

	/* Disable all other standard filters */
	for (i = 1U; i < FDCAN_STD_FILTER_COUNT; i++) {
		filter_ram[i] = 0U;
	}

	/* Reject non-matching standard and extended IDs (no FIFO, no interrupt) */
	h->instance->GFC = 0U;  /* ANFS=00 (reject), ANFE=00 (reject) */

	return STATUS_OK;
}
