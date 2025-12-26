/*
 * valve_physics.c
 *
 * PURE RESISTIVE HAPTIC FEEDBACK - Valve Handle Simulation
 *
 * This module implements a passive resistance model that simulates a real
 * mechanical valve handle (like a water faucet). The motor NEVER moves itself -
 * it only resists human-applied motion with friction forces.
 *
 * Design Principles:
 * - ZERO active positioning: No springs, no servo control, no target positions
 * - ONLY velocity-dependent forces: Friction opposes motion direction
 * - Boundary protection: End stops prevent over-travel (closed/open limits)
 * - Human-driven motion: Motor responds to human torque input with resistance
 *
 * All functions are pure (no side effects) and deterministic.
 */

#include "valve_physics.h"
#include "arm_math.h"
#include <stdbool.h>

/* Deterministic math functions (no libm dependencies) */

/*
 * valve_fabsf - Absolute value function
 *
 * Provides deterministic absolute value calculation using ARM VFP hardware.
 * The vabs.f32 instruction provides optimal performance without branching.
 */
static inline float valve_fabsf(float x)
{
    return __builtin_fabsf(x);
}

/*
 * valve_fminf - Minimum of two floats
 *
 * Uses ARM VFP vminnm.f32 instruction for optimal performance without branching.
 */
static inline float valve_fminf(float a, float b)
{
    return __builtin_fminf(a, b);
}

/*
 * valve_fmaxf - Maximum of two floats
 *
 * Uses ARM VFP vmaxnm.f32 instruction for optimal performance without branching.
 */
static inline float valve_fmaxf(float a, float b)
{
    return __builtin_fmaxf(a, b);
}

/*
 * valve_signf - Sign function
 *
 * Returns the sign of a float value (-1, 0, or 1) deterministically.
 * Used in friction calculations where sign determination is critical
 * for proper torque direction handling.
 */
static inline float valve_signf(float x)
{
    if (x > 0.0f) return 1.0f;
    if (x < 0.0f) return -1.0f;
    return 0.0f;
}

/*
 * Approximates sign(x) with a smooth tanh-like curve.
 * Hard sign transitions cause limit-cycle oscillations in the
 * Coulomb friction model, making the valve "buzz" at rest.
 */
static inline float valve_smooth_sign(float x, float smoothing_width)
{
	float ratio;
	float ratio2;

	if (smoothing_width <= 0.0f)
		return valve_signf(x);

	ratio = x / smoothing_width;

	/* Clamp to ±1 outside smoothing region */
	if (ratio > 3.0f)
		return 1.0f;
	if (ratio < -3.0f)
		return -1.0f;

	/*
	 * Polynomial approximation of tanh for |ratio| < 3
	 * tanh(x) ≈ x - x³/3 + 2x⁵/15 (good enough for smoothing)
	 */
	ratio2 = ratio * ratio;
	return ratio * (1.0f - ratio2 * (0.3333f - 0.1333f * ratio2));
}

/*
 * valve_smooth_saturation - Smooth saturation function
 *
 * Implements a smooth, differentiable saturation that gradually approaches
 * limits using a polynomial transition rather than hard clipping. This prevents
 * sudden changes in control derivatives near saturation boundaries.
 *
 * Uses a cubic Hermite interpolation in the transition region for C1 continuity
 * (continuous first derivative), ensuring smooth motor behavior.
 *
 * x: Input value
 * limit: Saturation limit (symmetric ±limit)
 * transition_width: Width of smooth transition region (0.0 to 1.0 of limit)
 * Returns smoothly saturated output.
 */
/* Smooth saturation previously used by the more complex model; no longer needed */

/*
 * Hard limit on motor torque to prevent hardware damage.
 * This is the last line of defense if the physics model or
 * upstream code produces unreasonable values.
 */
float
valve_physics_clamp_torque(float torque_nm, float limit_nm)
{
	if (torque_nm > limit_nm)
		return limit_nm;
	else if (torque_nm < -limit_nm)
		return -limit_nm;
	return torque_nm;
}

