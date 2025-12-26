/*
 * mpu_config.c - Memory Protection Unit configuration
 *
 * Configures memory regions to ensure cache coherency for DMA operations.
 * Specifically targets the D2 SRAM (0x30000000) used for Ethernet buffers.
 *
 * WHY: Prevents Bus Faults in Ethernet DMA by making buffers non-cacheable.
 * The STM32H7 D-Cache can become out of sync with DMA writes to RAM, causing
 * SCB_InvalidateDCache_by_Addr to fail if called with corrupted length values.
 * By configuring D2 SRAM as non-cacheable, we eliminate the need for manual
 * cache maintenance, fixing the root cause of firmware freezes ~30s after boot.
 */

#include "stm32h7xx_hal.h"

#include "board.h"

/*
 * MPU_Config - Configure the MPU attributes
 *
 * Sets up D2 SRAM as non-cacheable for DMA operations.
 */
void
MPU_Config(void)
{
	MPU_Region_InitTypeDef MPU_InitStruct = {0};

	/* Disables the MPU */
	HAL_MPU_Disable();

	/*
	 * Configure the MPU attributes as Device not cacheable
	 * for ETH DMA descriptors and buffers in D2 SRAM.
	 * RAM_D2 starts at 0x30000000. Size is 288KB.
	 * We use 512KB region to cover it all.
	 */
	MPU_InitStruct.Enable = MPU_REGION_ENABLE;
	MPU_InitStruct.Number = MPU_REGION_NUMBER0;
	MPU_InitStruct.BaseAddress = 0x30000000;
	MPU_InitStruct.Size = MPU_REGION_SIZE_512KB;
	MPU_InitStruct.SubRegionDisable = 0x00;
	MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL1;
	MPU_InitStruct.AccessPermission = MPU_REGION_FULL_ACCESS;
	MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;
	MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
	MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
	MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

	HAL_MPU_ConfigRegion(&MPU_InitStruct);

	/* Enables the MPU */
	HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);
}
