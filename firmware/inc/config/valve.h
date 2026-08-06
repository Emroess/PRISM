/*
 * config/valve.h - Valve Configuration Defines
 *
 * SINGLE SOURCE OF TRUTH for all valve-related configuration constants.
 * Consolidates defines previously scattered across valve_config.h and
 * valve_haptic.h.
 *
 * Note: Data structures and function prototypes remain in valve headers
 *
 * Compliant with:
 * - MISRA-C:2012
 * - OpenBSD style guide
 */

#ifndef CONFIG_VALVE_H
#define CONFIG_VALVE_H

#include <stdint.h>

/*
 * ===========================================================================
 * Configuration Field Masks (for selective updates)
 * ===========================================================================
 */
#define CFG_FIELD_VISCOUS        (1U << 0)
#define CFG_FIELD_COULOMB        (1U << 1)
#define CFG_FIELD_WALL_STIFFNESS (1U << 2)
#define CFG_FIELD_WALL_DAMPING   (1U << 3)
#define CFG_FIELD_SMOOTHING      (1U << 4)
#define CFG_FIELD_TORQUE_LIMIT   (1U << 5)
#define CFG_FIELD_OPEN_POS       (1U << 6)
#define CFG_FIELD_CLOSED_POS     (1U << 7)
#define CFG_FIELD_SCALE          (1U << 8)

/*
 * ===========================================================================
 * Control Loop Configuration
 * ===========================================================================
 */
#define VALVE_CONTROL_LOOP_HZ          1000U
#define VALVE_CONTROL_LOOP_PERIOD_S    (1.0f / (float)VALVE_CONTROL_LOOP_HZ)
#define VALVE_CONTROL_LOOP_PERIOD_MS   (1000U / VALVE_CONTROL_LOOP_HZ)
#define VALVE_CONTROL_LOOP_PERIOD_US   (1000000U / VALVE_CONTROL_LOOP_HZ)
#define VALVE_LOOP_DT_S                VALVE_CONTROL_LOOP_PERIOD_S

/* TIM6 timer configuration */
#define TIM6_PRESCALER                 199U    /* 200MHz -> 1MHz */
#define TIM6_BASE_FREQUENCY_HZ         1000000U /* 1 MHz after prescaling */

/*
 * ===========================================================================
 * Position and Safety Limits
 * ===========================================================================
 */
#define VALVE_MAX_POSITION_DEG         3600.0f  /* 10 turns maximum position */
#define VALVE_MAX_TORQUE_LIMIT_NM      30.0f    /* Maximum allowed torque limit */

/*
 * ===========================================================================
 * Mathematical and Physical Constants
 * ===========================================================================
 */
#define VALVE_TWO_PI                   6.28318530718f
#define VALVE_DEFAULT_DEGREES_PER_TURN 360.0f
#define VALVE_DEG_TO_RAD               0.0174533f
#define VALVE_RAD_TO_DEG               (1.0f / VALVE_DEG_TO_RAD)

/* Hardware constants */
#define VALVE_CPU_CLOCK_MHZ            400U

/*
 * ===========================================================================
 * Filter and Passivity Constants
 * ===========================================================================
 */
#define VALVE_TORQUE_FILTER_CUTOFF_HZ        400.0f
#define VALVE_PASSIVITY_ENERGY_CAP_J         2.0f

/*
 * Velocity estimate (runtime selectable).
 * 0=raw Δθ  1=ODrive  2=LPF on Δθ (default)
 */
#define VALVE_VEL_SOURCE_DELTA               0U
#define VALVE_VEL_SOURCE_ODRIVE              1U
#define VALVE_VEL_SOURCE_LPF_DELTA           2U
#define VALVE_VEL_SOURCE_DEFAULT             VALVE_VEL_SOURCE_LPF_DELTA
#define VALVE_VELOCITY_LPF_CUTOFF_HZ         30.0f
#define VALVE_VELOCITY_LPF_CUTOFF_MIN_HZ     1.0f
#define VALVE_VELOCITY_LPF_CUTOFF_MAX_HZ     200.0f

/*
 * Quiet-at-rest: free-space off when |ω| low; walls always active.
 */
