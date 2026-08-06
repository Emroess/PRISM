/*
 * valve_auto_params.c
 *
 * ---------------------------------------------------------------------------
 * Model (anchored at b0 = τc0 = 0.2)
 * ---------------------------------------------------------------------------
 * Design free-space peak effort at reference speed ω* = 8 rad/s:
 *     τ_fs  = b·ω* + τc
 *     τ_fs0 = b0·ω* + τc0 = 1.8 N·m
 *
 * Scale factors (exactly 1 at baseline):
 *     r_b = b / b0 ,   r_c = τc / τc0
 *     r   = 0.5·(r_b + r_c)
 *     r_τ = τ_fs / τ_fs0
 *
 * Free-space at elevated b/τc (identity at baseline):
 *   (1)–(3) Coulomb onset much softer — hand motion stays viscous-dominant
 *           (τc still grows at high |ω| only). Cuts grind + shake limit-cycles.
 *   (4) Settle arm / blank mild √r growth
 *   (5) Wall τ cap
 *   (6) Free-space soft-sat level = design · margin, ≤ hard max
 *   (7) Slew OFF for free-space (elevated slew delayed reverse → oscillation)
 *   (8) Torque LPF mild drop with effort (smooth texture; not severe lag)
 *   (9) Velocity LPF fixed 30 Hz
 *  (10) omega_fast_blend = 0 always
 */

#include "valve_auto_params.h"
#include "config/valve.h"

#define AUTO_OMEGA_STAR_RAD_S     8.0f
#define AUTO_W_DB0                0.50f
#define AUTO_W_FULL0              2.00f
#define AUTO_EPS0                 0.30f
#define AUTO_ARM0                 0.25f
#define AUTO_BLANK0               0.45f
#define AUTO_WALL0_NM             2.50f
#define AUTO_FREE_MARGIN          1.35f
#define AUTO_SLEW0_NM_PER_S       200.0f /* reported but free-space does not slew */
#define AUTO_LPF0_HZ              400.0f
#define AUTO_VEL_LPF0_HZ          30.0f
#define AUTO_R_MIN                0.25f

#define AUTO_W_DB_MIN             0.25f
#define AUTO_W_DB_MAX             2.20f
#define AUTO_W_FULL_MAX           5.00f
#define AUTO_EPS_MIN              0.15f
#define AUTO_EPS_MAX              1.80f
#define AUTO_ARM_MIN              0.18f
#define AUTO_ARM_MAX              0.55f
#define AUTO_BLANK_MIN            0.32f
#define AUTO_BLANK_MAX            0.90f
#define AUTO_WALL_MIN_NM          1.20f
#define AUTO_WALL_MAX_NM          3.00f
#define AUTO_FREE_MIN_NM          1.00f
#define AUTO_FREE_MAX_NM          2.50f
#define AUTO_SLEW_MIN             80.0f
#define AUTO_SLEW_MAX             250.0f
#define AUTO_LPF_MIN_HZ           120.0f
#define AUTO_LPF_MAX_HZ           400.0f

static struct valve_auto_params g_auto = {
	.coulomb_deadband_rad_s = AUTO_W_DB0,
	.coulomb_full_rad_s = AUTO_W_FULL0,
	.coulomb_eps_rad_s = AUTO_EPS0,
	.settle_arm_rad_s = AUTO_ARM0,
	.settle_blank_rad_s = AUTO_BLANK0,
	.wall_tau_max_nm = AUTO_WALL0_NM,
	.free_space_tau_max_nm = 2.43f, /* 1.8 · 1.35 baseline design */
	.torque_slew_nm_per_s = AUTO_SLEW0_NM_PER_S,
	.torque_lpf_hz = AUTO_LPF0_HZ,
	.velocity_lpf_hz = AUTO_VEL_LPF0_HZ,
	.omega_fast_blend = 0.0f,
	.scale_b = 1.0f,
	.scale_tc = 1.0f,
	.design_tau_fs_nm = 1.8f,
};

