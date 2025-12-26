/*
 * valve_manager.c - Valve service layer implementation
 *
 * Single source of truth for valve operations.
 * Used by both CLI and HTTP API to ensure consistent behavior.
 */

#include "config/valve.h"
#include "valve_manager.h"
#include "valve_presets.h"

status_t
valve_start(struct valve_context *ctx)
{
	return valve_haptic_start(ctx);
}

status_t
valve_stop(struct valve_context *ctx)
{
	valve_haptic_stop(ctx);
	return STATUS_OK;
}

status_t
valve_set_damping(struct valve_context *ctx, float value)
{
	struct valve_config temp_cfg = ctx->config;
	temp_cfg.hil_b_viscous_nm_s_per_rad = value;
	return valve_haptic_stage_config(ctx, &temp_cfg, CFG_FIELD_VISCOUS);
}

status_t
valve_set_friction(struct valve_context *ctx, float value)
{
	struct valve_config temp_cfg = ctx->config;
	temp_cfg.hil_tau_c_coulomb_nm = value;
	return valve_haptic_stage_config(ctx, &temp_cfg, CFG_FIELD_COULOMB);
}

status_t
valve_set_epsilon(struct valve_context *ctx, float value)
{
	struct valve_config temp_cfg = ctx->config;
	temp_cfg.hil_eps_smoothing = value;
	return valve_haptic_stage_config(ctx, &temp_cfg, CFG_FIELD_SMOOTHING);
}

status_t
valve_set_torque_limit(struct valve_context *ctx, float value)
{
	struct valve_config temp_cfg = ctx->config;
	temp_cfg.hil_tau_max_limit_nm = value;
	return valve_haptic_stage_config(ctx, &temp_cfg, CFG_FIELD_TORQUE_LIMIT);
}

status_t
valve_set_wall_damping(struct valve_context *ctx, float value)
{
	struct valve_config temp_cfg = ctx->config;
	temp_cfg.hil_c_w_wall_damping_nm_s_per_turn = value;
	return valve_haptic_stage_config(ctx, &temp_cfg, CFG_FIELD_WALL_DAMPING);
}

status_t
valve_set_wall_stiffness(struct valve_context *ctx, float value)
{
	struct valve_config temp_cfg = ctx->config;
	temp_cfg.hil_k_w_wall_stiffness_nm_per_turn = value;
	return valve_haptic_stage_config(ctx, &temp_cfg, CFG_FIELD_WALL_STIFFNESS);
}

status_t
valve_set_scale(struct valve_context *ctx, float degrees_per_turn)
{
	struct valve_config temp_cfg = ctx->config;
	temp_cfg.degrees_per_turn = degrees_per_turn;
	return valve_haptic_stage_config(ctx, &temp_cfg, CFG_FIELD_SCALE);
}

status_t
valve_set_open_position(struct valve_context *ctx, float degrees)
{
	struct valve_config temp_cfg = ctx->config;
	temp_cfg.open_position_deg = degrees;
	return valve_haptic_stage_config(ctx, &temp_cfg, CFG_FIELD_OPEN_POS);
}

status_t
valve_set_closed_position(struct valve_context *ctx, float degrees)
{
	struct valve_config temp_cfg = ctx->config;
	temp_cfg.closed_position_deg = degrees;
	return valve_haptic_stage_config(ctx, &temp_cfg, CFG_FIELD_CLOSED_POS);
}

status_t
valve_load_preset(struct valve_context *ctx, uint32_t preset_index, float degrees)
{
	return valve_haptic_load_preset(ctx, preset_index, degrees);
}

status_t
valve_save_preset(struct valve_context *ctx, uint32_t preset_index)
{
	return valve_preset_save_current(ctx, preset_index);
}

status_t
valve_edit_preset(uint32_t preset_index, const struct preset_params *params)
{
	return valve_preset_set(preset_index, params);
}

struct valve_state *
valve_get_state(struct valve_context *ctx)
{
	return valve_haptic_get_state(ctx);
}

float
valve_get_energy(struct valve_context *ctx)
{
	struct valve_state *state = valve_haptic_get_state(ctx);
	return state->passivity_energy_j;
}

const struct preset_params *
valve_get_presets(void)
{
	return preset_params;
}

struct valve_context *
valve_get_context(void)
{
	return valve_haptic_get_context();
}

status_t
valve_stage_config(struct valve_context *ctx, const struct valve_config *cfg, uint32_t field_mask)
{
	return valve_haptic_stage_config(ctx, cfg, field_mask);
}