#define VALVE_QUIET_GATE_DEFAULT_ENABLE      1U
#define VALVE_QUIET_ENTER_DEFAULT_RAD_S      0.08f
#define VALVE_QUIET_EXIT_DEFAULT_RAD_S       0.16f

/*
 * Coulomb: pure viscous when slow; full τc when firm.
 * Deadband / full / ε are gain-scheduled in valve_auto_params (identity
 * at b=τc=0.2). Raising gains widens onset and ε so τc/ε stays bounded
 * (more resistance without sharper grind).
 */
#define VALVE_HIL_EPS_SMOOTHING_DEFAULT      0.30f
#define VALVE_COULOMB_DEADBAND_RAD_S         0.50f
#define VALVE_COULOMB_FULL_RAD_S             2.00f

/*
 * Quiet enter debounce (faster after settle-arm).
 */
#define VALVE_REST_LATCH_SAMPLES             40U
#define VALVE_REST_LATCH_SETTLE_SAMPLES      12U

/*
 * Residual settle: arm on |ω|≥ARM or ring flips. Mid free-space blank
 * only after energetic peak (see PEAK_BLANK in valve_haptic.c) or rings —
 * not on ordinary hand motion (that was grindy at elevated b/τc).
 * Wall exit k=0 on leave.
 */
#define VALVE_SETTLE_ARM_RAD_S               0.25f
#define VALVE_SETTLE_BLANK_RAD_S             0.45f
#define VALVE_SETTLE_TIMEOUT_SAMPLES         5000U
#define VALVE_RING_FLIP_WINDOW_SAMPLES       300U
#define VALVE_RING_FLIP_COUNT               3U
#define VALVE_WALL_TAU_MAX_NM                2.5f
/*
 * End-stop release: after wall contact, blank free-space until quiet while
 * coasting slowly. Exit c mult dissipates launch. Soft pen makes deep stop
 * more "solid force", less elastic catapult.
 *
 * HOLD vibration fix: do NOT kill spring on tiny ω noise (that flickered k
 * on/off while the user held steady past the stop → strong in-place buzz).
 * Exit k=0 only above EXIT omega. Wall damper deadband while nearly still
 * so hold feels like pure force −k·p, not −c·ω noise.
 *
 * RE-ENTRY grind fix: quick pull from over-travel back into 0–90° used to
 * leave settle_armed + mid blank on, so free-space b/τc dropped whenever
 * |ω|<blank → bumpy until sit/quiet. Firm free-space motion after wall
 * sets free_space_restore (see valve_haptic.c) so friction stays on.
 */
#define VALVE_WALL_EXIT_OMEGA_RAD_S          0.60f
#define VALVE_WALL_DAMP_DEADBAND_RAD_S       0.20f
#define VALVE_WALL_RELEASE_BLANK_RAD_S       2.00f
#define VALVE_WALL_EXIT_C_MULT               2.50f
#define VALVE_WALL_SOFT_PEN_TURNS            0.05f

/*
 * ===========================================================================
 * Thermal Monitoring (read-only; limits enforced by ODrive S1 firmware)
 * ===========================================================================
 * ODrive S1 has built-in thermal protection:
 *   - inverter0.temp_limit_lower/upper for FET thermal derating
 *   - motor_thermistor.config.temp_limit_lower/upper for motor protection
 * Configure these via odrivetool and save to ODrive NVM.
 */

/*
 * ===========================================================================
 * ODrive Motor Parameters
 * ===========================================================================
 */
#define ODRIVE_TORQUE_CONSTANT_NM_PER_A  0.083f
#define ODRIVE_DEFAULT_NODE_ID           1
#define ODRIVE_CURRENT_HEADROOM_A        2.0f
#define VALVE_ODRIVE_VEL_LIMIT_TURNS_PER_S 20.0f
#define VALVE_ODRIVE_CURRENT_LIMIT_A     120.0f /* Odrive firmware is multiplying by ODRIVE_TORQUE_CONSTANT_NM_PER_A 120.0f*0.083=10.0*/

#endif /* CONFIG_VALVE_H */