/*
 * HIL-STYLE PHYSICS FUNCTIONS
 * Exact duplication of hil_valve_model.py physics
 */

/*
 * valve_hil_smooth_sign - Smooth sign function approximation
 *
 * Approximates the sign function using a smooth tanh-based polynomial
 * to avoid discontinuities at zero. This prevents numerical instabilities
 * in Coulomb friction calculations where sharp transitions can cause
 * limit-cycle oscillations in the control loop.
 *
 * sgn_ε(ω) = ω / sqrt(ω² + ε²)
 */
static inline float valve_hil_smooth_sign(float omega, float eps)
{
    float eps_sq = eps * eps;
    float denominator = omega * omega + eps_sq;
    float sqrt_denominator;
    arm_status status = arm_sqrt_f32(denominator, &sqrt_denominator);
    if (status != ARM_MATH_SUCCESS) {
        /* Fallback to avoid division by zero */
        sqrt_denominator = eps;
    }
    return omega / sqrt_denominator;
}

/*
 * valve_hil_compute_wall_penetration - Compute wall penetration
 *
 * Compute wall penetration:
 * p = min(0, θ - θ_off) + max(0, θ - θ_on)
 * Negative on left, positive on right, zero inside window
 */
static inline float valve_hil_compute_wall_penetration(
    float theta_turns,
    float theta_off,
    float theta_on)
{
    float left_penetration = valve_fminf(0.0f, theta_turns - theta_off);
    float right_penetration = valve_fmaxf(0.0f, theta_turns - theta_on);
    return left_penetration + right_penetration;
}

/*
 * valve_hil_compute_wall_torque - Compute wall torque with soft cubic easing
 *
 * Compute wall torque with soft cubic easing:
 * τ_wall = -k_w * p - c_w * ṗ
 *
 * Args:
 *     theta_turns: Position in turns
 *     omega_turns_per_s: Velocity in turns/s
 *     theta_off: Closed position in turns
 *     theta_on: Open position in turns
 *     kw: Wall stiffness [N·m/turn]
 *     cw: Wall damping [N·m·s/turn]
 */
static inline float valve_hil_compute_wall_torque(
    float theta_turns,
    float omega_turns_per_s,
    float theta_off,
    float theta_on,
    float kw,
    float cw)
{
    float penetration = valve_hil_compute_wall_penetration(theta_turns, theta_off, theta_on);

    if (valve_fabsf(penetration) < 1e-6f) {
        return 0.0f;
    }

    /* Wall stiffness torque (proportional to penetration) */
    float stiffness_torque = -kw * penetration;

    /*
     * Wall damping torque (proportional to penetration velocity)
     * p_dot ~= omega when moving into/out of wall
     */
    float penetration_velocity = (penetration != 0.0f) ? omega_turns_per_s : 0.0f;
    float damping_torque = -cw * penetration_velocity;

    return stiffness_torque + damping_torque;
}

/*
 * valve_hil_compute_virtual_torque - Compute total virtual torque
 *
 * Compute total virtual torque:
 * τ(θ,ω) = -b*ω - τ_c*sgn_ε(ω) + τ_wall(θ,ω)
 *
 * Where ω is in rad/s for the damping term
 */
static inline float valve_hil_compute_virtual_torque(
    float theta_turns,
    float omega_turns_per_s,
    float theta_off,
    float theta_on,
    float b,
    float tau_c,
    float kw,
    float cw,
    float eps,
    float max_torque)
{
    float omega_rad_s;
    float viscous_torque;
    float friction_torque;
    float wall_torque;
    float total_torque;

    /* Convert omega from turns/s to rad/s for physical units */
    omega_rad_s = omega_turns_per_s * VALVE_TWO_PI;

    /* Viscous damping term: b in [N*m*s/rad] */
    viscous_torque = -b * omega_rad_s;

    /* Coulomb/static friction term: tau_c in [N*m] */
    friction_torque = -tau_c * valve_hil_smooth_sign(omega_rad_s, eps);

    /* Wall interaction torque: kw [N*m/turn], cw [N*m*s/turn] */
    wall_torque = valve_hil_compute_wall_torque(
        theta_turns, omega_turns_per_s, theta_off, theta_on, kw, cw);

    /* Combine all torque components */
    total_torque = viscous_torque + friction_torque + wall_torque;

    /* Apply safety limits */
    total_torque = valve_fmaxf(-max_torque, valve_fminf(max_torque, total_torque));

    return total_torque;
}

