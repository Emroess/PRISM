/*
 * valve_physics.c
 *
 * PURE RESISTIVE HAPTIC FEEDBACK
 *
 * State snapshot: residual free-space blank + Coulomb speed schedule +
 * wall exit k=0 + wall τ cap 2.5 — BEFORE FOC free-space cap / slew /
 * soft-spring / impact derate / auto-params.
 */

#include "valve_physics.h"
#include "valve_auto_params.h"
#include "config/valve.h"
#include "arm_math.h"
#include <stdbool.h>

static inline float valve_fabsf(float x)
{
	return __builtin_fabsf(x);
}

static inline float valve_fminf(float a, float b)
{
	return __builtin_fminf(a, b);
}

static inline float valve_fmaxf(float a, float b)
{
	return __builtin_fmaxf(a, b);
}

static inline float valve_signf(float x)
{
	if (x > 0.0f) return 1.0f;
	if (x < 0.0f) return -1.0f;
	return 0.0f;
}

static inline float valve_clamp_sym(float x, float lim)
{
	if (x > lim) return lim;
	if (x < -lim) return -lim;
	return x;
}

float
valve_physics_clamp_torque(float torque_nm, float limit_nm)
{
	if (torque_nm > limit_nm)
		return limit_nm;
	else if (torque_nm < -limit_nm)
		return -limit_nm;
	return torque_nm;
}

static inline float valve_hil_smooth_sign(float omega, float eps)
{
	float eps_sq = eps * eps;
	float denominator = omega * omega + eps_sq;
	float sqrt_denominator;
	arm_status status = arm_sqrt_f32(denominator, &sqrt_denominator);
	if (status != ARM_MATH_SUCCESS) {
		sqrt_denominator = eps;
	}
	return omega / sqrt_denominator;
}

/* Ramp Coulomb 0→1 from auto deadband→full (slow turns pure viscous). */
static inline float valve_coulomb_speed_scale(float abs_omega)
{
	float span;
	float u;
	float w_db = valve_auto_coulomb_deadband();
	float w_full = valve_auto_coulomb_full();

	if (abs_omega <= w_db) {
		return 0.0f;
	}
	span = w_full - w_db;
	if (span < 1e-3f) {
		return 1.0f;
	}
	u = (abs_omega - w_db) / span;
	if (u >= 1.0f) {
		return 1.0f;
	}
	return u * u * (3.0f - 2.0f * u);
}

static inline float valve_hil_compute_wall_penetration(
    float theta_turns,
    float theta_off,
    float theta_on)
{
	float left_penetration = valve_fminf(0.0f, theta_turns - theta_off);
	float right_penetration = valve_fmaxf(0.0f, theta_turns - theta_on);
	return left_penetration + right_penetration;
}

static inline float valve_hil_compute_wall_torque(
    float theta_turns,
    float omega_turns_per_s,
    float theta_off,
    float theta_on,
    float kw,
    float cw)
{
	float penetration;
	float k_eff;
	float c_eff;
	float into_wall;
	float omega_rad_s;
	float tau;

	penetration = valve_hil_compute_wall_penetration(theta_turns,
	    theta_off, theta_on);

	if (valve_fabsf(penetration) < 1e-6f) {
		return 0.0f;
	}

	k_eff = kw;
	c_eff = cw;
	omega_rad_s = omega_turns_per_s * VALVE_TWO_PI;
	into_wall = penetration * omega_turns_per_s;

	/*
	 * Soft spring: force rises then soft-saturates with depth so deep
	 * over-travel feels more like a solid stop, less like a charged spring.
	 */
	{
		float pen_abs = valve_fabsf(penetration);
		float p0 = VALVE_WALL_SOFT_PEN_TURNS;
		float pen_soft;

		if (p0 < 1e-4f) {
			p0 = 1e-4f;
		}
		pen_soft = penetration / (1.0f + pen_abs / p0);
		penetration = pen_soft;
	}

	/*
	 * Holding steady past the stop: |ω| is small but noisy. Killing spring
	 * whenever into_wall<0 (old HOLD=0.12) flickered k on/off → strong
	 * in-place vibration. Only exit-kill spring at clear exit speed.
	 */
	if (into_wall < 0.0f &&
	    valve_fabsf(omega_rad_s) > VALVE_WALL_EXIT_OMEGA_RAD_S) {
		k_eff = 0.0f;
		c_eff = cw * VALVE_WALL_EXIT_C_MULT;
	} else if (valve_fabsf(omega_rad_s) < VALVE_WALL_DAMP_DEADBAND_RAD_S) {
		/* Pure static force while holding — no −c·ω noise */
		c_eff = 0.0f;
	}

	tau = (-k_eff * penetration) + (-c_eff * omega_turns_per_s);
	return valve_clamp_sym(tau, valve_auto_wall_tau_max());
}

