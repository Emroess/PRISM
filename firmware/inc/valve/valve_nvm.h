/*
 * valve_nvm.h
 *
 * Header for non-volatile memory management of valve presets.
 */

#ifndef VALVE_NVM_H
#define VALVE_NVM_H

#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Preset parameters structure (matches valve_presets.c) */
struct preset_params {
    char name[16];  /* User-editable preset name */
    float torque_limit_nm;
    float default_travel_deg;
    float hil_b_viscous_nm_s_per_rad;
    float hil_tau_c_coulomb_nm;
    float hil_k_w_wall_stiffness_nm_per_turn;
    float hil_c_w_wall_damping_nm_s_per_turn;
    float hil_eps_smoothing;
};

/* Initialize NVM (load defaults if needed) */
status_t valve_nvm_init(void);

/* Load presets from NVM */
status_t valve_nvm_load_presets(struct preset_params[4]);

/* Access built-in default presets */
const struct preset_params *valve_nvm_get_default_presets(void);

/* Save presets to NVM */
status_t valve_nvm_save_presets(const struct preset_params[4]);

#ifdef __cplusplus
}
#endif

#endif /* VALVE_NVM_H */
