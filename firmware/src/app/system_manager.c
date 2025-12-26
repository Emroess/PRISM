/*
 * system_manager.c - System-level service implementation
 *
 * Provides system control operations (reboot, DFU, fault info)
 */

#include <string.h>

#include "stm32h7xx.h"
#include "system_manager.h"

extern volatile struct hard_fault_info g_hard_fault_info;

void
system_reboot(void)
{
	NVIC_SystemReset();
	/* Never reached */
	while (1)
		;
}

void
system_enter_dfu(void)
{
	/* Set magic value for bootloader */
	*((uint32_t *)0x2000FFF0) = 0xDEADBEEF;
	
	NVIC_SystemReset();
	/* Never reached */
	while (1)
		;
}

status_t
system_get_fault_info(struct hard_fault_info *info)
{
	if (info == NULL)
		return STATUS_ERROR_INVALID_PARAM;
	
	if (!g_hard_fault_info.valid)
		return STATUS_ERROR_NOT_INITIALIZED;
	
	/* Copy field by field to avoid volatile cast warning */
	info->r0 = g_hard_fault_info.r0;
	info->r1 = g_hard_fault_info.r1;
	info->r2 = g_hard_fault_info.r2;
	info->r3 = g_hard_fault_info.r3;
	info->r12 = g_hard_fault_info.r12;
	info->lr = g_hard_fault_info.lr;
	info->pc = g_hard_fault_info.pc;
	info->psr = g_hard_fault_info.psr;
	info->cfsr = g_hard_fault_info.cfsr;
	info->hfsr = g_hard_fault_info.hfsr;
	info->dfsr = g_hard_fault_info.dfsr;
	info->afsr = g_hard_fault_info.afsr;
	info->mmar = g_hard_fault_info.mmar;
	info->bfar = g_hard_fault_info.bfar;
	info->valid = g_hard_fault_info.valid;
	
	return STATUS_OK;
}
