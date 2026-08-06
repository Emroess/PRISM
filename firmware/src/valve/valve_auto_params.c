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
 *     r   = 0.5·(r_b + r_c)          isotropic average
 *     r_τ = τ_fs / τ_fs0             effort scale (1 at baseline)
 *
 * Hand-tuned baseline values (identity targets):
 *     ω_db0    = 0.50 rad/s
 *     ω_full0  = 2.00 rad/s
 *     ε0       = 0.30 rad/s
 *     arm0     = 0.25 rad/s
 *     blank0   = 0.45 rad/s
 *     wall0    = 2.50 N·m
 *     free0    = 10.0 N·m
 *     slew0    = 200 N·m/s
 *     lpf0     = 400 Hz
 *
 * Anti-grind scheduling (all identity when r_b = r_c = r_τ = 1):
 *
 *   Raising b and τc together must raise resistance without sharpening
 *   Coulomb edges. Previously w_db/ε only tracked rc/rb (ratio), so 0.3/0.3
 *   kept the same sharp profile as 0.2/0.2 with 50% larger torque steps → grind.
 *
 *   (1) Coulomb deadband — ratio term + absolute effort widen:
 *         w_db = ω_db0 · (r_c / max(r_b, r_min)) · max(r_τ, 1)
 *
 *   (2) Coulomb full — wider ramp as effort grows:
 *         w_full = w_db + (ω_full0 − ω_db0) · max(r, 1)
 *
 *   (3) Soft-sign ε — track deadband, then floor so τc/ε ≤ τc0/ε0:
 *         ε = max( ε0 · (w_db / ω_db0),  τc · (ε0 / τc0) )
 *       → peak Coulomb slope stays ≤ baseline; higher τc feels smoother.
 *
 *   (4) Settle arm / blank — mild growth with √r (unchanged identity):
 *         arm   = arm0   · √r
 *         blank = arm + (blank0 − arm0) · √r
 *
 *   (5) Wall τ cap — tighter when design peak τ grows (FOC):
 *         wall = wall0 / r_τ
 *
 *   (6) Free-space τ cap — free0 / r_τ (large at baseline → no clip)
 *
 *   (7) Torque slew — keep ≥ slew0 (do NOT tighten with r_τ; lag caused
 *       ratchety free-space at elevated gains). Slightly raise with r_τ.
 *
 *   (8) Torque LPF — lower cutoff as effort grows (smooths Coulomb steps):
 *         fc = lpf0 / max(r_τ, 1)
 *
 * Clamps keep values in safe plant ranges without shifting the baseline point.
 */

#include "valve_auto_params.h"
#include "config/valve.h"

/* Identity anchors (must match #defines used at b0/τc0) */
#define AUTO_OMEGA_STAR_RAD_S     8.0f
#define AUTO_W_DB0                0.50f
#define AUTO_W_FULL0              2.00f
#define AUTO_EPS0                 0.30f
#define AUTO_ARM0                 0.25f
#define AUTO_BLANK0               0.45f
#define AUTO_WALL0_NM             2.50f
#define AUTO_FREE0_NM             10.0f
#define AUTO_SLEW0_NM_PER_S       200.0f
#define AUTO_LPF0_HZ              400.0f
#define AUTO_R_MIN                0.25f

/* Soft clamps (do not bind at baseline) */
#define AUTO_W_DB_MIN             0.25f
#define AUTO_W_DB_MAX             1.80f
#define AUTO_W_FULL_MAX           4.50f
#define AUTO_EPS_MIN              0.15f
#define AUTO_EPS_MAX              1.50f
#define AUTO_ARM_MIN              0.18f
#define AUTO_ARM_MAX              0.55f
#define AUTO_BLANK_MIN            0.32f
#define AUTO_BLANK_MAX            0.90f
#define AUTO_WALL_MIN_NM          1.20f
#define AUTO_WALL_MAX_NM          3.00f
#define AUTO_FREE_MIN_NM          1.20f
#define AUTO_FREE_MAX_NM          12.0f
#define AUTO_SLEW_MIN             80.0f
#define AUTO_SLEW_MAX             400.0f
#define AUTO_LPF_MIN_HZ           60.0f
#define AUTO_LPF_MAX_HZ           400.0f

static struct valve_auto_params g_auto = {
	.coulomb_deadband_rad_s = AUTO_W_DB0,
	.coulomb_full_rad_s = AUTO_W_FULL0,
	.coulomb_eps_rad_s = AUTO_EPS0,
	.settle_arm_rad_s = AUTO_ARM0,
	.settle_blank_rad_s = AUTO_BLANK0,
	.wall_tau_max_nm = AUTO_WALL0_NM,
	.free_space_tau_max_nm = AUTO_FREE0_NM,
	.torque_slew_nm_per_s = AUTO_SLEW0_NM_PER_S,
	.torque_lpf_hz = AUTO_LPF0_HZ,
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
	/* Only expand anti-grind above baseline effort (identity at r_τ≤1) */
	r_tau_up = auto_fmaxf(r_tau, 1.0f);

	/* (1) Coulomb deadband — ratio + absolute effort */
	w_db = AUTO_W_DB0 * (rc / rb) * r_tau_up;
	w_db = auto_clampf(w_db, AUTO_W_DB_MIN, AUTO_W_DB_MAX);

	/* (2) Coulomb full — baseline span 1.50, grow with r above 1 */
	span0 = AUTO_W_FULL0 - AUTO_W_DB0;
	w_full = w_db + span0 * auto_fmaxf(r, 1.0f);
	if (w_full < w_db + 0.35f) {
		w_full = w_db + 0.35f;
	}
	if (w_full > AUTO_W_FULL_MAX) {
		w_full = AUTO_W_FULL_MAX;
	}

	/* (3) ε from deadband, then floor so τc/ε ≤ τc0/ε0 */
	eps = AUTO_EPS0 * (w_db / AUTO_W_DB0);
	eps_floor = tc * (AUTO_EPS0 / VALVE_AUTO_TC0_NM);
	if (eps < eps_floor) {
		eps = eps_floor;
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

	/* (5) wall cap — tighter with effort (FOC) */
	wall = AUTO_WALL0_NM / r_tau;
	wall = auto_clampf(wall, AUTO_WALL_MIN_NM, AUTO_WALL_MAX_NM);

	/* (6) free-space cap */
	free_cap = AUTO_FREE0_NM / r_tau;
	free_cap = auto_clampf(free_cap, AUTO_FREE_MIN_NM, AUTO_FREE_MAX_NM);

	/* (7) slew — do not tighten; allow slightly faster tracking of larger τ */
	slew = AUTO_SLEW0_NM_PER_S * r_tau_up;
	slew = auto_clampf(slew, AUTO_SLEW_MIN, AUTO_SLEW_MAX);

	/* (8) torque LPF — lower when effort high (smooths Coulomb steps) */
	lpf = AUTO_LPF0_HZ / r_tau_up;
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

uint8_t
valve_auto_at_baseline(void)
{
	/*
	 * scale_b = b/0.2, scale_tc = τc/0.2 (after floor). Exact 0.2/0.2 → 1.
	 * Small tolerance for float noise only.
	 */
	if (g_auto.scale_b < 0.995f || g_auto.scale_b > 1.005f) {
		return 0U;
	}
	if (g_auto.scale_tc < 0.995f || g_auto.scale_tc > 1.005f) {
		return 0U;
	}
	return 1U;
}
