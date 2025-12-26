/*
 * valve_presets.c - Simplified preset-based valve configuration
 */

#include <stddef.h>
#include <string.h>

#include "valve_nvm.h"
#include "valve_presets.h"

/* Global preset parameters loaded from NVM */
struct preset_params preset_params[VALVE_PRESET_COUNT];

static void
load_default_presets(void)
{
	const struct preset_params *defaults;

	defaults = valve_nvm_get_default_presets();
	memcpy(preset_params, defaults, sizeof(preset_params));
}

/* Initialize presets from NVM */
status_t
valve_presets_init(void)
{
	status_t status;

	status = valve_nvm_load_presets(preset_params);
	if (status == STATUS_OK)
		return STATUS_OK;

	/* Attempt to rebuild NVM contents and try again */
	status = valve_nvm_init();
	if (status == STATUS_OK) {
		status = valve_nvm_load_presets(preset_params);
		if (status == STATUS_OK)
			return STATUS_OK;
	}

	load_default_presets();
	return STATUS_OK;
}

/* Save presets to NVM */
status_t
valve_presets_save(const struct preset_params new_presets[VALVE_PRESET_COUNT])
{
	uint32_t i;

	for (i = 0; i < VALVE_PRESET_COUNT; i++) {
		preset_params[i] = new_presets[i];
		preset_params[i].name[sizeof(preset_params[i].name) - 1U] = '\0';
	}
	return valve_nvm_save_presets(preset_params);
}

/* Get a single preset */
status_t
valve_preset_get(int index, struct preset_params *out)
{
	if (index < 0 || (uint32_t)index >= VALVE_PRESET_COUNT)
		return STATUS_ERROR_INVALID_PARAM;

	*out = preset_params[index];
	return STATUS_OK;
}

/* Set a single preset and save to NVM */
status_t
valve_preset_set(int index, const struct preset_params *in)
{
	if (index < 0 || (uint32_t)index >= VALVE_PRESET_COUNT)
		return STATUS_ERROR_INVALID_PARAM;

	preset_params[index] = *in;
	preset_params[index].name[sizeof(preset_params[index].name) - 1U] = '\0';
	return valve_nvm_save_presets(preset_params);
}

/*
 * valve_preset_from_preset - Generate valve config from preset
 *
 * Maps preset index and travel range to physics parameters.
 */
status_t
valve_preset_from_preset(int preset, float travel_degrees, struct valve_config *cfg)
{
	const struct preset_params *params;

	if (cfg == NULL)
		return STATUS_ERROR_INVALID_PARAM;

	if (preset < 0 || (unsigned int)preset >= VALVE_PRESET_COUNT)
		return STATUS_ERROR_INVALID_PARAM;

	params = &preset_params[preset];

	/* Use default travel range if not specified */
	if (travel_degrees <= 0.0f)
		travel_degrees = params->default_travel_deg;

	/* Validate travel range */
	if (travel_degrees <= 0.0f || travel_degrees > VALVE_MAX_POSITION_DEG)
		return STATUS_ERROR_INVALID_PARAM;

	/* Build configuration from feel parameters and travel range */
	cfg->closed_position_deg = 0.0f;
	cfg->open_position_deg = travel_degrees;
	cfg->degrees_per_turn = VALVE_DEFAULT_DEGREES_PER_TURN;
	cfg->torque_limit_nm = params->torque_limit_nm;
	cfg->hil_tau_max_limit_nm = params->torque_limit_nm;

	/* Set HIL physics parameters (exact duplicate of hil_valve_model.py) */
	cfg->hil_b_viscous_nm_s_per_rad = params->hil_b_viscous_nm_s_per_rad;
	cfg->hil_tau_c_coulomb_nm = params->hil_tau_c_coulomb_nm;
	cfg->hil_k_w_wall_stiffness_nm_per_turn = params->hil_k_w_wall_stiffness_nm_per_turn;
	cfg->hil_c_w_wall_damping_nm_s_per_turn = params->hil_c_w_wall_damping_nm_s_per_turn;
	cfg->hil_eps_smoothing = params->hil_eps_smoothing;
	cfg->hil_tau_max_limit_nm = params->torque_limit_nm;

	return STATUS_OK;
}

