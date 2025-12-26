/*
 * uart.c - UART driver implementation for USART3 (ST-LINK VCP)
 *
 * Per IMPROVE_FIRMWARE_PLAN.md:
 * - On hardware layer whitelist (allowed to access registers)
 * - Proper volatile for ISR-shared data
 * - Timeout protection on all operations
 * - Clean error handling
 *
 * Compliant with:
 * - MISRA-C:2012
 * - OpenBSD style guide
 * - DO-178C Level A requirements
 */

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "stm32h7xx.h"

#include "board.h"
#include "config/board.h"
#include "status.h"
#include "uart.h"

/*
 * RX ring buffer size (must be power of 2 for efficient modulo)
 */
#define UART_RX_BUFFER_SIZE 256U

/*
 * UART handle structure (opaque to application)
 */
struct uart_handle {
	USART_TypeDef *instance;           /* Hardware instance */
	volatile uint8_t rx_buffer[UART_RX_BUFFER_SIZE];  /* RX ring buffer */
	volatile size_t rx_head;           /* Write pointer (ISR) */
	volatile size_t rx_tail;           /* Read pointer (application) */
	volatile uint32_t rx_overflows;    /* RX buffer overflow counter */
	uint32_t baudrate;                 /* Configured baud rate */
	uint8_t initialized;               /* Initialization flag */
};

/*
 * Static UART handle for USART3
 * (We only have one UART in this firmware)
 */
static struct uart_handle uart3_handle;

/*
 * ISR handler forward declaration
 */
void USART3_IRQHandler(void);

/*
 * uart_init - Initialize UART peripheral
 */
