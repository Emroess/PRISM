/*
 * valve_nvm.c
 *
 * Non-volatile memory management for valve presets using STM32H7 flash.
 * Stores user-editable presets persistently across reboots.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "stm32h7xx_hal.h"

#include "config/nvm.h"
#include "drivers/flash_utils.h"
#include "valve_nvm.h"

/* NVM data structure - uses defines from nvm_config.h */
struct nvm_header {
	uint32_t version;
	uint32_t checksum;
	uint32_t write_count;	/* For wear leveling tracking */
};

struct nvm_data {
	struct nvm_header header;
	struct preset_params presets[VALVE_NVM_PRESET_COUNT];
};

/* Size of NVM data for flash operations */
#define NVM_DATA_SIZE sizeof(struct nvm_data)

static const struct preset_params default_presets[VALVE_NVM_PRESET_COUNT] = {
    /* VALVE_PRESET_LIGHT */
    {
        .name = "Light",
        .torque_limit_nm = 8.0f,
        .default_travel_deg = 90.0f,
        .hil_b_viscous_nm_s_per_rad = 0.02f,
        .hil_tau_c_coulomb_nm = 0.06f,
        .hil_k_w_wall_stiffness_nm_per_turn = 10.0f,
        .hil_c_w_wall_damping_nm_s_per_turn = 0.1f,
        .hil_eps_smoothing = 1e-3f
    },
    /* VALVE_PRESET_MEDIUM */
    {
        .name = "Medium",
        .torque_limit_nm = 8.0f,
        .default_travel_deg = 90.0f,
        .hil_b_viscous_nm_s_per_rad = 0.04f,
        .hil_tau_c_coulomb_nm = 0.12f,
        .hil_k_w_wall_stiffness_nm_per_turn = 15.0f,
        .hil_c_w_wall_damping_nm_s_per_turn = 0.2f,
        .hil_eps_smoothing = 1e-3f
    },
    /* VALVE_PRESET_HEAVY */
    {
        .name = "Heavy",
        .torque_limit_nm = 6.0f,
        .default_travel_deg = 360.0f,
        .hil_b_viscous_nm_s_per_rad = 0.08f,
        .hil_tau_c_coulomb_nm = 0.25f,
        .hil_k_w_wall_stiffness_nm_per_turn = 25.0f,
        .hil_c_w_wall_damping_nm_s_per_turn = 0.4f,
        .hil_eps_smoothing = 1e-3f
    },
    /* VALVE_PRESET_INDUSTRIAL */
    {
        .name = "Industrial",
        .torque_limit_nm = 8.0f,
        .default_travel_deg = 360.0f,
        .hil_b_viscous_nm_s_per_rad = 0.12f,
        .hil_tau_c_coulomb_nm = 0.40f,
        .hil_k_w_wall_stiffness_nm_per_turn = 35.0f,
        .hil_c_w_wall_damping_nm_s_per_turn = 0.6f,
        .hil_eps_smoothing = 1e-3f
    }
};

/* Flash address now defined in nvm_config.h as VALVE_NVM_FLASH_ADDR */

/* CRC calculation for validation */
static uint32_t
calculate_checksum(const struct nvm_data *data)
{
	return flash_calculate_checksum(data, sizeof(struct nvm_data));
}

static void
set_checksum(struct nvm_data *data)
{
	data->header.checksum = 0U;
	data->header.checksum = calculate_checksum(data);
}

/* Flash helper utilities - now use shared functions from flash_utils.h */

/* Erase NVM sector */
static status_t
erase_nvm_sector(void)
{
	return flash_erase_sector(VALVE_NVM_FLASH_ADDR);
}

/* Program NVM data to flash */
static status_t
program_nvm_data(const struct nvm_data *data)
{
	size_t aligned_size;

	/* Round up to multiple of 32 bytes for flash word alignment */
	aligned_size = ((NVM_DATA_SIZE + 31U) / 32U) * 32U;
	return flash_program_data(VALVE_NVM_FLASH_ADDR, data, aligned_size);
}

/* Load NVM data from flash */
static status_t
load_nvm_data(struct nvm_data *data)
{
	uint32_t stored_checksum;
	uint32_t calculated_checksum;

	memcpy(data, (const void *)VALVE_NVM_FLASH_ADDR, NVM_DATA_SIZE);

	/* Validate version and checksum */
	if (data->header.version != VALVE_NVM_VERSION)
		return STATUS_ERROR;

	stored_checksum = data->header.checksum;
	data->header.checksum = 0U;
	calculated_checksum = calculate_checksum(data);
	data->header.checksum = stored_checksum;

	if (calculated_checksum != stored_checksum)
		return STATUS_ERROR;

	return STATUS_OK;
}

/* Initialize NVM with default presets if invalid */
status_t
valve_nvm_init(void)
{
    __attribute__((aligned(32))) struct nvm_data data;

    if (load_nvm_data(&data) == STATUS_OK)
        return STATUS_OK;

    /* Initialize with default presets */
    data.header.version = VALVE_NVM_VERSION;
    data.header.write_count = 0;

    memcpy(data.presets, default_presets, sizeof(default_presets));

    /* Calculate checksum */
    set_checksum(&data);

    /* Erase and program */
    if (erase_nvm_sector() != STATUS_OK)
        return STATUS_ERROR;

    if (program_nvm_data(&data) != STATUS_OK)
        return STATUS_ERROR;

    return STATUS_OK;
}

/* Load presets from NVM */
status_t
valve_nvm_load_presets(struct preset_params presets_out[VALVE_NVM_PRESET_COUNT])
{
    struct nvm_data data;

    if (load_nvm_data(&data) != STATUS_OK)
        return STATUS_ERROR;

    memcpy(presets_out, data.presets, sizeof(data.presets));
    return STATUS_OK;
}

const struct preset_params *valve_nvm_get_default_presets(void)
{
    return default_presets;
}

/* Save presets to NVM */
status_t
valve_nvm_save_presets(const struct preset_params presets_in[VALVE_NVM_PRESET_COUNT])
{
	__attribute__((aligned(32))) struct nvm_data data;

	/* Load current data to preserve header */
	if (load_nvm_data(&data) != STATUS_OK) {
		/* If load fails, reinitialize header */
		data.header.version = VALVE_NVM_VERSION;
		data.header.write_count = 0;
	}

	data.header.write_count++;

	memcpy(data.presets, presets_in, sizeof(data.presets));

	/* Recalculate checksum */
	set_checksum(&data);

	/* Erase and program */
	if (erase_nvm_sector() != STATUS_OK)
		return STATUS_ERROR;

	if (program_nvm_data(&data) != STATUS_OK)
		return STATUS_ERROR;

	return STATUS_OK;
}
