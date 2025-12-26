/*
 * system_manager.h - System-level service API
 *
 * Centralized system control operations
 * Used by CLI and HTTP API
 */

#ifndef SYSTEM_MANAGER_H
#define SYSTEM_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Hard fault information structure
 * Captured by exception handler, retrieved via system_get_fault_info()
 */
struct hard_fault_info {
	uint32_t r0, r1, r2, r3;
	uint32_t r12;
	uint32_t lr;     /* Link register */
	uint32_t pc;     /* Program counter at fault */
	uint32_t psr;    /* Program status register */
	uint32_t cfsr;   /* Configurable Fault Status Register */
	uint32_t hfsr;   /* Hard Fault Status Register */
	uint32_t dfsr;   /* Debug Fault Status Register */
	uint32_t afsr;   /* Auxiliary Fault Status Register */
	uint32_t mmar;   /* MemManage Fault Address Register */
	uint32_t bfar;   /* Bus Fault Address Register */
	bool valid;      /* true if fault data is valid */
};

/*
 * Reboot the system
 * Does not return
 */
void system_reboot(void) __attribute__((noreturn));

/*
 * Enter DFU (Device Firmware Update) mode
 * Does not return
 */
void system_enter_dfu(void) __attribute__((noreturn));

/*
 * Get last captured hard fault information
 *
 * Returns: STATUS_OK if fault info available, STATUS_ERROR_NOT_INITIALIZED if none
 * info: Output structure to populate
 */
status_t system_get_fault_info(struct hard_fault_info *);

#ifdef __cplusplus
}
#endif

#endif /* SYSTEM_MANAGER_H */
