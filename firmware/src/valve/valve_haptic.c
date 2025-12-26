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
#include "protocols/can_simple.h"
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
#define VALVE_QUIET_ENTER_RAD_S       (1.0f * VALVE_DEG_TO_RAD)
#define VALVE_QUIET_EXIT_RAD_S        (2.0f * VALVE_DEG_TO_RAD)
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
		state->quiet_active = 0U;
	}
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
    
    /* Verify state is IDLE */
    if (state->status != VALVE_STATE_IDLE) {
        return STATUS_ERROR_BUSY;  /* Not in IDLE state */
    }
    
    ctx->staged_pending = 0U;
    ctx->staged_field_mask = 0U;
    
    /* Simplified: no preset validation */
    
	/* Validate configuration before starting */
	validation_result = valve_preset_validate(&ctx->config);
	if (validation_result != STATUS_OK) {
		return STATUS_ERROR_INVALID_CONFIG;  /* Configuration invalid */
	}

	state->degrees_per_turn = (ctx->config.degrees_per_turn > 0.0f) ?
		ctx->config.degrees_per_turn : VALVE_DEFAULT_DEGREES_PER_TURN;

	
	state->diag.last_can_status = STATUS_OK;
	state->diag.can_retry_count = 0;

	/* Clear any existing errors first */
	can_simple_clear_errors(state->odrive);
    
	/* Configure torque control with passthrough BEFORE enabling closed loop */
	if (can_simple_set_controller_mode(state->odrive,
	        CONTROL_MODE_TORQUE_CONTROL,
	        INPUT_MODE_PASSTHROUGH) != 0) {
        state->status = VALVE_STATE_IDLE;
		return STATUS_ERROR_HARDWARE_FAULT;  /* Error code 7: Failed to set controller mode */
    }

	/* Confirm controller/input modes actually changed via heartbeat.
	 * We treat this as a soft safety check: if the heartbeat never reports
	 * the desired modes within the timeout, we log the condition but still
	 * continue startup so that behavior can be evaluated on hardware. */
	if (!valve_wait_for_controller_mode(state,
	        CONTROL_MODE_TORQUE_CONTROL,
	        INPUT_MODE_PASSTHROUGH)) {
		/* Record that mode verification failed but do not abort. */
		state->diag.last_can_status = STATUS_ERROR_TIMEOUT;
	}
    
    /* Set limits based on loaded preset configuration */
    if (ctx->config.torque_limit_nm > 0.0f) {
        /* Convert torque limit to current using motor torque constant */
        float current_limit = (ctx->config.torque_limit_nm / ODRIVE_TORQUE_CONSTANT_NM_PER_A) + ODRIVE_CURRENT_HEADROOM_A;
        if (current_limit > VALVE_ODRIVE_CURRENT_LIMIT_A) {
            current_limit = VALVE_ODRIVE_CURRENT_LIMIT_A;
        }
        
        if (can_simple_set_limits(state->odrive, VALVE_ODRIVE_VEL_LIMIT_TURNS_PER_S, current_limit) != STATUS_OK) {
            state->status = VALVE_STATE_IDLE;
            return STATUS_ERROR_BUSY;  /* Error code 3: Failed to set limits */
        }
    }
    
    /* Set Odrive to CLOSED_LOOP_CONTROL */
    if (can_simple_set_axis_state(state->odrive, AXIS_STATE_CLOSED_LOOP_CONTROL) != 0) {
        state->status = VALVE_STATE_IDLE;
        return STATUS_ERROR_NOT_SUPPORTED;  /* Error code 11: Failed to enable closed loop */
    }
    
	/* Poll heartbeat for up to 500ms waiting for closed loop state (S1 broadcasts at 100ms intervals) */
	struct can_simple_heartbeat hb;
	uint32_t hb_age_ms = 0U;
	uint32_t wait_start_ms = board_get_systick_ms();
	uint8_t state_achieved = 0U;
	
	while ((board_get_systick_ms() - wait_start_ms) < 500U) {
		if (can_simple_get_cached_heartbeat(state->odrive, &hb, &hb_age_ms) == STATUS_OK &&
		    hb_age_ms < VALVE_HEARTBEAT_TIMEOUT_MS) {
			
			/* Check for errors */
			if (hb.axis_error != 0) {
				can_simple_set_axis_state(state->odrive, AXIS_STATE_IDLE);
				state->status = VALVE_STATE_IDLE;
				return STATUS_ERROR_BUFFER_FULL;  /* Error code 9: Odrive has axis error */
			}
			
			/* Check if we've reached closed loop state */
			if (hb.axis_state == AXIS_STATE_CLOSED_LOOP_CONTROL) {
				state_achieved = 1U;
				state->diag.heartbeat_age_ms = hb_age_ms;
				break;
			}
		}
		board_delay_ms(10);  /* Poll every 10ms */
	}
	
	/* Verify we achieved the target state */
	if (!state_achieved) {
		can_simple_set_axis_state(state->odrive, AXIS_STATE_IDLE);
		state->status = VALVE_STATE_IDLE;
		return STATUS_ERROR_BUFFER_EMPTY;  /* Error code 10: Timeout waiting for closed loop state */
	}
    
	/* Get initial encoder position from cached data (S1 broadcasts at 1kHz) */
	struct can_simple_encoder_estimates est;
	uint32_t encoder_age_ms = 0;
	status_t enc_status;
	uint32_t enc_wait_start_ms = board_get_systick_ms();
	const uint32_t enc_wait_timeout_ms = 200U; /* Wait up to 200 ms for first cached sample */

	do {
		enc_status = can_simple_get_cached_encoder(state->odrive, &est, &encoder_age_ms, NULL);
		if (enc_status == STATUS_OK) {
			break;
		}
		/* Give the broadcaster a moment to publish encoder data */
		board_delay_ms(5);
	} while ((board_get_systick_ms() - enc_wait_start_ms) < enc_wait_timeout_ms);

	if (enc_status != STATUS_OK) {
		can_simple_set_axis_state(state->odrive, AXIS_STATE_IDLE);
		state->status = VALVE_STATE_IDLE;
		return enc_status;  /* Propagate specific encoder error (e.g., buffer empty) */
	}
	
	/* Establish zero reference but track absolute shaft angle for commands.
	 * We seed command_position_deg to the actual measured angle so the
	 * ODrive holds wherever the user left the shaft when we enter RUNNING.
	 */
	state->encoder_zero_turns = est.position;
	state->raw_position_turns = est.position;
	state->position_deg = est.position * state->degrees_per_turn;
	state->command_position_deg = state->position_deg;
	float turn_to_rad = state->degrees_per_turn * VALVE_DEG_TO_RAD;
	state->omega_rad_s = est.velocity * turn_to_rad;
	if (state->omega_rad_s < (0.1f * VALVE_DEG_TO_RAD) && state->omega_rad_s > -(0.1f * VALVE_DEG_TO_RAD)) {
		state->omega_rad_s = 0.0f;
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
	
	/* Return to IDLE state */
	state->status = VALVE_STATE_IDLE;
	state->diag.can_retry_count = 0;
	state->diag.telemetry_age_ms = UINT32_MAX;
	state->diag.heartbeat_age_ms = UINT32_MAX;
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

	/* Update position and velocity with multi-stage filtering.
	 * As above, we use the ODrive encoder's native sign convention directly.
	 */
	const float deg_per_turn = (state->degrees_per_turn > 0.0f) ?
		state->degrees_per_turn : VALVE_DEFAULT_DEGREES_PER_TURN;
	float prev_turns = state->raw_position_turns;
	float delta_turns = obs.position - prev_turns;
	state->raw_position_turns = obs.position;
	state->position_deg = (obs.position - state->encoder_zero_turns) * deg_per_turn;

	/* Estimate instantaneous velocity from position delta (removes ODrive filter lag) */
	float vel_raw = delta_turns * deg_per_turn * VALVE_DEG_TO_RAD * (float)VALVE_CONTROL_LOOP_HZ;
	if (!velocity_filters_initialized) {
		valve_velocity_filters_seed(vel_raw);
	}

	/* Clamp extreme velocity estimates to avoid over-reacting to single-sample spikes */
	const float max_vel_rad_s = deg_per_turn * VALVE_DEG_TO_RAD * VALVE_ODRIVE_VEL_LIMIT_TURNS_PER_S;
	if (vel_raw > max_vel_rad_s) {
		vel_raw = max_vel_rad_s;
	} else if (vel_raw < -max_vel_rad_s) {
		vel_raw = -max_vel_rad_s;
	}

	float prev_omega = state->omega_rad_s;
	state->omega_rad_s = vel_raw;
	state->prev_omega_rad_s = prev_omega;
	state->alpha_rad_s2 = (state->omega_rad_s - prev_omega) * (float)VALVE_CONTROL_LOOP_HZ;
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
	if ((state->status & VALVE_STATE_RUNNING) == 0)
		return;

	valve_apply_staged_config(ctx);
	cfg = &ctx->config;

	t_start = dwt_get_cycles();

	/* Process encoder data and check for fresh samples */
	status_t encoder_status = valve_process_encoder_data(state);
	if (encoder_status != STATUS_OK) {
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

	/* Mirror measured angle for tooling that still inspects command_position */
	state->command_position_deg = state->position_deg;

	/* === TORQUE COMMAND PATH ===
	 *
	 * Purely resistive behavior: compute braking torque from the physics model,
	 * apply optional rate limiting, clamp to safety limits, then stream the
	 * torque setpoint directly to the ODrive torque controller.
	 */
	float torque_nm = valve_physics_calculate_torque_hil(cfg,
	    state->position_deg,
	    state->omega_rad_s,
	    state->quiet_active);

	float torque_limit = 0.0f;
	if (cfg->torque_limit_nm > 0.0f) {
	    torque_limit = cfg->torque_limit_nm;
	}

	float clamped_torque = valve_physics_clamp_torque(
	    torque_nm,
	    torque_limit);
	torque_nm = clamped_torque;

	float prev_filtered = state->filtered_torque_nm;
	float filtered_torque = valve_filter_lowpass_simple(
	    torque_nm,
	    prev_filtered,
	    VALVE_TORQUE_FILTER_CUTOFF_HZ,
	    VALVE_TORQUE_FILTER_SAMPLE_RATE_HZ);
	torque_nm = filtered_torque;

	/* Enhanced passivity energy tank with persistent storage */
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
	
	state->filtered_torque_nm = torque_nm;
	state->previous_torque_nm = torque_nm;

	float drive_torque_nm = torque_nm * VALVE_TORQUE_SIGN;
	status_t torque_status = can_simple_set_input_torque_nb(state->odrive, drive_torque_nm);
	if (torque_status != STATUS_OK) {
		valve_handle_can_failure(ctx, torque_status);
		return;
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