static inline float valve_hil_compute_virtual_torque(
    float theta_turns,
    float omega_filt_rad_s,
    float theta_off,
    float theta_on,
    float b,
    float tau_c,
    float kw,
    float cw,
    float eps,
    float max_torque,
    float degrees_per_turn)
{
	float viscous_torque;
	float friction_torque;
	float wall_torque;
	float total_torque;
	float omega_turns_s;
	float abs_w;
	float cscale;
	float pen;

	pen = valve_hil_compute_wall_penetration(theta_turns, theta_off,
	    theta_on);

	/* In wall: free-space off (wall-only) */
	if (valve_fabsf(pen) >= 1e-6f) {
		b = 0.0f;
		tau_c = 0.0f;
	}

	viscous_torque = -b * omega_filt_rad_s;

	abs_w = valve_fabsf(omega_filt_rad_s);
	/*
	 * Soft-sign ε from auto-params (identity at 0.2/0.2). Scales up with
	 * τc so Coulomb slope τc/ε stays ≤ baseline — higher friction without
	 * sharper zero-cross grind. Never use a smaller ε than auto.
	 */
	{
		float eps_auto = valve_auto_coulomb_eps();
		if (eps_auto > eps) {
			eps = eps_auto;
		}
	}
	cscale = valve_coulomb_speed_scale(abs_w);
	if (cscale <= 0.0f || tau_c <= 0.0f) {
		friction_torque = 0.0f;
	} else if (eps > 0.0f) {
		friction_torque = -(tau_c * cscale) *
		    valve_hil_smooth_sign(omega_filt_rad_s, eps);
	} else {
		friction_torque = -(tau_c * cscale) *
		    valve_signf(omega_filt_rad_s);
	}

	/*
	 * Free-space FOC cap only when gains elevated. At b=τc=0.2 this path
	 * is skipped so feel matches the hand-tuned baseline exactly.
	 */
	if (valve_auto_at_baseline() == 0U) {
		float free_space = viscous_torque + friction_torque;
		free_space = valve_clamp_sym(free_space,
		    valve_auto_free_space_tau_max());
		viscous_torque = free_space;
		friction_torque = 0.0f;
	}

	omega_turns_s = (omega_filt_rad_s * VALVE_RAD_TO_DEG) / degrees_per_turn;
	wall_torque = valve_hil_compute_wall_torque(
	    theta_turns, omega_turns_s, theta_off, theta_on, kw, cw);

	total_torque = viscous_torque + friction_torque + wall_torque;
	total_torque = valve_fmaxf(-max_torque, valve_fminf(max_torque, total_torque));

	return total_torque;
}

float valve_physics_calculate_torque_hil(
    const struct valve_config *cfg,
    float position_deg,
    float omega_filt_rad_s,
    bool quiet_active,
    bool settle_residual)
{
	float degrees_per_turn;
	float theta_turns;
	float theta_off_turns;
	float theta_on_turns;
	float b;
	float tau_c;
	float kw;
	float cw;
	float eps;
	float max_torque;

	degrees_per_turn = cfg->degrees_per_turn;
	if (degrees_per_turn <= 0.0f) {
		degrees_per_turn = VALVE_DEFAULT_DEGREES_PER_TURN;
	}

	theta_turns = position_deg / degrees_per_turn;
	theta_off_turns = cfg->closed_position_deg / degrees_per_turn;
	theta_on_turns = cfg->open_position_deg / degrees_per_turn;

	b = cfg->hil_b_viscous_nm_s_per_rad;
	tau_c = cfg->hil_tau_c_coulomb_nm;
	kw = cfg->hil_k_w_wall_stiffness_nm_per_turn;
	cw = cfg->hil_c_w_wall_damping_nm_s_per_turn;
	eps = cfg->hil_eps_smoothing;
	max_torque = cfg->hil_tau_max_limit_nm;

	/* Quiet or residual coast: free-space off; walls stay */
	if (quiet_active || settle_residual) {
		b = 0.0f;
		tau_c = 0.0f;
	}

	return valve_physics_clamp_torque(
	    valve_hil_compute_virtual_torque(
		theta_turns, omega_filt_rad_s,
		theta_off_turns, theta_on_turns,
		b, tau_c, kw, cw, eps, max_torque, degrees_per_turn),
	    max_torque);
}
