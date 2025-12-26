/*
 * main.c - Main application entry point with CLI
 */

#include <string.h>

#include "stm32h7xx_hal_conf.h"

#include "app/cli.h"
#include "board.h"
#include "config/core.h"
#include "drivers/fdcan.h"
#include "drivers/uart.h"
#include "network/net_init.h"

/*
 * main - Application entry point
 */
int
main(void)
{
	struct uart_handle *uart;
	struct fdcan_handle *can;
	struct cli_context cli;
	struct uart_config uart_cfg;
	struct fdcan_config can_cfg;
	status_t status;
	uint32_t unique_id[3];

	/* Initialize board */
	status = board_init();
	if (status != STATUS_OK) {
		board_led_on(BOARD_LED_ERROR);
		board_panic(status, __FILE__, __LINE__);
	}

	/* Brief delay for clocks to stabilize */
	board_delay_ms(100);

	/* Get UART handle */
	uart = uart_get_handle();

	/* Initialize UART */
	uart_cfg.baudrate = BOARD_USART3_BAUDRATE;
	uart_cfg.kernel_hz = BOARD_USART3_KERNEL_HZ;

	status = uart_init(uart, &uart_cfg);
	if (status != STATUS_OK) {
		board_led_on(BOARD_LED_ERROR);
		board_panic(status, __FILE__, __LINE__);
	}

	/* Print boot banner */
	uart_write_string(uart, "\r\n", 100);
	uart_write_string(uart, "========================================\r\n", 100);
	uart_write_string(uart, "STM32H753ZI Firmware\r\n", 100);
	uart_write_string(uart, "========================================\r\n", 100);
	uart_write_string(uart, "\r\n", 100);

	/* Display system information */
	board_get_unique_id(unique_id);
	uart_printf(uart, "System Clock:    %lu Hz\r\n",
	    (unsigned long)board_get_sysclk_hz());
	uart_printf(uart, "Device ID:       %08lX-%08lX-%08lX\r\n",
	    (unsigned long)unique_id[0],
	    (unsigned long)unique_id[1],
	    (unsigned long)unique_id[2]);
	uart_write_string(uart, "\r\n", 100);

	/* Initialize FDCAN */
	uart_write_string(uart, "Initializing FDCAN1...\r\n", 100);
	can = fdcan_get_handle();
	can_cfg.kernel_hz = BOARD_FDCAN_KERNEL_HZ;
	can_cfg.bitrate = BOARD_FDCAN1_BITRATE;
	can_cfg.sample_point_percent = BOARD_FDCAN1_SAMPLE_POINT_PERCENT;
	can_cfg.loopback_mode = FDCAN_MODE_NORMAL;

	status = fdcan_init(can, &can_cfg);
	if (status != STATUS_OK) {
		uart_printf(uart, "ERROR: FDCAN init failed (status=%d)\r\n",
		    (int)status);
		uart_write_string(uart, "\r\n", 100);
	} else {
		uart_write_string(uart, "FDCAN1 initialized successfully\r\n", 100);
		uart_printf(uart, "  Bit rate:      %lu bps\r\n",
		    (unsigned long)BOARD_FDCAN1_BITRATE);
		uart_printf(uart, "  Sample point:  %u%%\r\n",
		    (unsigned int)BOARD_FDCAN1_SAMPLE_POINT_PERCENT);
		uart_write_string(uart, "  Mode:          Normal\r\n", 100);
		uart_write_string(uart, "\r\n", 100);
	}

	/* Initialize Ethernet */
	uart_write_string(uart, "Initializing Ethernet...\r\n", 100);
	ethernet_init();
	uart_write_string(uart, "Ethernet initialized\r\n", 100);

	/* Boot complete - turn off boot LED */
	board_led_off(BOARD_LED_BOOT);

	/* Initialize CLI */
	uart_write_string(uart, "Type 'help' for available commands\r\n", 100);
	uart_write_string(uart, "\r\n", 100);

	status = cli_init(&cli, uart, can);
	if (status != STATUS_OK) {
		board_led_on(BOARD_LED_ERROR);
		board_panic(status, __FILE__, __LINE__);
	}

	/* Enter CLI loop (never returns) */
	cli_run(&cli);

	return 0;
}

/*
 * assert_failed - HAL assertion failure handler
 *
 * Called when configASSERT() macro fails in HAL code.
 */
void
assert_failed(const uint8_t *file, uint32_t line)
{
	board_panic(STATUS_ERROR, (const char *)file, (int)line);
}
