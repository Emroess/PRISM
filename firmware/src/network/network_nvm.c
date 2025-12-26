/*
 * network_nvm.c - Network configuration NVM storage
 */

#include <string.h>

#include "stm32h7xx_hal.h"

#include "config/nvm.h"
#include "drivers/flash_utils.h"
#include "network/nvm.h"

/* CRC32 polynomial (standard) - kept local as network uses different algorithm */
#define CRC32_POLY	0xEDB88320

uint32_t
network_nvm_calculate_checksum(const struct network_nvm_config *config)
{
	uint32_t checksum;
	const uint8_t *ptr;
	size_t len;
	size_t i;

	if (config == NULL)
		return 0;

	checksum = 0;
	ptr = (const uint8_t *)config;
	len = sizeof(*config) - sizeof(config->checksum);

	for (i = 0; i < len; i++)
		checksum ^= ptr[i];

	return checksum;
}

/* Flash helper utilities - now use shared functions from flash_utils.h */

/* Erase NVM sector */
static bool
erase_nvm_sector(void)
{
	return (flash_erase_sector(NETWORK_CONFIG_ADDR) == STATUS_OK);
}

/* Program network config to flash */
static bool
program_network_config(const struct network_nvm_config *config)
{
	/* Use 96 bytes to cover 76-byte struct + padding (3 flash words) */
	return (flash_program_data(NETWORK_CONFIG_ADDR, config, 96U) == STATUS_OK);
}

bool
network_nvm_save(const struct network_nvm_config *config)
{
	__attribute__((aligned(32))) uint8_t buffer[96] = {0};
	struct network_nvm_config *config_to_save;

	if (config == NULL)
		return false;

	/* Prepare config with checksum */
	config_to_save = (struct network_nvm_config *)buffer;
	*config_to_save = *config;
	config_to_save->checksum = network_nvm_calculate_checksum(config_to_save);

	/* Erase sector */
	if (!erase_nvm_sector())
		return false;

	/* Program data */
	if (!program_network_config(config_to_save))
		return false;

	return true;
}

bool
network_nvm_load(struct network_nvm_config *config)
{
	const struct network_nvm_config *flash_config;
	uint32_t expected_checksum;

	if (config == NULL)
		return false;

	/* Read from flash - simple memcpy, no complex operations at boot */
	flash_config = (const struct network_nvm_config *)NETWORK_CONFIG_ADDR;

	/* Quick validation before copying */
	if (flash_config->magic != NETWORK_NVM_MAGIC)
		return false;

	/* Copy data */
	memcpy(config, flash_config, sizeof(*config));

	/* Ensure strings are null-terminated */
	config->ip_addr[sizeof(config->ip_addr) - 1] = '\0';
	config->netmask[sizeof(config->netmask) - 1] = '\0';
	config->gateway[sizeof(config->gateway) - 1] = '\0';

	/* Validate version and checksum */
	if (config->version != NETWORK_NVM_VERSION)
		return false;

	expected_checksum = network_nvm_calculate_checksum(config);
	if (expected_checksum != config->checksum)
		return false;

	return true;
}