static float
auto_clampf(float x, float lo, float hi)
{
	if (x < lo) {
		return lo;
	}
	if (x > hi) {
		return hi;
	}
	return x;
}

static float
auto_fmaxf(float a, float b)
{
	return (a > b) ? a : b;
}

static float
auto_sqrtf(float x)
{
	float y;
	int i;

	if (x <= 0.0f) {
		return 0.0f;
	}
	y = x;
	if (y < 1.0f) {
		y = 1.0f;
	}
	for (i = 0; i < 6; i++) {
		y = 0.5f * (y + x / y);
	}
	return y;
}

void
valve_auto_params_update(const struct valve_config *cfg)
{
	float b;
	float tc;
	float rb;
	float rc;
	float r;
	float r_sqrt;
	float tau_fs;
	float tau_fs0;
	float r_tau;
	float r_tau_up;
	float w_db;
	float w_full;
	float span0;
	float eps;
	float eps_floor;
	float arm;
	float blank;
	float gap0;
	float wall;
	float free_cap;
	float slew;
	float lpf;

	if (cfg == NULL) {
		return;
	}

	b = cfg->hil_b_viscous_nm_s_per_rad;
	tc = cfg->hil_tau_c_coulomb_nm;
	if (b < 0.0f) {
		b = 0.0f;
	}
	if (tc < 0.0f) {
		tc = 0.0f;
	}

	rb = b / VALVE_AUTO_B0_NM_S_PER_RAD;
	rc = tc / VALVE_AUTO_TC0_NM;
	if (rb < AUTO_R_MIN) {
		rb = AUTO_R_MIN;
	}
	if (rc < AUTO_R_MIN) {
		rc = AUTO_R_MIN;
	}

	r = 0.5f * (rb + rc);
	r_sqrt = auto_sqrtf(r);

	tau_fs0 = VALVE_AUTO_B0_NM_S_PER_RAD * AUTO_OMEGA_STAR_RAD_S +
	    VALVE_AUTO_TC0_NM;
	tau_fs = b * AUTO_OMEGA_STAR_RAD_S + tc;
	if (tau_fs < 1e-4f) {
		tau_fs = 1e-4f;
	}
	r_tau = tau_fs / tau_fs0;
	r_tau_up = auto_fmaxf(r_tau, 1.0f);

	/*
	 * (1) Coulomb deadband — grow faster than linear with effort so hand
	 * speeds stay pure viscous at 0.3+ (grind + reverse limit-cycles).
	 * Identity: r_tau_up=1 → ω_db0 · (rc/rb).
	 */
	w_db = AUTO_W_DB0 * (rc / rb) * r_tau_up * r_tau_up;
	w_db = auto_clampf(w_db, AUTO_W_DB_MIN, AUTO_W_DB_MAX);

	/* (2) Coulomb full — long soft ramp; full τc only when moving firm */
	span0 = AUTO_W_FULL0 - AUTO_W_DB0;
	w_full = w_db + span0 * r_tau_up * r_tau_up;
	if (w_full < w_db + 0.50f) {
		w_full = w_db + 0.50f;
	}
	if (w_full > AUTO_W_FULL_MAX) {
		w_full = AUTO_W_FULL_MAX;
	}

	/* (3) ε — floor so τc/ε ≤ baseline slope; wider with deadband */
	eps = AUTO_EPS0 * (w_db / AUTO_W_DB0);
	eps_floor = tc * (AUTO_EPS0 / VALVE_AUTO_TC0_NM);
	if (eps < eps_floor) {
		eps = eps_floor;
	}
	/* Extra soften when elevated: keep Coulomb slope from feeling gritty */
	if (r_tau_up > 1.0f) {
		float eps_elev = eps_floor * r_tau_up;
		if (eps < eps_elev) {
			eps = eps_elev;
		}
	}
	eps = auto_clampf(eps, AUTO_EPS_MIN, AUTO_EPS_MAX);

	/* (4) settle arm / blank */
	arm = AUTO_ARM0 * r_sqrt;
	arm = auto_clampf(arm, AUTO_ARM_MIN, AUTO_ARM_MAX);
	gap0 = AUTO_BLANK0 - AUTO_ARM0;
	blank = arm + gap0 * r_sqrt;
	blank = auto_clampf(blank, AUTO_BLANK_MIN, AUTO_BLANK_MAX);
	if (blank < arm + 0.08f) {
		blank = arm + 0.08f;
	}

	/* (5) wall cap */
	wall = AUTO_WALL0_NM / r_tau;
	wall = auto_clampf(wall, AUTO_WALL_MIN_NM, AUTO_WALL_MAX_NM);

	/* (6) free-space soft-sat level */
	free_cap = tau_fs * AUTO_FREE_MARGIN;
	free_cap = auto_clampf(free_cap, AUTO_FREE_MIN_NM, AUTO_FREE_MAX_NM);
	if (free_cap > VALVE_FREE_SPACE_TAU_HARD_MAX_NM) {
		free_cap = VALVE_FREE_SPACE_TAU_HARD_MAX_NM;
	}

	/* (7) slew unused on free-space path (see haptic) */
	slew = AUTO_SLEW0_NM_PER_S;
	slew = auto_clampf(slew, AUTO_SLEW_MIN, AUTO_SLEW_MAX);

	/*
	 * (8) Torque LPF — identity 400 Hz; mild drop when elevated to
	 * smooth Coulomb residual texture without heavy phase lag.
	 * fc = 400 / √r_τ  → ~327 Hz at 0.3/0.3
	 */
	lpf = AUTO_LPF0_HZ / auto_fmaxf(r_sqrt, 1.0f);
	lpf = auto_clampf(lpf, AUTO_LPF_MIN_HZ, AUTO_LPF_MAX_HZ);

	g_auto.coulomb_deadband_rad_s = w_db;
	g_auto.coulomb_full_rad_s = w_full;
	g_auto.coulomb_eps_rad_s = eps;
	g_auto.settle_arm_rad_s = arm;
	g_auto.settle_blank_rad_s = blank;
	g_auto.wall_tau_max_nm = wall;
	g_auto.free_space_tau_max_nm = free_cap;
	g_auto.torque_slew_nm_per_s = slew;
	g_auto.torque_lpf_hz = lpf;
	g_auto.velocity_lpf_hz = AUTO_VEL_LPF0_HZ;
	g_auto.omega_fast_blend = 0.0f;
	g_auto.scale_b = rb;
	g_auto.scale_tc = rc;
	g_auto.design_tau_fs_nm = tau_fs;
}

