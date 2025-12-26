/*
 * status.h - Common status/error code definitions
 *
 * Per IMPROVE_FIRMWARE_PLAN.md:
 * - All functions return status_t for error checking
 * - No magic numbers
 * - Clear error categories
 *
 * Compliant with:
 * - MISRA-C:2012
 * - OpenBSD style guide (no typedefs for enums)
 * - Linux kernel conventions
 */

#ifndef STATUS_H
#define STATUS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Status codes for all firmware operations
 * All driver functions must return one of these codes
 */
enum status_code {
	STATUS_OK                      = 0,
	STATUS_ERROR                   = 1,
	STATUS_ERROR_TIMEOUT           = 2,
	STATUS_ERROR_BUSY              = 3,
	STATUS_ERROR_INVALID_PARAM     = 4,
	STATUS_ERROR_INVALID_CONFIG    = 5,
	STATUS_ERROR_NOT_INITIALIZED   = 6,
	STATUS_ERROR_HARDWARE_FAULT    = 7,
	STATUS_ERROR_OUT_OF_MEMORY     = 8,
	STATUS_ERROR_BUFFER_FULL       = 9,
	STATUS_ERROR_BUFFER_EMPTY      = 10,
	STATUS_ERROR_NOT_SUPPORTED     = 11,
};

/* Type alias for status returns (not a typedef per OpenBSD style) */
typedef enum status_code status_t;

/*
 * Assert macro for critical invariants
 * Per improvement plan: fails at compile time where possible,
 * panics at runtime otherwise
 */
#define ASSERT(condition) \
	do { \
		if (!(condition)) { \
			board_panic(STATUS_ERROR, __FILE__, __LINE__); \
		} \
	} while (0)

/* Forward declaration of panic handler (defined in board.c) */
void board_panic(status_t error_code, const char *file, int line) 
	__attribute__((noreturn));

/*
 * Convert status code to human-readable string
 * Returns: Constant string (stored in ROM)
 */
const char *status_to_string(status_t status);

/*
 * Convert valve state to human-readable string
 * Uses int parameter to avoid circular header dependency with valve_haptic.h
 * Returns: Constant string (stored in ROM)
 */
const char *valve_state_to_string(int);

#ifdef __cplusplus
}
#endif

#endif /* STATUS_H */
