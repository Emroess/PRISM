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
