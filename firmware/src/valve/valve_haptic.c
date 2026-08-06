/*
 * valve_haptic.c
 *
 * Haptic valve simulation control loop and state machine.
 */

#include <stdbool.h>
#include <string.h>

#include "arm_math.h"
#include "stm32h7xx.h"
#include "stm32h753xx.h"

#include "board.h"
#include "config/board.h"
#include "drivers/fdcan.h"
#include "network/hitl_server.h"
#include "protocols/can_simple.h"
#include "valve_auto_params.h"
#include "valve_filters.h"
#include "valve_haptic.h"
#include "valve_physics.h"
#include "valve_presets.h"

/* TIM6 handle (basic timer for valve control loop) */
static TIM_TypeDef *htim6 = TIM6;
static struct valve_context *active_valve_context;
static uint32_t last_heartbeat_check_ms = 0;
#define VALVE_VELOCITY_FILTER_LIGHT_HZ 200.0f

/* Velocity filter state */
static float velocity_filter_state = 0.0f;
static bool velocity_filters_initialized = false;
static volatile uint8_t velocity_source = VALVE_VEL_SOURCE_DEFAULT;
static volatile float velocity_lpf_hz = VALVE_VELOCITY_LPF_CUTOFF_HZ;
static volatile uint8_t quiet_gate_enable = VALVE_QUIET_GATE_DEFAULT_ENABLE;
static volatile float quiet_enter_rad_s = VALVE_QUIET_ENTER_DEFAULT_RAD_S;
static volatile float quiet_exit_rad_s = VALVE_QUIET_EXIT_DEFAULT_RAD_S;
/* Residual settle + ring detect */
static uint8_t settle_armed = 0;
static uint16_t settle_timeout_count = 0;
static uint16_t rest_latch_count = 0;
static int8_t ring_last_sign = 0;
static uint8_t ring_flip_count = 0;
static uint16_t ring_flip_window = 0;
/* Peak |ω| while armed — wide residual blank after energetic flicks only */
static float settle_peak_abs = 0.0f;
/* After wall contact: free-space blank until quiet (end-stop release) */
static uint8_t wall_release_armed = 0;
/*
 * After intentional free-space re-entry from over-travel: keep normal b/τc
 * until quiet. Otherwise settle_armed + mid blank (and re-arm on |ω|≥arm)
 * zeros free-space whenever |ω|<blank → bumpy/grindy until sit.
 */
static uint8_t free_space_restore = 0;
/*
 * After a firm tap / flick: blank free-space only while coasting slowly
 * (|ω| < blank). NEVER blank while |ω| is high — that killed all resistance
 * during fast back-and-forth at 0.2/0.2 (blank-until-quiet bug).
 */
static uint8_t post_impact_blank = 0;
#define VALVE_SETTLE_PEAK_BLANK_RAD_S        0.90f
#define VALVE_SETTLE_TAP_PEAK_BASELINE_RAD_S 1.50f
#define VALVE_SETTLE_TAP_PEAK_ELEVATED_RAD_S 1.80f
#define VALVE_SETTLE_BLANK_WIDE_MAX_RAD_S    1.50f
/* Firm free-space motion after wall → restore friction feel (not just wall blank) */
#define VALVE_WALL_REENTRY_FREE_RAD_S        0.50f
/* Sustained |ω| above runaway threshold → ESTOP */
static uint16_t runaway_omega_count = 0;
/* Extra free-space ω LPF when elevated (smooth −b·ω texture) */
static float free_space_omega_filt = 0.0f;
static uint8_t free_space_omega_filt_init = 0;
/* Use BOARD_SYSCLK_HZ from board_config.h instead of local define */
#define VALVE_CAN_FAILURE_MAX 3U
#define VALVE_ENCODER_STALE_MS 10U
#define VALVE_ENCODER_TIMEOUT_MS 50U
#define VALVE_HEARTBEAT_TIMEOUT_MS 500U
#define VALVE_STARTUP_ENCODER_TIMEOUT_MS 200U
#define VALVE_TURNS_TO_DEG (360.0f)
#define VALVE_ENCODER_TIMEOUT_TICKS 5U  /* ~5ms at 1 kHz */
#define VALVE_VELOCITY_DEADBAND_DEFAULT_RAD_S (5.0f * VALVE_DEG_TO_RAD)
#define VALVE_MIN_VELOCITY_DEADBAND_RAD_S     (0.5f * VALVE_DEG_TO_RAD)
#define VALVE_STARTUP_RAMP_MS         2000U   /* 2 second startup ramp */
#define VALVE_MAX_PENDING_TICKS 4U
#define VALVE_TORQUE_SIGN 1.0f  /* Odrive is positive torque */
#define VALVE_TORQUE_FILTER_SAMPLE_RATE_HZ ((float)VALVE_CONTROL_LOOP_HZ)

/* Consistent error handling macro (simplified) */
#define VALVE_ERROR_CHECK(expr) do { \
    status_t _status = (expr); \
    if (_status != STATUS_OK) { \
        return _status; \
    } \
} while (0)



/* Simple exponential smoothing filter for velocity and other signals */
static inline float
simple_lowpass(float input, float *state, float alpha)
{
	*state = alpha * input + (1.0f - alpha) * (*state);
	return *state;
}

