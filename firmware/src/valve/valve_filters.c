/*
 * valve_filters.c
 *
 * Advanced filtering algorithms implementation for haptic valve control system.
 * Provides modular, reusable filter implementations using CMSIS-DSP.
 * Uses static allocation for deterministic embedded operation.
 * 
 * This file and these filters are not currently used in the main firmware,
 * but are provided for future expansion and experimentation.
 * valve_filters.h contains predefined configurations for common use cases.
 */

#include "valve_filters.h"
#include "arm_math.h"
#include <string.h>

/* Pre-defined filter configurations */
const valve_filter_config_t VALVE_FILTER_VELOCITY_ESTIMATION = {
    .type = VALVE_FILTER_FIR_LOWPASS,
    .sample_rate_hz = 1000.0f,
    .cutoff_freq_hz = 50.0f,
    .q_factor = 0.707f,
    .bandwidth_hz = 10.0f,
    .step_size = 0.01f,
    .num_taps = 16,
    .enabled = true
};

const valve_filter_config_t VALVE_FILTER_COMPLIANCE_SMOOTHING = {
    .type = VALVE_FILTER_BIQUAD_LOWPASS,
    .sample_rate_hz = 1000.0f,
    .cutoff_freq_hz = 30.0f,
    .q_factor = 0.707f,
    .bandwidth_hz = 5.0f,
    .step_size = 0.01f,
    .num_taps = 0,
    .enabled = true
};

const valve_filter_config_t VALVE_FILTER_TORQUE_RIPPLE_REJECTION = {
    .type = VALVE_FILTER_BIQUAD_NOTCH,
    .sample_rate_hz = 1000.0f,
    .cutoff_freq_hz = 100.0f,  /* 100Hz ripple frequency */
    .q_factor = 10.0f,         /* Narrow notch */
    .bandwidth_hz = 5.0f,
    .step_size = 0.01f,
    .num_taps = 0,
    .enabled = true
};

/*
 * Setup/configure a filter instance with given parameters
 */
valve_filter_status_t valve_filter_setup(valve_filter_instance_t *instance, const valve_filter_config_t *config)
{
    if (instance == NULL || config == NULL || config->type >= VALVE_FILTER_MAX_TYPES) {
        return VALVE_FILTER_ERROR_INVALID_CONFIG;
    }

    /* Copy configuration */
    instance->config = *config;

    /* Clear state and coefficient buffers */
    memset(instance->state_buffer, 0, sizeof(instance->state_buffer));
    memset(instance->coeff_buffer, 0, sizeof(instance->coeff_buffer));

    /* Initialize based on filter type */
    switch (config->type) {
        case VALVE_FILTER_FIR_LOWPASS: {
            /* Design FIR lowpass filter */
            uint32_t num_taps = config->num_taps;
            if (num_taps > VALVE_FILTER_MAX_TAPS) {
                return VALVE_FILTER_ERROR_INVALID_CONFIG;
            }

            /* Generate Hamming windowed sinc filter */
            float fc_norm = config->cutoff_freq_hz / config->sample_rate_hz;
            for (uint32_t n = 0; n < num_taps; n++) {
                float x = 2.0f * VALVE_PI * fc_norm * (float)(n - (num_taps - 1) / 2);
                float sinc = (x != 0.0f) ? arm_sin_f32(x) / x : 1.0f;
                float window = 0.54f - 0.46f * arm_cos_f32(2.0f * VALVE_PI * (float)n / (float)(num_taps - 1));
                instance->coeff_buffer[n] = sinc * window;
            }

            /* Normalize for unity gain */
            float sum = 0.0f;
            for (uint32_t i = 0; i < num_taps; i++) {
                sum += instance->coeff_buffer[i];
            }
            for (uint32_t i = 0; i < num_taps; i++) {
                instance->coeff_buffer[i] /= sum;
            }

            /* Initialize CMSIS FIR filter */
            arm_fir_init_f32(&instance->filter.fir, num_taps,
                            instance->coeff_buffer, instance->state_buffer,
                            1);
            break;
        }

        case VALVE_FILTER_BIQUAD_LOWPASS: {
            /* Design biquad lowpass filter */
            float omega = 2.0f * VALVE_PI * config->cutoff_freq_hz / config->sample_rate_hz;
            float alpha = arm_sin_f32(omega) / (2.0f * config->q_factor);
            float cos_omega = arm_cos_f32(omega);

            float b0 = (1.0f - cos_omega) / 2.0f;
            float b1 = 1.0f - cos_omega;
            float b2 = (1.0f - cos_omega) / 2.0f;
            float a0 = 1.0f + alpha;
            float a1 = -2.0f * cos_omega;
            float a2 = 1.0f - alpha;

            /* Normalize coefficients */
            instance->coeff_buffer[0] = b0 / a0;
            instance->coeff_buffer[1] = b1 / a0;
            instance->coeff_buffer[2] = b2 / a0;
            instance->coeff_buffer[3] = a1 / a0;
            instance->coeff_buffer[4] = a2 / a0;

            /* Initialize CMSIS biquad filter */
            arm_biquad_cascade_df1_init_f32(&instance->filter.biquad, 1,
                                          instance->coeff_buffer, instance->state_buffer);
            break;
        }

        default:
            return VALVE_FILTER_ERROR_UNSUPPORTED_TYPE;
    }

    return VALVE_FILTER_SUCCESS;
}