/*
 * Validate preset configuration for physical reasonableness.
 */
status_t
valve_preset_validate(const struct valve_config *cfg)
{
	if (cfg == NULL)
		return STATUS_ERROR_INVALID_CONFIG;

	/* Position limit validation */
	if (cfg->closed_position_deg >= cfg->open_position_deg)
		return STATUS_ERROR_INVALID_CONFIG;

	if (cfg->open_position_deg - cfg->closed_position_deg > VALVE_MAX_POSITION_DEG)
		return STATUS_ERROR_INVALID_CONFIG;

	if (cfg->degrees_per_turn <= 0.0f ||
	    cfg->degrees_per_turn > VALVE_MAX_POSITION_DEG)
		return STATUS_ERROR_INVALID_CONFIG;

	/* Torque limit validation */
	if (cfg->hil_tau_max_limit_nm <= 0.0f ||
	    cfg->hil_tau_max_limit_nm > VALVE_MAX_TORQUE_LIMIT_NM)
		return STATUS_ERROR_INVALID_CONFIG;

	/* HIL physics parameter validation */
	if (cfg->hil_b_viscous_nm_s_per_rad < 0.0f)
		return STATUS_ERROR_INVALID_CONFIG;
	if (cfg->hil_tau_c_coulomb_nm < 0.0f)
		return STATUS_ERROR_INVALID_CONFIG;
	if (cfg->hil_k_w_wall_stiffness_nm_per_turn < 0.0f)
		return STATUS_ERROR_INVALID_CONFIG;
	if (cfg->hil_c_w_wall_damping_nm_s_per_turn < 0.0f)
		return STATUS_ERROR_INVALID_CONFIG;
	if (cfg->hil_eps_smoothing <= 0.0f)
		return STATUS_ERROR_INVALID_CONFIG;

	return STATUS_OK;
}

/*
 * valve_preset_get_default_travel - Get default travel for a preset
 */
float
valve_preset_get_default_travel(int preset)
{
	if (preset < 0 || (unsigned int)preset >= VALVE_PRESET_COUNT)
		return 0.0f;
	return preset_params[preset].default_travel_deg;
}

/*
 * Captures the current valve configuration as a named preset.
 * Users can tune parameters in real-time, then save them so
 * the settings survive power cycles and firmware updates.
 */
status_t
valve_preset_save_current(struct valve_context *ctx, uint8_t preset_idx)
{
	struct preset_params p;
	struct valve_config *cfg;

	if (ctx == NULL || preset_idx >= VALVE_PRESET_COUNT)
		return STATUS_ERROR_INVALID_PARAM;

	cfg = &ctx->config;
	
	/* Build preset from current config */
	p.torque_limit_nm = cfg->torque_limit_nm;
	p.default_travel_deg = cfg->open_position_deg - cfg->closed_position_deg;
	p.hil_b_viscous_nm_s_per_rad = cfg->hil_b_viscous_nm_s_per_rad;
	p.hil_tau_c_coulomb_nm = cfg->hil_tau_c_coulomb_nm;
	p.hil_k_w_wall_stiffness_nm_per_turn =
	    cfg->hil_k_w_wall_stiffness_nm_per_turn;
	p.hil_c_w_wall_damping_nm_s_per_turn =
	    cfg->hil_c_w_wall_damping_nm_s_per_turn;
	p.hil_eps_smoothing = cfg->hil_eps_smoothing;
	
	/* Keep existing name */
	memcpy(p.name, preset_params[preset_idx].name, sizeof(p.name));
	
	return valve_preset_set(preset_idx, &p);
}

