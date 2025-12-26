/*
 * valve_manager.h - Valve service layer
 *
 * Provides unified API for valve operations used by both CLI and HTTP API.
 * Single source of truth for all valve business logic.
 */

#ifndef VALVE_MANAGER_H
#define VALVE_MANAGER_H

#include <stdint.h>

#include "status.h"
#include "valve_haptic.h"
#include "valve_presets.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Valve control operations
 */
status_t valve_start(struct valve_context *ctx);
status_t valve_stop(struct valve_context *ctx);

/*
 * Valve configuration setters - atomic updates via staging
 */
status_t valve_set_damping(struct valve_context *ctx, float value);
status_t valve_set_friction(struct valve_context *ctx, float value);
status_t valve_set_epsilon(struct valve_context *ctx, float value);
status_t valve_set_torque_limit(struct valve_context *ctx, float value);
status_t valve_set_wall_damping(struct valve_context *ctx, float value);
status_t valve_set_wall_stiffness(struct valve_context *ctx, float value);
status_t valve_set_scale(struct valve_context *ctx, float degrees_per_turn);
status_t valve_set_open_position(struct valve_context *ctx, float degrees);
status_t valve_set_closed_position(struct valve_context *ctx, float degrees);

/*
 * Valve preset operations
 */
status_t valve_load_preset(struct valve_context *ctx, uint32_t preset_index, float degrees);
status_t valve_save_preset(struct valve_context *ctx, uint32_t preset_index);
status_t valve_edit_preset(uint32_t preset_index, const struct preset_params *params);
const struct preset_params * valve_get_presets(void);

/*
 * Valve query operations
 */
struct valve_state * valve_get_state(struct valve_context *ctx);
float valve_get_energy(struct valve_context *ctx);
struct valve_context * valve_get_context(void);

/*
 * Valve configuration staging - atomic multi-field update
 */
status_t valve_stage_config(struct valve_context *, const struct valve_config *,
    uint32_t);

#ifdef __cplusplus
}
#endif

#endif /* VALVE_MANAGER_H */
