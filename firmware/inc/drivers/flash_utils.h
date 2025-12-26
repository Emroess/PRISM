/*
 * flash_utils.h - Flash Memory Utilities
 *
 * Shared flash helper functions for NVM operations.
 * Extracted from valve_nvm.c and network_nvm.c to eliminate duplication.
 */

#ifndef FLASH_UTILS_H
#define FLASH_UTILS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "status.h"

/*
 * Get the flash bank number for an address.
 *
 * @param address: Flash address to check
 * @return: FLASH_BANK_1 or FLASH_BANK_2
 */
uint32_t flash_get_bank(uint32_t address);

/*
 * Get the sector number within a bank for an address.
 *
 * @param address: Flash address to check
 * @return: Sector number (0-7 for 128KB sectors)
 */
uint32_t flash_get_sector(uint32_t address);

/*
 * Erase a flash sector containing the given address.
 *
 * @param address: Address within the sector to erase
 * @return: STATUS_OK on success, error code on failure
 */
status_t flash_erase_sector(uint32_t address);

/*
 * Program data to flash memory.
 * Data must be 32-byte aligned for STM32H7 flash word programming.
 *
 * @param address: Flash address to program (must be 32-byte aligned)
 * @param data: Source data buffer (must be 32-byte aligned)
 * @param size: Size of data in bytes (must be multiple of 32)
 * @return: STATUS_OK on success, error code on failure
 */
status_t flash_program_data(uint32_t address, const void *data, size_t size);

/*
 * Calculate a simple XOR checksum over a data buffer.
 *
 * @param data: Pointer to data buffer
 * @param size: Size of data in bytes
 * @return: XOR checksum value
 */
uint32_t flash_calculate_checksum(const void *data, size_t size);

#endif /* FLASH_UTILS_H */
