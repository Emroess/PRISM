/*
 * valve_auto_params.h
 *
 * Gain-scheduled haptic hyperparameters from user b (viscous) and τc (Coulomb).
 *
 * Baseline (identity): b = 0.2 N·m·s/rad, τc = 0.2 N·m
 * At that point every derived value matches the hand-tuned constants that
 * "work great" on the plant — zero intentional change to feel at 0.2/0.2.
 *
 * For other (b, τc): soften Coulomb edges with absolute effort (anti-grind),
 * tight free-space τ cap, fixed 30 Hz vel LPF, no raw-ω lead (runaway fix).
 */

#ifndef VALVE_AUTO_PARAMS_H
#define VALVE_AUTO_PARAMS_H

#include "valve_haptic.h"

#ifdef __cplusplus
extern "C" {
#endif

/* User baseline that must reproduce fixed-hand-tune values exactly. */
#define VALVE_AUTO_B0_NM_S_PER_RAD   0.20f
#define VALVE_AUTO_TC0_NM            0.20f

struct valve_auto_params {
	float coulomb_deadband_rad_s;
	float coulomb_full_rad_s;
	float coulomb_eps_rad_s;
	float settle_arm_rad_s;
	float settle_blank_rad_s;
	float wall_tau_max_nm;
	float free_space_tau_max_nm;
	float torque_slew_nm_per_s;
	float torque_lpf_hz;
	float velocity_lpf_hz;
	/* 0 at baseline → blend raw ω into viscous for passivity at high gain */
	float omega_fast_blend;
	/* Debug / status */
	float scale_b;
	float scale_tc;
	float design_tau_fs_nm;
};

void valve_auto_params_update(const struct valve_config *cfg);

const struct valve_auto_params *valve_auto_params_get(void);

float valve_auto_coulomb_deadband(void);
float valve_auto_coulomb_full(void);
float valve_auto_coulomb_eps(void);
float valve_auto_settle_arm(void);
float valve_auto_settle_blank(void);
float valve_auto_wall_tau_max(void);
float valve_auto_free_space_tau_max(void);
float valve_auto_torque_slew_nm_per_s(void);
float valve_auto_torque_lpf_hz(void);
float valve_auto_velocity_lpf_hz(void);
float valve_auto_omega_fast_blend(void);

/* True when b≈0.2 and τc≈0.2 — elevated-only paths must stay off. */
uint8_t valve_auto_at_baseline(void);

#ifdef __cplusplus
}
#endif

#endif /* VALVE_AUTO_PARAMS_H */