/*
 * Process a single sample through the filter
 */
valve_filter_status_t valve_filter_process(valve_filter_instance_t *instance,
                                         float input, float *output)
{
    
    if (instance == NULL || output == NULL || !instance->config.enabled) {
        return VALVE_FILTER_ERROR_INVALID_STATE;
    }

    switch (instance->config.type) {
        case VALVE_FILTER_FIR_LOWPASS:
            arm_fir_f32(&instance->filter.fir, &input, output, 1);
            break;

        case VALVE_FILTER_BIQUAD_LOWPASS:
            arm_biquad_cascade_df1_f32(&instance->filter.biquad, &input, output, 1);
            break;

        default:
            return VALVE_FILTER_ERROR_UNSUPPORTED_TYPE;
    }

    return VALVE_FILTER_SUCCESS;
}

/*
 * Process multiple samples through the filter
 */
valve_filter_status_t valve_filter_process_block(valve_filter_instance_t *instance,
                                               const float *input, float *output,
                                               uint32_t num_samples)
{
    if (instance == NULL || input == NULL || output == NULL || !instance->config.enabled) {
        return VALVE_FILTER_ERROR_INVALID_STATE;
    }

    switch (instance->config.type) {
        case VALVE_FILTER_FIR_LOWPASS:
            arm_fir_f32(&instance->filter.fir, input, output, num_samples);
            break;

        case VALVE_FILTER_BIQUAD_LOWPASS:
            arm_biquad_cascade_df1_f32(&instance->filter.biquad, input, output, num_samples);
            break;

        default:
            return VALVE_FILTER_ERROR_UNSUPPORTED_TYPE;
    }

    return VALVE_FILTER_SUCCESS;
}

/*
 * Reset filter state
 */
valve_filter_status_t valve_filter_reset(valve_filter_instance_t *instance)
{
    if (instance == NULL) {
        return VALVE_FILTER_ERROR_INVALID_STATE;
    }

    memset(instance->state_buffer, 0, sizeof(instance->state_buffer));
    return VALVE_FILTER_SUCCESS;
}
/*
 * valve_filter_lowpass_simple - Lightweight single-pole IIR low-pass filter
 *
 * Implements a simple first-order low-pass filter for real-time torque smoothing
 * without the overhead of CMSIS-DSP structures. This is optimized for use in the
 * 1000 Hz control loop where efficiency is critical.
 *
 * This is essential for best-in-class motor control as it:
 * - Removes high-frequency noise from torque calculations
 * - Prevents excitation of structural resonances in the motor/gearbox
 * - Reduces audible buzzing or humming from rapid torque changes
 * - Improves perceived smoothness of haptic feedback
 * - Acts as final safety barrier against control instabilities
 *
 * Uses exponential smoothing (single-pole IIR) for efficiency:
 *   y[n] = α * x[n] + (1 - α) * y[n-1]
 * where α = 1 - exp(-2π * fc * dt) ≈ 2π * fc * dt for small fc*dt
 *
 * Smooths torque commands to reduce audible motor whine from
 * high-frequency control corrections. Without this, the motor
 * emits an annoying buzz during precision positioning.
 */
float
valve_filter_lowpass_simple(float input, float previous_output,
    float cutoff_freq_hz, float sample_rate_hz)
{
	float nyquist;
	float dt;
	float alpha;
	float filtered;

	/* Disable filtering if cutoff frequency is zero or invalid */
	if (cutoff_freq_hz <= 0.0f || sample_rate_hz <= 0.0f)
		return input;

	/* Prevent cutoff frequency from exceeding Nyquist limit */
	nyquist = sample_rate_hz * 0.5f;
	if (cutoff_freq_hz > nyquist * 0.9f)
		cutoff_freq_hz = nyquist * 0.9f;

	/*
	 * Calculate smoothing coefficient (alpha)
	 * alpha = 2*pi * fc * dt for small fc*dt (valid approximation for fc << fs)
	 */
	dt = 1.0f / sample_rate_hz;
	alpha = 6.28318530718f * cutoff_freq_hz * dt;

	/* Clamp alpha to valid range [0, 1] */
	if (alpha > 1.0f)
		alpha = 1.0f;
	else if (alpha < 0.0f)
		alpha = 0.0f;

	/* Apply exponential smoothing */
	filtered = alpha * input + (1.0f - alpha) * previous_output;

	return filtered;
}
