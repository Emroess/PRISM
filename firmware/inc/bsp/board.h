/*
 * board.h - Board Support Package API for NUCLEO-H753ZI
 *
 * This is the ONLY interface the application layer should use for
 * hardware initialization and control.
 *
 * Per IMPROVE_FIRMWARE_PLAN.md:
 * - board.c is the single authority for all hardware configuration
 * - Application layer MUST NOT include stm32h7xx.h directly
 * - All hardware access goes through this clean API
 *
 * Compliant with:
 * - MISRA-C:2012
 * - OpenBSD style guide (no typedefs for structs)
 * - DO-178C Level A requirements
 */

#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>

#include "config/board.h"
#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Board initialization and validation
 */

/*
 * board_init - Initialize all board hardware
 *
 * Configures:
 * - Power regulator (VOS1 for 400 MHz operation)
 * - Clock tree (PLL1, SYSCLK, AHB, APB dividers)
 * - Flash latency
 * - GPIO alternate functions for UART3 and FDCAN1
 * - MPU (if needed)
 * - SysTick timer
 *
 * Must be called before any peripheral drivers are initialized.
 *
 * Returns: STATUS_OK on success, error code on failure
 */
status_t board_init(void);

/**
 * @brief  Configure the MPU attributes
 * @param  None
 * @retval None
 *
 * WHY: Ensures memory safety for DMA operations by configuring D2 SRAM as non-cacheable.
 * Prevents Bus Faults from cache coherency issues in Ethernet DMA, fixing firmware freezes.
 */
void MPU_Config(void);

/*
 * board_validate - Verify hardware configuration is correct
 *
 * Checks:
 * - Clock frequencies match board_config.h
 * - Power regulator is in correct mode
 * - Critical GPIO pins are configured
 *
 * This is called by board_init() after configuration, but can also
 * be called by application code to verify system state.
 *
 * Returns: STATUS_OK if valid, error code if misconfigured
 */
status_t board_validate(void);

/*
 * board_panic - Critical error handler (never returns)
 *
 * Called when a critical invariant is violated or unrecoverable error occurs.
 * Per IMPROVE_FIRMWARE_PLAN.md section 0.5:
 * - Zero dependencies (inline register writes only)
 * - Disables interrupts immediately
 * - Turns on error LED
 * - Blinks error code pattern
 * - Sends message via UART if initialized
 *
 * @error_code: Status code indicating failure type
 * @file: Source file where panic was triggered (__FILE__)
 * @line: Line number where panic was triggered (__LINE__)
 */
void board_panic(status_t error_code, const char *file, int line)
	__attribute__((noreturn));

/*
 * System timing functions
 */

/*
 * board_get_systick_ms - Get millisecond counter since boot
 *
 * Returns: Milliseconds since board_init() was called
 */
uint32_t board_get_systick_ms(void);

/*
 * board_delay_ms - Blocking delay in milliseconds
 *
 * Uses SysTick timer for accurate timing.
 * Safe to call from application code.
 *
 * @ms: Number of milliseconds to delay
 */
void board_delay_ms(uint32_t ms);

/*
 * LED control (for diagnostics and status indication)
 */

enum board_led {
	BOARD_LED_BOOT   = 0,  /* Green LED (PB0) */
	BOARD_LED_ERROR  = 1,  /* Yellow LED (PB7) */
	BOARD_LED_STATUS = 2,  /* Red LED (PB14) */
};

/*
 * board_led_on - Turn on specified LED
 */
void board_led_on(enum board_led led);

/*
 * board_led_off - Turn off specified LED
 */
void board_led_off(enum board_led led);

/*
 * board_led_toggle - Toggle specified LED
 */
void board_led_toggle(enum board_led led);

/*
 * System information
 */

/*
 * board_get_sysclk_hz - Get current system clock frequency
 *
 * Returns: SYSCLK frequency in Hz (should be 400000000)
 */
uint32_t board_get_sysclk_hz(void);

/*
 * board_get_unique_id - Get STM32 unique device ID
 *
 * Reads the 96-bit unique ID from device.
 */
void board_get_unique_id(uint32_t[3]);

#ifdef __cplusplus
}
#endif

#endif /* BOARD_H */