/*
 * valve_physics_calculate_torque - Main haptic physics engine
 *
 * Calculates PURE RESISTIVE TORQUE that simulates a mechanical valve handle.
 * 
 * CRITICAL DESIGN PRINCIPLE: This system provides ONLY resistance to human-applied
 * motion. The motor NEVER generates forces to move itself - it only creates drag
 * forces that oppose whatever direction the human hand is trying to turn it.
 *
 * Think of this like a brake system, not a servo motor:
 * - At rest: Quiet gate zeroes all forces so it stays exactly where you leave it
 * - During motion: Friction resists the motion direction
 * - Near limits: End stops create "soft walls" that prevent over-rotation
 * - NO position seeking: No stored energy, only passive dissipation
 *
 * Mathematical model for RESISTIVE forces:
 *   Torque = -B*velocity - F*sign(velocity) + boundary_resistance(position)
 * 
 * Where:
 *   B = viscous damping coefficient (creates speed-dependent drag) - REMOVED
 *   F = Coulomb friction (constant force opposing motion direction)
 *   boundary_resistance = only active near closed/open limits (soft stops)
 *
 * This matches how real mechanical valves work:
 * - Water faucet: Turn it and feel constant resistance, release and it stays
 * - Gas valve: Stiff friction makes it hard to turn, but no self-movement
 * - Ball valve: Light resistance during rotation, hard stops at limits
 *
 * ELECTROMAGNETIC BRAKING NOTE:
 * valve_physics_calculate_torque_hil - HIL-style physics engine
 *
 * Exact duplication of hil_valve_model.py physics model.
 * Implements pure resistive haptic feedback with:
 * - Viscous damping and Coulomb friction
 * - Soft wall end-stops with cubic easing
 * - Smooth sign function to avoid chatter
 *
 * This replaces the complex Stribeck model with the simpler, proven HIL approach.
 */
float valve_physics_calculate_torque_hil(
    const struct valve_config *cfg,
    float position_deg,
    float omega_rad_s,
    bool quiet_active)
{
    /* Quiet gate: no torque when active */
    if (quiet_active) {
        return 0.0f;
    }

    /* Convert position and velocity to turns for wall calculations */
    float degrees_per_turn = cfg->degrees_per_turn;
    if (degrees_per_turn <= 0.0f) {
        degrees_per_turn = VALVE_DEFAULT_DEGREES_PER_TURN; /* Fallback */
    }

    float theta_turns = position_deg / degrees_per_turn;

    /* Convert endpoint positions from degrees to turns */
    float theta_off_turns = cfg->closed_position_deg / degrees_per_turn;
    float theta_on_turns = cfg->open_position_deg / degrees_per_turn;

    /* Get HIL physics parameters */
    float b = cfg->hil_b_viscous_nm_s_per_rad;
    float tau_c = cfg->hil_tau_c_coulomb_nm;
    float kw = cfg->hil_k_w_wall_stiffness_nm_per_turn;
    float cw = cfg->hil_c_w_wall_damping_nm_s_per_turn;
    float eps = cfg->hil_eps_smoothing;
    float max_torque = cfg->hil_tau_max_limit_nm;

    /* Compute virtual torque using HIL physics */
    float virtual_torque = valve_hil_compute_virtual_torque(
        theta_turns, (omega_rad_s * VALVE_RAD_TO_DEG) / degrees_per_turn,
        theta_off_turns, theta_on_turns,
        b, tau_c, kw, cw, eps, max_torque);

    /* Apply final safety clamping */
    return valve_physics_clamp_torque(virtual_torque, max_torque);
}