/* Enter critical section by disabling interrupts for thread-safe configuration updates */
static inline uint32_t valve_enter_critical(void)
{
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

/* Exit critical section by restoring interrupt state */
static inline void valve_exit_critical(uint32_t primask)
{
    if ((primask & 0x1U) == 0U) {
        __enable_irq();
    }
}

/* Apply staged configuration changes atomically to avoid partial updates during operation */
static void valve_apply_staged_config(struct valve_context *ctx)
{
    if (ctx == NULL || ctx->staged_pending == 0U) {
        return;
    }

    uint32_t primask = valve_enter_critical();
    ctx->config = ctx->staged_config;
    ctx->staged_field_mask = 0U;
    ctx->staged_pending = 0U;
    valve_exit_critical(primask);

    ctx->state.degrees_per_turn = (ctx->config.degrees_per_turn > 0.0f) ?
        ctx->config.degrees_per_turn : VALVE_DEFAULT_DEGREES_PER_TURN;
    valve_auto_params_update(&ctx->config);
}

/* Seed velocity filters with initial value when encoder stream resets to prevent transients */
static void valve_velocity_filters_seed(float velocity)
{
	velocity_filter_state = velocity;
	velocity_filters_initialized = true;
}

/* Invalidate velocity filters and reset quiet mode on encoder errors or resets */
static inline void valve_velocity_filters_invalidate(struct valve_state *state)
{
	velocity_filters_initialized = false;
	if (state != NULL) {
		if (quiet_gate_enable == 0U) {
			state->quiet_active = 0U;
		}
	}
}

static void
valve_update_quiet_gate(struct valve_state *state)
{
	float abs_w;
	float enter_w;
	float exit_w;
	uint16_t latch_need;

	if (state == NULL) {
		return;
	}
	if (quiet_gate_enable == 0U) {
		state->quiet_active = 0U;
		return;
	}
	abs_w = state->omega_rad_s;
	if (abs_w < 0.0f) {
		abs_w = -abs_w;
	}
	enter_w = quiet_enter_rad_s;
	exit_w = quiet_exit_rad_s;
	if (exit_w < enter_w) {
		exit_w = enter_w;
	}
	latch_need = VALVE_REST_LATCH_SAMPLES;
	if (settle_armed != 0U) {
		latch_need = VALVE_REST_LATCH_SETTLE_SAMPLES;
	}
	if (state->quiet_active != 0U) {
		if (abs_w > exit_w) {
			state->quiet_active = 0U;
			rest_latch_count = 0;
		}
	} else if (abs_w < enter_w) {
		if (rest_latch_count < 0xFFFFU) {
			rest_latch_count++;
		}
		if (rest_latch_count >= latch_need) {
			state->quiet_active = 1U;
			settle_armed = 0U;
			settle_timeout_count = 0U;
			settle_peak_abs = 0.0f;
			post_impact_blank = 0U;
			ring_flip_count = 0U;
			ring_flip_window = 0U;
		}
	} else {
		rest_latch_count = 0;
	}
}

/*
 * Mid-range residual: blank free-space while |ω| < blank_eff until quiet.
 * End-stop: wall_release_armed blanks free-space more broadly after contact
 * so rebound cannot re-excite a free ring (solid-stop feel).
 */
static bool
valve_update_settle_residual(struct valve_state *state,
    const struct valve_config *cfg)
{
	float abs_filt;
	float omega;
	float blank_eff;
	float dpt;
	float theta;
	float theta_off;
	float theta_on;
	float pen;
	int8_t sgn;
	bool mid_residual;
	bool wall_residual;

	if (state == NULL) {
		return false;
	}

	omega = state->omega_rad_s;
	abs_filt = omega;
	if (abs_filt < 0.0f) {
		abs_filt = -abs_filt;
	}

	/* Wall contact → arm wall-release; free-space re-entry restores friction */
	pen = 0.0f;
	if (cfg != NULL) {
		dpt = cfg->degrees_per_turn;
		if (dpt <= 0.0f) {
			dpt = VALVE_DEFAULT_DEGREES_PER_TURN;
		}
		theta = state->position_deg / dpt;
		theta_off = cfg->closed_position_deg / dpt;
		theta_on = cfg->open_position_deg / dpt;
		if (theta < theta_off) {
			pen = theta_off - theta;
		} else if (theta > theta_on) {
			pen = theta - theta_on;
		}
		if (pen >= 1e-6f) {
			wall_release_armed = 1U;
			free_space_restore = 0U;
			post_impact_blank = 0U;
			settle_armed = 1U;
			settle_timeout_count = VALVE_SETTLE_TIMEOUT_SAMPLES;
		} else if (wall_release_armed != 0U) {
			if (abs_filt > VALVE_WALL_REENTRY_FREE_RAD_S) {
				/*
				 * Quick pull back into 0–90°: drop wall blank AND
				 * residual settle so free-space b/τc stay continuous
				 * while speed varies (mid blank was grindy until sit).
				 */
				wall_release_armed = 0U;
				free_space_restore = 1U;
				post_impact_blank = 0U;
				settle_armed = 0U;
				settle_timeout_count = 0U;
				settle_peak_abs = 0.0f;
				ring_flip_count = 0U;
				ring_flip_window = 0U;
			} else if (abs_filt > quiet_exit_rad_s) {
				/* Light free-space motion: clear wall blank only */
				wall_release_armed = 0U;
			}
		}
	}

	if (ring_flip_window > 0U) {
		ring_flip_window--;
		if (ring_flip_window == 0U) {
			ring_flip_count = 0U;
		}
	}
	if (abs_filt > quiet_enter_rad_s) {
		if (omega > 0.0f) {
			sgn = 1;
		} else if (omega < 0.0f) {
			sgn = -1;
		} else {
			sgn = 0;
		}
		if (sgn != 0 && ring_last_sign != 0 && sgn != ring_last_sign &&
		    abs_filt < VALVE_SETTLE_BLANK_WIDE_MAX_RAD_S) {
			if (ring_flip_count < 0xFFU) {
				ring_flip_count++;
			}
			ring_flip_window = VALVE_RING_FLIP_WINDOW_SAMPLES;
			if (ring_flip_count >= VALVE_RING_FLIP_COUNT) {
				settle_armed = 1U;
				settle_timeout_count = VALVE_SETTLE_TIMEOUT_SAMPLES;
				/* Ring residual → blank free-space until quiet */
				post_impact_blank = 1U;
			}
		}
		if (sgn != 0) {
			ring_last_sign = sgn;
		}
	} else {
		ring_last_sign = 0;
	}

	if (abs_filt >= valve_auto_settle_arm()) {
		settle_armed = 1U;
		settle_timeout_count = VALVE_SETTLE_TIMEOUT_SAMPLES;
	}

	if (state->quiet_active != 0U) {
		settle_armed = 0U;
		settle_timeout_count = 0U;
		settle_peak_abs = 0.0f;
		wall_release_armed = 0U;
		free_space_restore = 0U;
		post_impact_blank = 0U;
		ring_flip_count = 0U;
		ring_flip_window = 0U;
		return false;
	}

	/*
	 * Intentional free-space after over-travel: do not blank free-space.
	 */
	if (free_space_restore != 0U) {
		post_impact_blank = 0U;
		return false;
	}

	/*
	 * Wall-release residual: blank free-space only while coasting slowly
	 * after wall contact (not during intentional re-entry into legal range).
	 */
	wall_residual = false;
	if (wall_release_armed != 0U) {
		if (abs_filt > quiet_enter_rad_s) {
			if (settle_timeout_count < 1200U) {
				settle_timeout_count = 1200U;
			}
		} else if (settle_timeout_count > 0U) {
			settle_timeout_count--;
		} else {
			wall_release_armed = 0U;
		}
		if (wall_release_armed != 0U &&
		    abs_filt < valve_auto_settle_blank()) {
			wall_residual = true;
		}
	}

	/* --- Mid-range / post-tap residual path --- */
	mid_residual = false;
	if (settle_armed != 0U) {
		if (abs_filt > settle_peak_abs) {
			settle_peak_abs = abs_filt;
		}
		if (abs_filt > quiet_enter_rad_s) {
			if (settle_timeout_count < 800U) {
				settle_timeout_count = 800U;
			}
		} else if (settle_timeout_count > 0U && wall_release_armed == 0U) {
			settle_timeout_count--;
		} else if (settle_timeout_count == 0U && wall_release_armed == 0U) {
			settle_armed = 0U;
			settle_peak_abs = 0.0f;
		}

		/* Arm residual coast blank after firm tap/flick peak */
		{
			float tap_peak = VALVE_SETTLE_TAP_PEAK_BASELINE_RAD_S;

			if (valve_auto_at_baseline() == 0U) {
				tap_peak = VALVE_SETTLE_TAP_PEAK_ELEVATED_RAD_S;
			}
			if (settle_peak_abs >= tap_peak) {
				post_impact_blank = 1U;
			}
		}
	} else {
		settle_peak_abs = 0.0f;
	}

	/*
	 * Free-space residual blank ONLY while coasting slowly after impact.
	 * While the user (or residual) is moving with |ω| ≥ blank, free-space
	 * b/τc stay ON so resistance is present. Cleared on quiet / restore.
	 */
	blank_eff = valve_auto_settle_blank();
	if (post_impact_blank != 0U) {
		if (abs_filt < blank_eff) {
			mid_residual = true;
		}
	} else if (settle_armed != 0U &&
	    settle_peak_abs >= VALVE_SETTLE_PEAK_BLANK_RAD_S &&
	    valve_auto_at_baseline() != 0U) {
		/* Baseline: milder residual coast blank */
		if (abs_filt < blank_eff) {
			mid_residual = true;
		}
	}

	return (wall_residual || mid_residual);
}

/* Clamp filter alpha to [0,1] for stability and to prevent invalid filter behavior */
static inline float clamp_alpha(float alpha)
{
	if (alpha < 0.0f) return 0.0f;
	if (alpha > 1.0f) return 1.0f;
	return alpha;
}

/* Safe square root with domain checking to avoid NaN in physics calculations */
static inline float safe_sqrtf(float x)
{
	if (x < 0.0f) {
		/* Domain error: sqrt of negative number */
		return 0.0f;
	}
	float result;
	arm_status status = arm_sqrt_f32(x, &result);
	if (status != ARM_MATH_SUCCESS) {
		return 0.0f;
	}
	return result;
}

/* Safe absolute value function for consistency and MISRA compliance */
static inline float safe_fabsf(float x)
{
	return (x < 0.0f) ? -x : x;
}

/* Clamp torque to symmetric limits and indicate if clamping occurred for diagnostics */
static inline bool clamp_torque(float *torque, float limit)
{
	if (*torque > limit) {
		*torque = limit;
		return true;
	}
	if (*torque < -limit) {
		*torque = -limit;
		return true;
	}
	return false;
}

/* Compute low-pass filter alpha coefficient from cutoff frequency for velocity filtering */
static inline float valve_lowpass_alpha(float cutoff_hz, float dt_s)
{
	if (cutoff_hz <= 0.0f) {
		return 1.0f;
	}
	const float two_pi = VALVE_TWO_PI;
	const float rc = 1.0f / (two_pi * cutoff_hz);
	float alpha = dt_s / (rc + dt_s);
	return clamp_alpha(alpha);
}

/*
 * DWT Cycle Counter Functions
 * Used for precise timing measurements (CPU cycles @ 400 MHz)
 */

/* Initialize DWT cycle counter for performance profiling */
static inline void dwt_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

/* Get current cycle count for timing measurements */
static inline uint32_t dwt_get_cycles(void)
{
    return DWT->CYCCNT;
}

/* Convert cycles to microseconds for human-readable timing data */
static inline uint32_t dwt_cycles_to_us(uint32_t cycles)
{
    return cycles / VALVE_CPU_CLOCK_MHZ;
}

/*
 * TIM6 Configuration
 * Configure TIM6 for VALVE_CONTROL_LOOP_HZ interrupt rate
 * APB1 timer clock = 200 MHz (check RCC configuration)
 * Prescaler = 199 (200 MHz / 200 = 1 MHz)
 * ARR = (1 MHz / VALVE_CONTROL_LOOP_HZ) - 1
 */

/* Configure TIM6 registers for the specified interrupt frequency to drive the control loop */
static void tim6_configure_for_hz(uint32_t hz)
{
    /* Calculate period in timer ticks */
    uint32_t period_ticks = (TIM6_BASE_FREQUENCY_HZ / hz) - 1U;
    
    /* Configure timer */
    htim6->PSC = TIM6_PRESCALER;  /* Prescaler: 200 MHz / 200 = 1 MHz */
    htim6->ARR = period_ticks;
    htim6->CR1 = TIM_CR1_ARPE;    /* Auto-reload preload enable */
    
    /* Enable update interrupt */
    htim6->DIER = TIM_DIER_UIE;
}

/* Initialize TIM6 hardware and NVIC for periodic control loop interrupts */
static void tim6_init(void)
{
    /* Enable TIM6 clock */
    RCC->APB1LENR |= RCC_APB1LENR_TIM6EN;
    (void)RCC->APB1LENR;  /* Read back to ensure write completes */
    
    /* Configure TIM6 for control loop frequency */
    tim6_configure_for_hz(VALVE_CONTROL_LOOP_HZ);
    
    /* Configure NVIC
     * Priority 5 = HIGH (higher than FDCAN=6, UART=5)
	* Critical: TIM6 must preempt FDCAN to maintain 1000 Hz loop timing
     * Lower number = higher priority on Cortex-M
     */
    NVIC_SetPriority(TIM6_DAC_IRQn, 5);
    NVIC_EnableIRQ(TIM6_DAC_IRQn);
}

/* Start TIM6 timer to begin generating control loop interrupts */
static void tim6_start(void)
{
    htim6->CR1 |= TIM_CR1_CEN;  /* Enable counter */
}

/* Stop TIM6 timer to halt control loop execution */
static void tim6_stop(void)
{
    htim6->CR1 &= ~TIM_CR1_CEN;  /* Disable counter */
}

/* Wait for ODrive to enter the required control and input modes before starting closed-loop control */
static bool valve_wait_for_controller_mode(struct valve_state *state,
	uint8_t desired_control_mode,
	uint8_t desired_input_mode)
{
	struct can_simple_heartbeat hb;
	uint32_t hb_age_ms = 0U;
	uint32_t start_ms = board_get_systick_ms();

	while ((board_get_systick_ms() - start_ms) < 300U) { /* up to 300 ms */
		if (can_simple_get_cached_heartbeat(state->odrive, &hb, &hb_age_ms) == STATUS_OK &&
		    hb_age_ms < VALVE_HEARTBEAT_TIMEOUT_MS) {
			uint8_t reported_control = hb.controller_status & 0x0FU;
			uint8_t reported_input = (hb.controller_status >> 4) & 0x0FU;
			if (reported_control == desired_control_mode &&
			    reported_input == desired_input_mode) {
				state->diag.heartbeat_age_ms = hb_age_ms;
				return true;
			}
		}
		board_delay_ms(10);
	}

	return false;
}

/*
 * FDCAN error callback - invoked from ISR context on bus-off/error passive
 *
 * Minimal handler: stop TIM6, send single ESTOP, set ERROR state.
 * No delays, no blocking operations.
 */

/* Handle FDCAN bus errors by immediately stopping control and sending emergency stop */
static void valve_fdcan_error_callback(uint8_t error_code, void *context)
{
	struct valve_state *state = (struct valve_state *)context;
	
	(void)error_code;  /* Could log specific error, but minimal for ISR */
	
	/* Stop TIM6 immediately */
	TIM6->CR1 &= ~TIM_CR1_CEN;
	
	/* Send single non-blocking ESTOP */
	if (state->odrive != NULL) {
		(void)can_simple_estop_nb(state->odrive);
	}
	
	/* Set error state */
	state->status = VALVE_STATE_ERROR;
	        state->diag.last_can_status = STATUS_ERROR_TIMEOUT;
}

/* Initialize valve haptic system with ODrive handle and default configuration for safe operation */
status_t valve_haptic_init(struct valve_context *ctx, struct can_simple_handle *odrive)
{
    if (ctx == NULL || odrive == NULL) {
        return STATUS_ERROR_INVALID_PARAM;
    }
    
    /* Only init if not already initialized (preserve loaded preset) */
    if (ctx->state.odrive == odrive && (ctx->state.status & VALVE_STATE_ERROR) == 0) {
        return STATUS_OK;  /* Already initialized with same handle */
    }

    /* Initialize simplified state */
	ctx->state = (struct valve_state){
		.position_deg = 0.0f,
		.omega_rad_s = 0.0f,
		.omega_raw_rad_s = 0.0f,
		.alpha_rad_s2 = 0.0f,
		.prev_omega_rad_s = 0.0f,
		.torque_nm = 0.0f,
		.command_position_deg = 0.0f,
		.raw_position_turns = 0.0f,
		.degrees_per_turn = VALVE_DEFAULT_DEGREES_PER_TURN,
		.previous_torque_nm = 0.0f,
		.status = VALVE_STATE_IDLE,
		.quiet_active = 0U,
        .diag = {
            .loop_count = 0,
            .telemetry_age_ms = UINT32_MAX,
            .heartbeat_age_ms = UINT32_MAX,
            .last_can_status = STATUS_OK,
            .can_retry_count = 0,
            .safety = {
                .peak_fet_temperature_c = 0.0f,
                .peak_motor_temperature_c = 0.0f,
            },
        },
        .odrive = odrive,
		.encoder_zero_turns = 0.0f,
    };
	
	memset(&ctx->config, 0, sizeof(ctx->config));
	memset(&ctx->staged_config, 0, sizeof(ctx->staged_config));
	ctx->staged_field_mask = 0U;
	ctx->staged_pending = 0U;
	ctx->config.degrees_per_turn = VALVE_DEFAULT_DEGREES_PER_TURN;
	
	/* Load default preset (light resistance) for basic functionality */
	status_t preset_status = valve_haptic_load_preset(ctx, VALVE_PRESET_LIGHT, 0.0f);
	if (preset_status != STATUS_OK) {
		return preset_status;  /* Failed to load default preset */
	}
	
    active_valve_context = ctx;
	
	/* Register FDCAN error callback for immediate bus-off detection */
	struct fdcan_handle *fdcan = fdcan_get_handle();
	fdcan_set_error_callback(fdcan, valve_fdcan_error_callback, &ctx->state);

	
	return STATUS_OK;
}

/*
 * valve_haptic_load_preset - Load preset configuration
 *
 * Generates physics parameters from preset index and travel range.
 */
status_t
valve_haptic_load_preset(struct valve_context *ctx, int preset, float travel_degrees)
{
	float prev_deg_per_turn;
	status_t result;

	if (ctx == NULL)
		return STATUS_ERROR_INVALID_PARAM;

	prev_deg_per_turn = (ctx->config.degrees_per_turn > 0.0f) ?
	    ctx->config.degrees_per_turn : VALVE_DEFAULT_DEGREES_PER_TURN;

	result = valve_preset_from_preset(preset, travel_degrees, &ctx->config);
	if (result != STATUS_OK)
		return result;

	ctx->config.degrees_per_turn = prev_deg_per_turn;

	return valve_preset_validate(&ctx->config);
}

/* Stage configuration changes for atomic application during runtime to avoid disrupting control */
status_t valve_haptic_stage_config(struct valve_context *ctx, const struct valve_config *cfg, uint32_t field_mask)
{
    if (ctx == NULL || cfg == NULL) {
        return STATUS_ERROR_INVALID_PARAM;
    }

    status_t validation = valve_preset_validate(cfg);
    if (validation != STATUS_OK) {
        return validation;
    }

    if (ctx->state.status == VALVE_STATE_RUNNING) {
        uint32_t primask = valve_enter_critical();
        ctx->staged_config = *cfg;
        ctx->staged_field_mask = field_mask;
        ctx->staged_pending = 1U;
        valve_exit_critical(primask);
        return STATUS_OK;
    }

    ctx->config = *cfg;
    ctx->state.degrees_per_turn = (ctx->config.degrees_per_turn > 0.0f) ?
        ctx->config.degrees_per_turn : VALVE_DEFAULT_DEGREES_PER_TURN;
    ctx->staged_pending = 0U;
    ctx->staged_field_mask = 0U;
    valve_auto_params_update(&ctx->config);
    return STATUS_OK;
}

/*
 * Start valve control loop (torque-control, purely resistive model)
 *
 * Initialization sequence:
 * 1. Verify preset is loaded and state is IDLE
 * 2. Validate configuration limits
 * 3. Program ODrive controller/input modes for torque control passthrough
 * 4. Apply velocity/current limits derived from the preset torque limit
 * 5. Transition the axis to CLOSED_LOOP_CONTROL and confirm via heartbeat
 * 6. Sample encoder once to establish zero and seed velocity filters
 * 7. Start TIM6 timer and enter RUNNING state for autonomous 1 kHz operation
 *
 * Returns STATUS_OK on success, error code on failure
 */

/* Start the haptic valve control loop with ODrive initialization and encoder setup */
status_t valve_haptic_start(struct valve_context *ctx)
{
    struct valve_state *state = &ctx->state;
	status_t validation_result;
    
    /* Verify we have an Odrive handle */
    if (state->odrive == NULL) {
        return STATUS_ERROR_INVALID_PARAM;  /* No Odrive handle */
    }

    if (state->status == VALVE_STATE_ERROR) {
	    valve_haptic_stop(ctx);
    }
    
    /* Verify state is IDLE */
    if (state->status != VALVE_STATE_IDLE) {
        return STATUS_ERROR_BUSY;  /* Not in IDLE state */
    }
    
    ctx->staged_pending = 0U;
    ctx->staged_field_mask = 0U;
    
    /* Simplified: no preset validation */
    
	/* Validate configuration before starting */
	validation_result = valve_preset_validate(&ctx->config);
	if (validation_result == STATUS_OK) {
		valve_auto_params_update(&ctx->config);
	}
	if (validation_result != STATUS_OK) {
		return STATUS_ERROR_INVALID_CONFIG;  /* Configuration invalid */
	}

	state->degrees_per_turn = (ctx->config.degrees_per_turn > 0.0f) ?
		ctx->config.degrees_per_turn : VALVE_DEFAULT_DEGREES_PER_TURN;

	
	state->diag.last_can_status = STATUS_OK;
	state->diag.can_retry_count = 0;

	if (ctx->output_mode == VALVE_OUTPUT_MODE_ODRIVE) {
		/* ---- Physical ODrive path ------------------------------------ */

		/* Clear any existing errors first */
		can_simple_clear_errors(state->odrive);

		/* Configure torque control with passthrough BEFORE enabling closed loop */
		if (can_simple_set_controller_mode(state->odrive,
		        CONTROL_MODE_TORQUE_CONTROL,
		        INPUT_MODE_PASSTHROUGH) != 0) {
			state->status = VALVE_STATE_IDLE;
			return STATUS_ERROR_HARDWARE_FAULT;  /* Error code 7: Failed to set controller mode */
		}

		/* Confirm controller/input modes actually changed via heartbeat. */
		if (!valve_wait_for_controller_mode(state,
		        CONTROL_MODE_TORQUE_CONTROL,
		        INPUT_MODE_PASSTHROUGH)) {
			state->diag.last_can_status = STATUS_ERROR_TIMEOUT;
		}

		/* Set limits based on loaded preset configuration */
		if (ctx->config.torque_limit_nm > 0.0f) {
			float current_limit = (ctx->config.torque_limit_nm / ODRIVE_TORQUE_CONSTANT_NM_PER_A) + ODRIVE_CURRENT_HEADROOM_A;
			if (current_limit > VALVE_ODRIVE_CURRENT_LIMIT_A) {
				current_limit = VALVE_ODRIVE_CURRENT_LIMIT_A;
			}
			if (can_simple_set_limits(state->odrive, VALVE_ODRIVE_VEL_LIMIT_TURNS_PER_S, current_limit) != STATUS_OK) {
				state->status = VALVE_STATE_IDLE;
				return STATUS_ERROR_BUSY;
			}
		}

		/* Set Odrive to CLOSED_LOOP_CONTROL */
		if (can_simple_set_axis_state(state->odrive, AXIS_STATE_CLOSED_LOOP_CONTROL) != 0) {
			state->status = VALVE_STATE_IDLE;
			return STATUS_ERROR_NOT_SUPPORTED;
		}

		/* Poll heartbeat for up to 500ms waiting for closed loop state */
		struct can_simple_heartbeat hb;
		uint32_t hb_age_ms = 0U;
		uint32_t wait_start_ms = board_get_systick_ms();
		uint8_t state_achieved = 0U;

		while ((board_get_systick_ms() - wait_start_ms) < 500U) {
			if (can_simple_get_cached_heartbeat(state->odrive, &hb, &hb_age_ms) == STATUS_OK &&
			    hb_age_ms < VALVE_HEARTBEAT_TIMEOUT_MS) {
				if (hb.axis_error != 0) {
					can_simple_set_axis_state(state->odrive, AXIS_STATE_IDLE);
					state->status = VALVE_STATE_IDLE;
					return STATUS_ERROR_BUFFER_FULL;
				}
				if (hb.axis_state == AXIS_STATE_CLOSED_LOOP_CONTROL) {
					state_achieved = 1U;
					state->diag.heartbeat_age_ms = hb_age_ms;
					break;
				}
			}
			board_delay_ms(10);
		}

		if (!state_achieved) {
			can_simple_set_axis_state(state->odrive, AXIS_STATE_IDLE);
			state->status = VALVE_STATE_IDLE;
			return STATUS_ERROR_BUFFER_EMPTY;
		}

		/* Get initial encoder position from cached data */
		struct can_simple_encoder_estimates est;
		uint32_t encoder_age_ms = 0;
		status_t enc_status;
		uint32_t enc_wait_start_ms = board_get_systick_ms();
		const uint32_t enc_wait_timeout_ms = 200U;

		do {
			enc_status = can_simple_get_cached_encoder(state->odrive, &est, &encoder_age_ms, NULL);
			if (enc_status == STATUS_OK) {
				break;
			}
			board_delay_ms(5);
		} while ((board_get_systick_ms() - enc_wait_start_ms) < enc_wait_timeout_ms);

		if (enc_status != STATUS_OK) {
			can_simple_set_axis_state(state->odrive, AXIS_STATE_IDLE);
			state->status = VALVE_STATE_IDLE;
			return enc_status;
		}

		/* Seed: zero at start pose */
		state->encoder_zero_turns = est.position;
		state->raw_position_turns = est.position;
		state->position_deg       = 0.0f;
		state->command_position_deg = 0.0f;
		settle_armed = 0U;
		settle_timeout_count = 0U;
		settle_peak_abs = 0.0f;
		wall_release_armed = 0U;
		free_space_restore = 0U;
		rest_latch_count = 0;
		ring_last_sign = 0;
		ring_flip_count = 0U;
		ring_flip_window = 0U;
		float turn_to_rad = state->degrees_per_turn * VALVE_DEG_TO_RAD;
		state->omega_rad_s = est.velocity * turn_to_rad;
		if (state->omega_rad_s < (0.1f * VALVE_DEG_TO_RAD) &&
		    state->omega_rad_s > -(0.1f * VALVE_DEG_TO_RAD)) {
			state->omega_rad_s = 0.0f;
		}
		state->omega_raw_rad_s = state->omega_rad_s;

	} else {
		/* ---- HITL path (Isaac Sim) ----------------------------------- *
		 * No CAN bus, no ODrive. Seed state from zero; the first encoder *
		 * packet from Isaac Sim will update position_deg / omega_rad_s   *
		 * before the physics model does anything meaningful.             */
		state->encoder_zero_turns   = 0.0f;
		state->raw_position_turns   = 0.0f;
		state->position_deg         = 0.0f;
		state->command_position_deg = 0.0f;
		state->omega_rad_s          = 0.0f;
		state->omega_raw_rad_s      = 0.0f;
	}
	valve_velocity_filters_seed(state->omega_rad_s);
	state->prev_omega_rad_s = state->omega_rad_s;
	state->alpha_rad_s2 = 0.0f;
	state->quiet_active = (state->omega_rad_s == 0.0f) ? 1U : 0U;

	/* Clear diagnostics */
	state->diag.loop_count = 0;
	state->diag.can_retry_count = 0;
	state->diag.telemetry_age_ms = 0;  /* Fresh data */
	state->diag.heartbeat_age_ms = 0;
	state->diag.t_us_accum = 0ULL;
	state->diag.last_loop_time_us = 0U;
	state->diag.sample_seq = 0U;
	state->torque_nm = 0.0f;
	state->previous_torque_nm = 0.0f;
	state->filtered_torque_nm = 0.0f;
	state->passivity_energy_j = 0.0f;
    
    /* Initialize DWT cycle counter for timing measurements */
    dwt_init();
    
    /* Initialize and start TIM6 */
    tim6_init();
    tim6_start();
    
    /* Enter RUNNING state */
    state->status = VALVE_STATE_RUNNING;
    
    return STATUS_OK;
}

/*
 * Stop valve control loop
 *
 * 1. Stop TIM6 timer (Phase 4)
 * 2. Set Odrive to IDLE
 * 3. Clear control loop flag
 * 4. Return to IDLE state
 */

/* Stop the haptic valve control loop and return ODrive to idle state for safe shutdown */
void valve_haptic_stop(struct valve_context *ctx)
{
    struct valve_state *state = &ctx->state;
    
	valve_velocity_filters_invalidate(state);
    
    /* Stop TIM6 timer */
    tim6_stop();
    
	/* SAFETY: Abort pending TX and command zero torque before setting IDLE */
	if (state->odrive != NULL) {
		can_simple_abort_all_tx(state->odrive);
		(void)can_simple_set_input_torque_nb(state->odrive, 0.0f);
		(void)can_simple_set_axis_state_nb(state->odrive, AXIS_STATE_IDLE);
	}
	
	state->status = VALVE_STATE_IDLE;
	state->quiet_active = 0U;
	settle_armed = 0U;
	settle_timeout_count = 0U;
	settle_peak_abs = 0.0f;
	wall_release_armed = 0U;
	free_space_restore = 0U;
	runaway_omega_count = 0U;
	free_space_omega_filt = 0.0f;
	free_space_omega_filt_init = 0U;
	post_impact_blank = 0U;
	rest_latch_count = 0;
	ring_last_sign = 0;
	ring_flip_count = 0U;
	ring_flip_window = 0U;
	state->diag.can_retry_count = 0;
	state->diag.telemetry_age_ms = UINT32_MAX;
	state->diag.heartbeat_age_ms = UINT32_MAX;
	state->diag.safety.last_error_code = (uint32_t)VALVE_ERROR_NONE;
}

/* Emergency stop with ESTOP command for immediate shutdown in critical safety situations */
static void valve_haptic_emergency_stop(struct valve_context *ctx)
{
    struct valve_state *state = &ctx->state;
    
	valve_velocity_filters_invalidate(state);
    
    /* Stop TIM6 timer */
    tim6_stop();
    
	/* EMERGENCY: Abort pending TX, command zero torque, and ESTOP */
	if (state->odrive != NULL) {
		can_simple_abort_all_tx(state->odrive);
		(void)can_simple_set_input_torque_nb(state->odrive, 0.0f);
		(void)can_simple_estop_nb(state->odrive);
	}
	
	/* Set error state */
	state->status = VALVE_STATE_ERROR;
	state->quiet_active = 0U;
	settle_armed = 0U;
	settle_timeout_count = 0U;
	settle_peak_abs = 0.0f;
	wall_release_armed = 0U;
	free_space_restore = 0U;
	runaway_omega_count = 0U;
	free_space_omega_filt = 0.0f;
	free_space_omega_filt_init = 0U;
	post_impact_blank = 0U;
	rest_latch_count = 0;
	ring_last_sign = 0;
	ring_flip_count = 0U;
	ring_flip_window = 0U;
	state->diag.can_retry_count = 0;
	state->diag.telemetry_age_ms = UINT32_MAX;
	state->diag.heartbeat_age_ms = UINT32_MAX;
}

/* Stop the loop and record a CAN failure for post-mortem visibility and error tracking */
static void valve_handle_can_failure(struct valve_context *ctx, status_t error_code)
{
	if (ctx == NULL) {
		return;
	}

	struct valve_state *state = &ctx->state;

	valve_haptic_stop(ctx);
	state->status = VALVE_STATE_ERROR;
	state->diag.last_can_status = error_code;
	state->diag.safety.last_error_code = (uint32_t)VALVE_ERROR_CAN;
	state->diag.safety.last_error_timestamp_ms = board_get_systick_ms();
	state->diag.safety.emergency_stops++;
}

/*
 * Process control loop iteration
 * Called from main loop when valve_control_flag is set by TIM6 ISR
 *
 * Control loop sequence:
 * 1. Poll encoder position/velocity (throttled) or propagate estimate
 * 2. Verify/clamp position
 * 3. Calculate physics torque
 * 4. Apply torque command
 * 5. Measure timing and track CAN failures
 */

/*
 * Reads cached encoder data from ODrive broadcasts and updates state.
 * The S1 endpoint broadcasts at 1kHz, allowing the control loop to
 * run synchronously without blocking on CAN transactions.
 */

/* Process incoming encoder data from ODrive to update position and velocity estimates */
static status_t
valve_process_encoder_data(struct valve_state *state)
{
	struct can_simple_encoder_estimates obs;
	uint32_t age_ms = UINT32_MAX;
	status_t obs_status;

	/* Get cached encoder data (S1 broadcasts at 1kHz automatically) */
	obs_status = can_simple_get_cached_encoder(state->odrive, &obs, &age_ms, NULL);
	if (obs_status != STATUS_OK) {
		state->diag.telemetry_age_ms = UINT32_MAX;
		state->diag.can_retry_count++;
		if (state->diag.can_retry_count >= VALVE_CAN_FAILURE_MAX) {
			valve_velocity_filters_invalidate(state);
			return STATUS_ERROR_TIMEOUT;
		}
		return STATUS_ERROR_BUFFER_EMPTY;
	}

	state->diag.telemetry_age_ms = age_ms;
	
	/* Track encoder data age statistics (convert ms to µs for consistency) */
	uint32_t age_us = age_ms * 1000U;
	if (state->diag.encoder_age_count == 0U) {
		/* First sample - initialize */
		state->diag.encoder_age_min_us = age_us;
		state->diag.encoder_age_max_us = age_us;
		state->diag.encoder_age_sum_us = age_us;
		state->diag.encoder_age_count = 1U;
	} else {
		/* Update running statistics */
		if (age_us < state->diag.encoder_age_min_us) {
			state->diag.encoder_age_min_us = age_us;
		}
		if (age_us > state->diag.encoder_age_max_us) {
			state->diag.encoder_age_max_us = age_us;
		}
		state->diag.encoder_age_sum_us += age_us;
		state->diag.encoder_age_count++;
		
		/* Prevent overflow - reset every ~1M samples */
		if (state->diag.encoder_age_count >= 1000000U) {
			state->diag.encoder_age_min_us = age_us;
			state->diag.encoder_age_max_us = age_us;
			state->diag.encoder_age_sum_us = age_us;
			state->diag.encoder_age_count = 1U;
		}
	}

	/* Reject stale encoder data */
	if (age_ms > VALVE_ENCODER_TIMEOUT_MS) {
		state->diag.can_retry_count++;
		valve_velocity_filters_invalidate(state);
		if (state->diag.can_retry_count >= VALVE_CAN_FAILURE_MAX) {
			return STATUS_ERROR_TIMEOUT;
		}
		return STATUS_ERROR_BUFFER_EMPTY;
	}
	if (age_ms > VALVE_ENCODER_STALE_MS) {
		state->diag.can_retry_count++;
		if (state->diag.can_retry_count >= VALVE_CAN_FAILURE_MAX) {
			valve_velocity_filters_invalidate(state);
			return STATUS_ERROR_TIMEOUT;
		}
		return STATUS_ERROR_BUFFER_EMPTY;
	}

	const float deg_per_turn = (state->degrees_per_turn > 0.0f) ?
		state->degrees_per_turn : VALVE_DEFAULT_DEGREES_PER_TURN;
	float prev_turns = state->raw_position_turns;
	float delta_turns = obs.position - prev_turns;
	float abs_delta;
	uint8_t glitch = 0U;

	/*
	 * Reject impossible single-sample jumps (CAN/glitch). Hold previous
	 * position/velocity so −b·ω cannot spike.
	 */
	abs_delta = delta_turns;
	if (abs_delta < 0.0f) {
		abs_delta = -abs_delta;
	}
	if (abs_delta > VALVE_ENCODER_DELTA_MAX_TURNS) {
		glitch = 1U;
		delta_turns = 0.0f;
		/* Do not advance raw_position on glitch — next good sample OK */
	} else {
		state->raw_position_turns = obs.position;
		state->position_deg = (obs.position - state->encoder_zero_turns) *
		    deg_per_turn;
	}

	float vel_delta = delta_turns * deg_per_turn * VALVE_DEG_TO_RAD *
	    (float)VALVE_CONTROL_LOOP_HZ;
	float vel_odrive = obs.velocity * deg_per_turn * VALVE_DEG_TO_RAD;
	/* Hand-scale clamp (not ODrive 20 turn/s) */
	const float max_vel_rad_s = VALVE_PHYSICS_OMEGA_MAX_RAD_S;
	if (vel_delta > max_vel_rad_s) {
		vel_delta = max_vel_rad_s;
	} else if (vel_delta < -max_vel_rad_s) {
		vel_delta = -max_vel_rad_s;
	}
	if (vel_odrive > max_vel_rad_s) {
		vel_odrive = max_vel_rad_s;
	} else if (vel_odrive < -max_vel_rad_s) {
		vel_odrive = -max_vel_rad_s;
	}

	{
		/* Fixed 30 Hz velocity LPF (CLI override still works at baseline) */
		float lpf_hz = velocity_lpf_hz;
		float alpha;
		float vel_out;
		uint8_t src = velocity_source;

		if (lpf_hz < 1.0f) {
			lpf_hz = VALVE_VELOCITY_LPF_CUTOFF_HZ;
		}
		alpha = valve_lowpass_alpha(lpf_hz, VALVE_LOOP_DT_S);

		if (glitch != 0U) {
			/* Hold filtered velocity through glitch sample */
			vel_out = state->omega_rad_s;
			vel_delta = state->omega_raw_rad_s;
		} else if (src == VALVE_VEL_SOURCE_ODRIVE) {
			vel_out = vel_odrive;
			if (!velocity_filters_initialized) {
				valve_velocity_filters_seed(vel_out);
			} else {
				(void)simple_lowpass(vel_delta, &velocity_filter_state, alpha);
			}
		} else if (src == VALVE_VEL_SOURCE_LPF_DELTA) {
			if (!velocity_filters_initialized) {
				valve_velocity_filters_seed(vel_delta);
				vel_out = vel_delta;
			} else {
				vel_out = simple_lowpass(vel_delta, &velocity_filter_state,
				    alpha);
			}
		} else {
			vel_out = vel_delta;
			if (!velocity_filters_initialized) {
				valve_velocity_filters_seed(vel_delta);
			} else {
				(void)simple_lowpass(vel_delta, &velocity_filter_state, alpha);
			}
		}

		{
			float prev_omega = state->omega_rad_s;
			state->omega_rad_s = vel_out;
			state->omega_raw_rad_s = vel_delta;
			state->prev_omega_rad_s = prev_omega;
			state->alpha_rad_s2 = (state->omega_rad_s - prev_omega) *
			    (float)VALVE_CONTROL_LOOP_HZ;
		}
	}
	valve_update_quiet_gate(state);
	state->diag.can_retry_count = 0;

	return STATUS_OK;
}

/* Update diagnostic counters and performance monitoring data after each control loop iteration */
static void valve_update_diagnostics(struct valve_state *state, float torque, uint32_t t_start, uint16_t loop_period_us)
{
	(void)loop_period_us;  /* Unused - timing calculated from DWT cycles */
	
	/* Calculate loop execution time in microseconds */
	uint32_t t_end = dwt_get_cycles();
	uint32_t elapsed_cycles = t_end - t_start;
	uint32_t elapsed_us = dwt_cycles_to_us(elapsed_cycles);

	/* Expose instantaneous loop time and accumulate monotonic time for streaming */
	state->diag.last_loop_time_us = elapsed_us;
	state->diag.t_us_accum += (uint64_t)elapsed_us;
	state->diag.sample_seq++;
	
	/* Update timing statistics with simple min/max/sum tracking */
	if (state->diag.timing_sample_count == 0U) {
		/* First sample - initialize */
		state->diag.loop_time_min_us = elapsed_us;
		state->diag.loop_time_max_us = elapsed_us;
		state->diag.loop_time_sum_us = elapsed_us;
		state->diag.timing_sample_count = 1U;
	} else {
		/* Update running statistics (branchless for speed) */
		if (elapsed_us < state->diag.loop_time_min_us) {
			state->diag.loop_time_min_us = elapsed_us;
		}
		if (elapsed_us > state->diag.loop_time_max_us) {
			state->diag.loop_time_max_us = elapsed_us;
		}
		state->diag.loop_time_sum_us += elapsed_us;
		state->diag.timing_sample_count++;
		
		/* Prevent overflow by resetting stats every ~1M samples (~16 minutes at 1kHz) */
		if (state->diag.timing_sample_count >= 1000000U) {
			state->diag.loop_time_min_us = elapsed_us;
			state->diag.loop_time_max_us = elapsed_us;
			state->diag.loop_time_sum_us = elapsed_us;
			state->diag.timing_sample_count = 1U;
		}
	}
	
	/* Update basic state */
	state->torque_nm = torque;
	state->diag.loop_count++;
	state->diag.can_retry_count = 0;
}

/*
 * Core haptic control loop, called from TIM6 ISR at 1kHz.
 * The fixed-rate execution is critical for stability - variable timing
 * would cause the discrete-time physics model to diverge from reality.
 */
void
valve_haptic_process(struct valve_context *ctx)
{
	struct valve_state *state;
	struct valve_config *cfg;
	uint32_t t_start;

	state = &ctx->state;
	if (state->status != VALVE_STATE_RUNNING)
		return;

	valve_apply_staged_config(ctx);
	cfg = &ctx->config;

	t_start = dwt_get_cycles();

	/* Process encoder data and check for fresh samples.
	 * HITL mode: skip CAN encoder read entirely — encoder data comes from
	 * Isaac Sim via hitl_server_get_encoder() later in the dispatch block. */
	if (ctx->output_mode == VALVE_OUTPUT_MODE_ODRIVE) {
		status_t encoder_status = valve_process_encoder_data(state);
		if (encoder_status != STATUS_OK) {
			/*
			 * Never leave last torque on the bus. Soft miss → zero;
			 * hard timeout after retries → fault stop.
			 */
			if (state->odrive != NULL) {
				(void)can_simple_set_input_torque_nb(state->odrive, 0.0f);
			}
			state->filtered_torque_nm = 0.0f;
			state->previous_torque_nm = 0.0f;
			state->torque_nm = 0.0f;
			if (encoder_status == STATUS_ERROR_TIMEOUT) {
				valve_handle_can_failure(ctx, encoder_status);
			}
			return;
		}

		/* Check ODrive status periodically (every 100ms)
		 * Read from cached heartbeat (S1 broadcasts at 100ms intervals) */
		uint32_t now_ms = board_get_systick_ms();
		if (now_ms - last_heartbeat_check_ms > 100) {
			struct can_simple_heartbeat hb;
			uint32_t hb_age_ms;
			if (can_simple_get_cached_heartbeat(state->odrive, &hb, &hb_age_ms) == STATUS_OK) {
				state->diag.heartbeat_age_ms = hb_age_ms;
				if (hb.axis_error != 0) {
					/* ODrive has an error - emergency stop valve */
					valve_haptic_emergency_stop(ctx);
					return;
				}
			}
			last_heartbeat_check_ms = now_ms;
		}

		/* Runaway: |ω| too high → ESTOP (cannot fling lever) */
		{
			float abs_w = state->omega_rad_s;
			if (abs_w < 0.0f) {
				abs_w = -abs_w;
			}
			if (abs_w >= VALVE_RUNAWAY_OMEGA_HARD_RAD_S) {
				runaway_omega_count = 0U;
				valve_haptic_emergency_stop(ctx);
				return;
			}
			if (abs_w >= VALVE_RUNAWAY_OMEGA_RAD_S) {
				if (runaway_omega_count < 0xFFFFU) {
					runaway_omega_count++;
				}
				if (runaway_omega_count >= VALVE_RUNAWAY_HOLD_SAMPLES) {
					runaway_omega_count = 0U;
					valve_haptic_emergency_stop(ctx);
					return;
				}
			} else {
				runaway_omega_count = 0U;
			}
		}
	} /* end ODRIVE-only encoder/heartbeat block */

	/* Mirror measured angle for tooling that still inspects command_position */
	state->command_position_deg = state->position_deg;

	/* === TORQUE COMMAND PATH === */
	bool settle_residual = valve_update_settle_residual(state, cfg);
	valve_update_quiet_gate(state);
	if (state->quiet_active != 0U) {
		settle_residual = false;
	}

	/* Same 30 Hz ω for free-space and wall (no extra free-space lag stage) */
	float torque_nm = valve_physics_calculate_torque_hil(cfg,
	    state->position_deg,
	    state->omega_rad_s,
	    state->omega_rad_s,
	    state->quiet_active != 0U,
	    settle_residual);

	float torque_limit = 0.0f;
	if (cfg->torque_limit_nm > 0.0f) {
	    torque_limit = cfg->torque_limit_nm;
	}

	float clamped_torque = valve_physics_clamp_torque(
	    torque_nm,
	    torque_limit);
	torque_nm = clamped_torque;

	if (state->quiet_active != 0U || settle_residual) {
		/* snap filter — no residual LPF memory buzz */
	} else {
		float prev_filtered = state->filtered_torque_nm;
		float lpf_hz = valve_auto_torque_lpf_hz();

		if (lpf_hz < 1.0f) {
			lpf_hz = VALVE_TORQUE_FILTER_CUTOFF_HZ;
		}
		torque_nm = valve_filter_lowpass_simple(
		    torque_nm,
		    prev_filtered,
		    lpf_hz,
		    VALVE_TORQUE_FILTER_SAMPLE_RATE_HZ);
	}

	if (ctx->output_mode == VALVE_OUTPUT_MODE_ODRIVE &&
	    state->quiet_active == 0U && !settle_residual) {
		const float dt_s = VALVE_LOOP_DT_S;
		float power_w = torque_nm * state->omega_rad_s;
		float delta_energy = power_w * dt_s;
		
		if (power_w <= 0.0f) {
			/* Store dissipative energy */
			state->passivity_energy_j += delta_energy;
			if (state->passivity_energy_j < -VALVE_PASSIVITY_ENERGY_CAP_J) {
				state->passivity_energy_j = -VALVE_PASSIVITY_ENERGY_CAP_J;
			}
		} else {
			/* Use stored energy for active torque */
			float available_energy = -state->passivity_energy_j;
			float max_allowed_power = available_energy / dt_s;
			
			if (max_allowed_power <= 0.0f) {
				torque_nm = 0.0f;
			} else if (power_w > max_allowed_power) {
				torque_nm = max_allowed_power / state->omega_rad_s;
			}
			
			/* Update energy tank with actual power used */
			delta_energy = torque_nm * state->omega_rad_s * dt_s;
			state->passivity_energy_j += delta_energy;
		}
		
		/* Maintain negative energy storage (never reset to zero) */
		if (state->passivity_energy_j > 0.0f) {
			state->passivity_energy_j = 0.0f;
		}
	} /* end passivity block (ODRIVE mode only) */

	/*
	 * No free-space torque slew. At elevated gains, slew delayed reverse
	 * during fast shake and pumped speed-specific oscillations. Safety is
	 * soft free-space sat + runaway ESTOP + encoder fail → zero torque.
	 */

	state->filtered_torque_nm = torque_nm;
	state->previous_torque_nm = torque_nm;

	/* Compute signed drive torque before dispatch */
	float drive_torque_nm = torque_nm * VALVE_TORQUE_SIGN;

	/* === TORQUE DISPATCH ===
	 *
	 * ODRIVE mode (default): send torque to physical motor via CAN.
	 * HITL mode:  queue torque for Isaac Sim (flushed in main-loop
	 *             hitl_server_process()), then read back simulated
	 *             encoder data produced by the Isaac Sim integrator.
	 */
	if (ctx->output_mode == VALVE_OUTPUT_MODE_HITL) {
		/* Queue torque for Isaac Sim – ISR-safe volatile write */
		hitl_server_set_torque(drive_torque_nm, state->diag.sample_seq,
		    state->diag.t_us_accum);

		/* Read back simulated encoder from Isaac Sim.
		 * If data is stale or missing, fault the same way as a CAN timeout. */
		struct hitl_encoder_data enc;
		if (!hitl_server_get_encoder(&enc)) {
			valve_handle_can_failure(ctx, STATUS_ERROR_TIMEOUT);
			return;
		}
		/* Inject simulated encoder into control-loop state */
		state->position_deg = enc.pos_deg;
		state->omega_rad_s  = enc.vel_rad_s;
		state->omega_raw_rad_s = enc.vel_rad_s;
	} else {
		/* Normal ODrive path */
		status_t torque_status = can_simple_set_input_torque_nb(state->odrive, drive_torque_nm);
		if (torque_status != STATUS_OK) {
			valve_handle_can_failure(ctx, torque_status);
			return;
		}
	}
	state->diag.last_can_status = STATUS_OK;

	valve_update_diagnostics(state, drive_torque_nm, t_start,
	    (uint16_t)VALVE_CONTROL_LOOP_PERIOD_US);
	
	/* Process profiler sampling */
}

/* Timer ISR callback to execute valve control loop at fixed intervals for real-time operation */
void valve_haptic_timer_isr(void)
{
	struct valve_context *ctx = active_valve_context;

	if (ctx == NULL) {
		return;
	}

	if (ctx->state.status != VALVE_STATE_RUNNING) {
		return;
	}

	/* Execute control loop directly in ISR for autonomous operation */
	valve_haptic_process(ctx);
}

/*
 * TIM6_DAC_IRQHandler - TIM6 interrupt handler
 * Called at VALVE_CONTROL_LOOP_HZ when TIM6 update event occurs
 * Executes valve control loop directly for autonomous, deterministic operation
 */

/* Hardware interrupt handler for TIM6 to trigger control loop execution at precise intervals */
void TIM6_DAC_IRQHandler(void)
{
    /* Check if update interrupt flag is set */
    if (htim6->SR & TIM_SR_UIF) {
        /* Clear update interrupt flag precisely */
        htim6->SR &= ~TIM_SR_UIF;
        
        /* Execute control loop autonomously */
        valve_haptic_timer_isr();
    }
}

/* Get pointer to current valve state for external monitoring and diagnostics */
struct valve_state *
valve_haptic_get_state(struct valve_context *ctx)
{
	return &ctx->state;
}

/* Get pointer to current valve configuration for inspection and modification */
struct valve_config *
valve_haptic_get_config(struct valve_context *ctx)
{
	return &ctx->config;
}

/*
 * Returns loop execution timing for performance monitoring.
 * Used to detect overruns that could cause control instability -
 * if avg_us exceeds 1000, the loop cannot keep up with its 1kHz rate.
 */
status_t
valve_haptic_get_loop_timing(struct valve_context *ctx, uint32_t *min_us,
    uint32_t *avg_us, uint32_t *max_us)
{
	struct valve_state *state;

	if (ctx == NULL || min_us == NULL || avg_us == NULL || max_us == NULL)
		return STATUS_ERROR_INVALID_PARAM;

	state = &ctx->state;
	if (state->diag.timing_sample_count == 0U)
		return STATUS_ERROR_NOT_INITIALIZED;

	*min_us = state->diag.loop_time_min_us;
	*max_us = state->diag.loop_time_max_us;
	*avg_us = state->diag.loop_time_sum_us / state->diag.timing_sample_count;

	return STATUS_OK;
}

/* Get pointer to the active valve context for global access in ISRs and callbacks */
struct valve_context *
valve_haptic_get_context(void)
{
	return active_valve_context;
}

/*
 * Estimates how long the valve takes to stop moving after release.
 * Used by tuning tools to characterize damping behavior and detect
 * oscillatory instability before it becomes dangerous.
 */
float
valve_haptic_calc_settling_time_ms(void)
{
	return 0.0f;
}

status_t
valve_haptic_set_vel_source(uint8_t source)
{
	if (source > VALVE_VEL_SOURCE_LPF_DELTA) {
		return STATUS_ERROR_INVALID_PARAM;
	}
	velocity_source = source;
	return STATUS_OK;
}

uint8_t
valve_haptic_get_vel_source(void)
{
	return velocity_source;
}

status_t
valve_haptic_set_vel_lpf_hz(float hz)
{
	if (hz < VALVE_VELOCITY_LPF_CUTOFF_MIN_HZ ||
	    hz > VALVE_VELOCITY_LPF_CUTOFF_MAX_HZ) {
		return STATUS_ERROR_INVALID_PARAM;
	}
	velocity_lpf_hz = hz;
	return STATUS_OK;
}

float
valve_haptic_get_vel_lpf_hz(void)
{
	return velocity_lpf_hz;
}

void
valve_haptic_set_quiet_enable(uint8_t enable)
{
	quiet_gate_enable = (enable != 0U) ? 1U : 0U;
	if (quiet_gate_enable == 0U && active_valve_context != NULL) {
		active_valve_context->state.quiet_active = 0U;
	}
}

uint8_t
valve_haptic_get_quiet_enable(void)
{
	return quiet_gate_enable;
}

void
valve_haptic_set_quiet_enter(float rad_s)
{
	if (rad_s < 0.0f) {
		rad_s = 0.0f;
	}
	quiet_enter_rad_s = rad_s;
	if (quiet_exit_rad_s < quiet_enter_rad_s) {
		quiet_exit_rad_s = quiet_enter_rad_s;
	}
}

float
valve_haptic_get_quiet_enter(void)
{
	return quiet_enter_rad_s;
}

void
valve_haptic_set_quiet_exit(float rad_s)
{
	if (rad_s < 0.0f) {
		rad_s = 0.0f;
	}
	quiet_exit_rad_s = rad_s;
	if (quiet_exit_rad_s < quiet_enter_rad_s) {
		quiet_enter_rad_s = quiet_exit_rad_s;
	}
}

float
valve_haptic_get_quiet_exit(void)
{
	return quiet_exit_rad_s;
}

/*
 * valve_haptic_set_output_mode - Switch torque routing at runtime.
 *
 * VALVE_OUTPUT_MODE_ODRIVE -> VALVE_OUTPUT_MODE_HITL:
 *   Disarms the physical ODrive (AXIS_STATE_IDLE) so it is safe to touch
 *   the hardware while Isaac Sim is in control.  Passivity tank is disabled.
 *
 * VALVE_OUTPUT_MODE_HITL -> VALVE_OUTPUT_MODE_ODRIVE:
 *   Re-arms the ODrive (AXIS_STATE_CLOSED_LOOP_CONTROL).  Passivity tank
 *   resumes.  The encoder zero reference is re-seeded from the last
 *   known HITL position so torque is continuous across the switch.
 *
 *   NOTE: Switching while running is intentional; the valve stays in
 *   VALVE_STATE_RUNNING throughout.
 */
status_t
valve_haptic_set_output_mode(struct valve_context *ctx, uint8_t mode)
{
	if (ctx == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}
	if (mode != VALVE_OUTPUT_MODE_ODRIVE && mode != VALVE_OUTPUT_MODE_HITL) {
		return STATUS_ERROR_INVALID_PARAM;
	}
	if (ctx->output_mode == mode) {
		return STATUS_OK; /* Already in requested mode */
	}

	if (mode == VALVE_OUTPUT_MODE_HITL) {
		/*
		 * Transition to HITL:
		 * Disarm the ODrive for safety (someone may touch the physical
		 * hardware while the simulation is running).
		 * TODO (future): keep ODrive armed to allow hot-switch back if
		 * real-time comparison is desired – change AXIS_STATE_IDLE to
		 * AXIS_STATE_CLOSED_LOOP_CONTROL and command 0 N·m continuously.
		 */
		if (ctx->state.odrive != NULL) {
			(void)can_simple_set_input_torque_nb(ctx->state.odrive, 0.0f);
			(void)can_simple_set_axis_state_nb(ctx->state.odrive, AXIS_STATE_IDLE);
		}
		/* Reset passivity tank – it will be disabled in the control loop */
		ctx->state.passivity_energy_j = 0.0f;
	} else {
		/* Transition back to ODRIVE: re-arm for closed-loop torque control */
		if (ctx->state.odrive != NULL) {
			(void)can_simple_set_controller_mode(ctx->state.odrive,
			    CONTROL_MODE_TORQUE_CONTROL, INPUT_MODE_PASSTHROUGH);
			(void)can_simple_set_axis_state(ctx->state.odrive,
			    AXIS_STATE_CLOSED_LOOP_CONTROL);
		}
		/* Re-seed passivity tank at zero so first ticks are passive */
		ctx->state.passivity_energy_j = 0.0f;
	}

	ctx->output_mode = mode;
	return STATUS_OK;
}

uint8_t
valve_haptic_get_output_mode(const struct valve_context *ctx)
{
	if (ctx == NULL) {
		return VALVE_OUTPUT_MODE_ODRIVE;
	}
	return ctx->output_mode;
}