const struct valve_auto_params *
valve_auto_params_get(void)
{
	return &g_auto;
}

float valve_auto_coulomb_deadband(void) { return g_auto.coulomb_deadband_rad_s; }
float valve_auto_coulomb_full(void) { return g_auto.coulomb_full_rad_s; }
float valve_auto_coulomb_eps(void) { return g_auto.coulomb_eps_rad_s; }
float valve_auto_settle_arm(void) { return g_auto.settle_arm_rad_s; }
float valve_auto_settle_blank(void) { return g_auto.settle_blank_rad_s; }
float valve_auto_wall_tau_max(void) { return g_auto.wall_tau_max_nm; }
float valve_auto_free_space_tau_max(void) { return g_auto.free_space_tau_max_nm; }
float valve_auto_torque_slew_nm_per_s(void) { return g_auto.torque_slew_nm_per_s; }
float valve_auto_torque_lpf_hz(void) { return g_auto.torque_lpf_hz; }
float valve_auto_velocity_lpf_hz(void) { return g_auto.velocity_lpf_hz; }
float valve_auto_omega_fast_blend(void) { return 0.0f; }

uint8_t
valve_auto_at_baseline(void)
{
	if (g_auto.scale_b < 0.995f || g_auto.scale_b > 1.005f) {
		return 0U;
	}
	if (g_auto.scale_tc < 0.995f || g_auto.scale_tc > 1.005f) {
		return 0U;
	}
	return 1U;
}
