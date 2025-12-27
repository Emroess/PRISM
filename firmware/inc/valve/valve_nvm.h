/*
 * valve_nvm.h
 *
 * Header for non-volatile memory management of valve presets
 * and encoder zero calibration.
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
    float theta_closed_deg;  /* Closed position offset from zero (degrees) */
    float theta_open_deg;    /* Open position offset from zero (degrees) */
};

/*
 * Encoder zero calibration structure.
 *
 * Stores the absolute encoder position that defines the fixed 0.00 reference.
 * This position remains constant across valve simulations and reboots.
 * The validation_sample is used to detect if the encoder/magnet was disturbed.
 */
struct valve_zero_calibration {
    float absolute_zero_turns;    /* Encoder turns at mechanical 0° reference */
    float validation_sample;      /* Secondary position for disturbance detection */
    float validation_offset;      /* Expected offset between zero and validation */
    uint32_t calibration_time;    /* Uptime (ms) when calibration was performed */
};

/* Initialize NVM (load defaults if needed) */
status_t valve_nvm_init(void);

/* Load presets from NVM */
status_t valve_nvm_load_presets(struct preset_params[4]);

/* Access built-in default presets */
const struct preset_params *valve_nvm_get_default_presets(void);

/* Save presets to NVM */
status_t valve_nvm_save_presets(const struct preset_params[4]);

/*
 * Zero calibration NVM functions
 */

/* Load zero calibration from NVM */
status_t valve_nvm_load_zero_calibration(struct valve_zero_calibration *cal);

/* Save zero calibration to NVM */
status_t valve_nvm_save_zero_calibration(const struct valve_zero_calibration *cal);

/* Check if zero calibration is valid (exists and passes integrity check) */
status_t valve_nvm_is_zero_calibration_valid(void);

/* Clear zero calibration (revert to startup-relative mode) */
status_t valve_nvm_clear_zero_calibration(void);

#ifdef __cplusplus
}
#endif

#endif /* VALVE_NVM_H */
