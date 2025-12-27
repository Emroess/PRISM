/*
 * config/nvm.h - NVM Configuration Defines
 *
 * SINGLE SOURCE OF TRUTH for all NVM-related configuration.
 * Consolidates defines previously scattered across valve_nvm.c,
 * network_nvm.h, and valve_presets.h.
 *
 * Flash Memory Layout (STM32H753, Bank 2):
 * - NVM (valve presets):    0x081C0000, 64KB
 * - NETWORK_CONFIG:         0x081A0000, 64KB
 *
 * Compliant with:
 * - MISRA-C:2012
 * - OpenBSD style guide
 */

#ifndef CONFIG_NVM_H
#define CONFIG_NVM_H

#include <stdint.h>

/*
 * ===========================================================================
 * Flash Sector Configuration (STM32H753)
 * ===========================================================================
 */
#define NVM_FLASH_SECTOR_SIZE       0x20000U  /* 128 KB sectors in bank 2 */

/*
 * ===========================================================================
 * Valve Preset NVM Configuration
 * ===========================================================================
 */
#define VALVE_NVM_MAGIC             0x56414C56U  /* "VALV" as ASCII */
#define VALVE_NVM_VERSION           2U
#define VALVE_NVM_PRESET_COUNT      4U

/* Linker symbol for valve NVM region */
extern uint8_t __nvm_data_start[];
#define VALVE_NVM_FLASH_ADDR        ((uint32_t)&__nvm_data_start[0])

/*
 * ===========================================================================
 * Encoder Zero Calibration NVM Configuration
 * ===========================================================================
 */
#define ZERO_CAL_NVM_MAGIC          0x5A45524FU  /* "ZERO" as ASCII */
#define ZERO_CAL_NVM_VERSION        1U

/* Calibration data stored after valve presets (offset 4KB into NVM region) */
#define ZERO_CAL_NVM_OFFSET         0x1000U
#define ZERO_CAL_NVM_FLASH_ADDR     (VALVE_NVM_FLASH_ADDR + ZERO_CAL_NVM_OFFSET)

/*
 * ===========================================================================
 * Network Config NVM Configuration
 * ===========================================================================
 */
#define NETWORK_NVM_MAGIC           0x4E455457U  /* "NETW" as ASCII */
#define NETWORK_NVM_VERSION         1U

/* Linker symbol for network config region */
extern uint8_t __network_config_start[];
#define NETWORK_NVM_FLASH_ADDR      ((uint32_t)&__network_config_start[0])

/*
 * ===========================================================================
 * Compile-time Validation
 * ===========================================================================
 */
_Static_assert(VALVE_NVM_MAGIC != NETWORK_NVM_MAGIC,
               "NVM magic numbers must be unique");
_Static_assert(VALVE_NVM_PRESET_COUNT > 0U,
               "VALVE_NVM_PRESET_COUNT must be at least 1");

#endif /* CONFIG_NVM_H */