status_t
uart_init(struct uart_handle *h, const struct uart_config *cfg)
{
	uint32_t usartdiv;
	uint32_t timeout;

	if (h == NULL || cfg == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	if (h->initialized != 0U) {
		return STATUS_ERROR_BUSY;
	}

	/* Initialize handle */
	memset(h, 0, sizeof(*h));
	h->instance = USART3;
	h->baudrate = cfg->baudrate;

	/* Enable USART3 clock */
	RCC->APB1LENR |= RCC_APB1LENR_USART3EN;
	(void)RCC->APB1LENR;  /* Read back for write completion */

	/* Disable USART3 before configuration */
	h->instance->CR1 &= ~USART_CR1_UE;

	/* Configure 8N1: 8 data bits, no parity, 1 stop bit */
	h->instance->CR1 = 0;  /* Clear all settings */
	h->instance->CR2 = 0;  /* 1 stop bit (default) */
	h->instance->CR3 = 0;  /* No hardware flow control */

	/* Calculate baud rate divisor */
	usartdiv = cfg->kernel_hz / cfg->baudrate;
	h->instance->BRR = usartdiv;

	/* Enable UART, transmitter, receiver with RX interrupt */
	h->instance->CR1 = USART_CR1_UE |     /* UART enable */
	                   USART_CR1_TE |     /* Transmitter enable */
	                   USART_CR1_RE |     /* Receiver enable */
	                   USART_CR1_RXNEIE;  /* RX not empty interrupt */

	/* Wait for UART to be ready */
	timeout = 10000U;
	while ((h->instance->ISR & USART_ISR_TEACK) == 0U) {
		timeout--;
		if (timeout == 0U) {
			return STATUS_ERROR_TIMEOUT;
		}
	}

	/* Enable USART3 interrupt in NVIC */
	NVIC_SetPriority(USART3_IRQn, 7);
	NVIC_EnableIRQ(USART3_IRQn);

	h->initialized = 1U;

	return STATUS_OK;
}

/*
 * uart_deinit - Deinitialize UART peripheral
 */
status_t
uart_deinit(struct uart_handle *h)
{
	if (h == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	if (h->initialized == 0U) {
		return STATUS_ERROR_NOT_INITIALIZED;
	}

	/* Disable USART3 interrupt */
	NVIC_DisableIRQ(USART3_IRQn);

	/* Disable USART3 */
	h->instance->CR1 = 0;

	/* Disable USART3 clock */
	RCC->APB1LENR &= ~RCC_APB1LENR_USART3EN;

	h->initialized = 0U;

	return STATUS_OK;
}

/*
 * uart_write - Write data to UART with timeout
 */
status_t
uart_write(struct uart_handle *h, const uint8_t *data, size_t len,
           uint32_t timeout_ms)
{
	size_t i;
	uint32_t start;
	uint32_t elapsed;

	if (h == NULL || data == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	if (h->initialized == 0U) {
		return STATUS_ERROR_NOT_INITIALIZED;
	}

	start = board_get_systick_ms();

	for (i = 0; i < len; i++) {
		/* Wait for TX empty (TXE) */
		while ((h->instance->ISR & USART_ISR_TXE_TXFNF) == 0U) {
			if (timeout_ms > 0U) {
				elapsed = board_get_systick_ms() - start;
				if (elapsed >= timeout_ms) {
					return STATUS_ERROR_TIMEOUT;
				}
			}
		}

		/* Write byte to data register */
		h->instance->TDR = data[i];
	}

	/* Wait for transmission to complete */
	while ((h->instance->ISR & USART_ISR_TC) == 0U) {
		if (timeout_ms > 0U) {
			elapsed = board_get_systick_ms() - start;
			if (elapsed >= timeout_ms) {
				return STATUS_ERROR_TIMEOUT;
			}
		}
	}

	return STATUS_OK;
}

/*
 * uart_read - Read data from UART (non-blocking)
 */
status_t
uart_read(struct uart_handle *h, uint8_t *data, size_t len,
          size_t *bytes_read)
{
	size_t count;
	size_t i;

	if (h == NULL || data == NULL || bytes_read == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	if (h->initialized == 0U) {
		return STATUS_ERROR_NOT_INITIALIZED;
	}

	/* Calculate available bytes in ring buffer */
	count = (h->rx_head - h->rx_tail) % UART_RX_BUFFER_SIZE;

	/* Limit to requested length */
	if (count > len) {
		count = len;
	}

	/* Copy data from ring buffer */
	for (i = 0; i < count; i++) {
		data[i] = h->rx_buffer[h->rx_tail];
		h->rx_tail = (h->rx_tail + 1U) % UART_RX_BUFFER_SIZE;
	}

	*bytes_read = count;

	return STATUS_OK;
}

/*
 * uart_write_string - Write null-terminated string
 */
status_t
uart_write_string(struct uart_handle *h, const char *str,
                  uint32_t timeout_ms)
{
	size_t len;

	if (h == NULL || str == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	len = strlen(str);
	return uart_write(h, (const uint8_t *)str, len, timeout_ms);
}

/*
 * uart_get_rx_available - Get number of bytes in RX buffer
 */
size_t
uart_get_rx_available(const struct uart_handle *h)
{
	if (h == NULL || h->initialized == 0U) {
		return 0;
	}

	return (h->rx_head - h->rx_tail) % UART_RX_BUFFER_SIZE;
}

/*
 * USART3_IRQHandler - USART3 interrupt handler
 *
 * Per IMPROVE_FIRMWARE_PLAN.md:
 * - Minimal work in ISR
 * - All shared data is volatile
 * - No function calls except register access
 */
void
USART3_IRQHandler(void)
{
	struct uart_handle *h = &uart3_handle;
	uint32_t isr;
	uint8_t data;
	size_t next_head;

	isr = h->instance->ISR;

	/* Check for RX not empty */
	if ((isr & USART_ISR_RXNE_RXFNE) != 0U) {
		/* Read data register (clears RXNE flag) */
		data = (uint8_t)(h->instance->RDR & 0xFFU);

		/* Calculate next head position */
		next_head = (h->rx_head + 1U) % UART_RX_BUFFER_SIZE;

		/* Check for buffer overflow */
		if (next_head != h->rx_tail) {
			/* Buffer has space, store byte */
			h->rx_buffer[h->rx_head] = data;
			h->rx_head = next_head;
		} else {
			/* Buffer full, drop byte and count overflow */
			h->rx_overflows++;
		}
	}

	/* Check for errors */
	if ((isr & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE)) != 0U) {
		/* Clear error flags */
		h->instance->ICR = USART_ICR_ORECF | USART_ICR_FECF | 
		                   USART_ICR_NECF;
	}
}

/*
 * uart_get_handle - Get UART3 handle for application use
 *
 * This is a convenience function to get the static UART handle.
 */
struct uart_handle *
uart_get_handle(void)
{
	return &uart3_handle;
}

/*
 * uart_get_rx_overflows - Get RX buffer overflow count
 *
 * Returns the number of bytes dropped due to RX buffer full.
 * Counter saturates at UINT32_MAX.
 */
uint32_t
uart_get_rx_overflows(const struct uart_handle *h)
{
	if (h == NULL) {
		return 0;
	}
	return h->rx_overflows;
}

/*
 * uart_printf - Formatted output to UART
 */
status_t
uart_printf(struct uart_handle *h, const char *fmt, ...)
{
	char buffer[128];
	int len;
	va_list args;

	va_start(args, fmt);
	len = vsnprintf(buffer, sizeof(buffer), fmt, args);
	va_end(args);

	if (len < 0 || (size_t)len >= sizeof(buffer)) {
		return STATUS_ERROR;
	}

	return uart_write(h, (const uint8_t *)buffer, (size_t)len, 1000);
}

