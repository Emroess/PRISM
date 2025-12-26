/*
 * valve_filters.h
 *
 * Advanced filtering algorithms for haptic valve control system.
 * Provides modular, reusable filter implementations using CMSIS-DSP.
 * 
 * Unused at this time - kept for possible future use.
 */

#ifndef VALVE_FILTERS_H
#define VALVE_FILTERS_H

#include <stdbool.h>
#include <stdint.h>

#include "arm_math.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Mathematical constants */
#define VALVE_PI 3.14159265358979323846f

/* Filter type enumeration */
typedef enum {
    VALVE_FILTER_NONE = 0,
    VALVE_FILTER_FIR_LOWPASS,
    VALVE_FILTER_BIQUAD_LOWPASS,
    VALVE_FILTER_BIQUAD_BANDPASS,
    VALVE_FILTER_BIQUAD_NOTCH,
    VALVE_FILTER_LMS_ADAPTIVE,
    VALVE_FILTER_KALMAN_POSITION,
    VALVE_FILTER_MAX_TYPES
} valve_filter_type_t;

/* Common filter configuration */
typedef struct {
    valve_filter_type_t type;
    float sample_rate_hz;
    float cutoff_freq_hz;
    float q_factor;           /* Quality factor for biquad filters */
    float bandwidth_hz;       /* Bandwidth for notch/bandpass */
    float step_size;          /* LMS step size */
    uint32_t num_taps;        /* FIR filter length */
    bool enabled;
} valve_filter_config_t;

/* Maximum filter sizes for static allocation */
#define VALVE_FILTER_MAX_TAPS 32
#define VALVE_FILTER_MAX_STATES 128

/* Filter instance structure (statically allocated) */
struct valve_filter_instance {
    valve_filter_config_t config;
    /* Pre-allocated state buffers */
    float state_buffer[VALVE_FILTER_MAX_STATES];
    float coeff_buffer[VALVE_FILTER_MAX_TAPS];
    union {
        arm_fir_instance_f32 fir;
        arm_biquad_casd_df1_inst_f32 biquad;
        arm_lms_instance_f32 lms;
        /* Kalman filter state would go here */
    } filter;
};

/* Filter instance handle */
typedef struct valve_filter_instance valve_filter_instance_t;

/* Filter status */
typedef enum {
    VALVE_FILTER_SUCCESS = 0,
    VALVE_FILTER_ERROR_INVALID_CONFIG,
    VALVE_FILTER_ERROR_MEMORY,
    VALVE_FILTER_ERROR_UNSUPPORTED_TYPE,
    VALVE_FILTER_ERROR_INVALID_STATE
} valve_filter_status_t;

/*
 * Setup/configure a filter instance with given parameters
 *
 * @param instance: Pre-allocated filter instance structure
 * @param config: Filter configuration
 * @return: Status code
 */
valve_filter_status_t valve_filter_setup(valve_filter_instance_t *instance, const valve_filter_config_t *config);

/*
 * Process a single sample through the filter
 *
 * @param instance: Filter instance handle
 * @param input: Input sample
 * @param output: Output sample (can be same as input for in-place)
 * @return: Status code
 */
valve_filter_status_t valve_filter_process(valve_filter_instance_t *instance,
                                         float input, float *output);

/*
 * Process multiple samples through the filter
 *
 * @param instance: Filter instance handle
 * @param input: Input buffer
 * @param output: Output buffer (can be same as input for in-place)
 * @param num_samples: Number of samples to process
 * @return: Status code
 */
valve_filter_status_t valve_filter_process_block(valve_filter_instance_t *instance,
                                               const float *input, float *output,
                                               uint32_t num_samples);

/*
 * Reset filter state to initial conditions
 *
 * @param instance: Filter instance handle
 * @return: Status code
 */
valve_filter_status_t valve_filter_reset(valve_filter_instance_t *instance);

/*
 * Pre-defined filter configurations for common use cases
 */

/* High-quality velocity estimation filter */
extern const valve_filter_config_t VALVE_FILTER_VELOCITY_ESTIMATION;

/* Smooth compliance response filter */
extern const valve_filter_config_t VALVE_FILTER_COMPLIANCE_SMOOTHING;

/* Torque ripple rejection filter */
extern const valve_filter_config_t VALVE_FILTER_TORQUE_RIPPLE_REJECTION;

/* Position sensor noise filter */
extern const valve_filter_config_t VALVE_FILTER_POSITION_NOISE_REDUCTION;

/*
 * Utility functions for filter design
 */

/*
 * Design FIR low-pass filter coefficients using window method
 *
 * @param coeffs: Output coefficient array
 * @param num_taps: Number of taps (must be odd for symmetry)
 * @param cutoff_hz: Cutoff frequency in Hz
 * @param sample_rate_hz: Sample rate in Hz
 * @return: Status code
 */
valve_filter_status_t valve_filter_design_fir_lowpass(float *coeffs,
                                                    uint32_t num_taps,
                                                    float cutoff_hz,
                                                    float sample_rate_hz);

/*
 * Design biquad filter coefficients
 *
 * @param coeffs: Output coefficient array [b0, b1, b2, a1, a2]
 * @param type: Filter type (lowpass, bandpass, notch)
 * @param cutoff_hz: Cutoff frequency in Hz
 * @param q_factor: Quality factor
 * @param sample_rate_hz: Sample rate in Hz
 * @return: Status code
 */
valve_filter_status_t valve_filter_design_biquad(float *coeffs,
                                               valve_filter_type_t type,
                                               float cutoff_hz,
                                               float q_factor,
                                               float sample_rate_hz);

/*
 * Simple single-pole IIR low-pass filter for real-time control loops
 *
 * Lightweight filter optimized for 1kHz control loops without CMSIS-DSP overhead.
 * Ideal for smoothing torque commands, sensor readings, or control signals.
 *
 * @param input: Current input value
 * @param previous_output: Previous filter output (state)
 * @param cutoff_freq_hz: Filter cutoff frequency in Hz (typically 50-200 Hz for haptics)
 * @param sample_rate_hz: Sample rate in Hz (e.g., 1000 Hz for 1ms loop)
 * @return: Filtered output value
 */
float valve_filter_lowpass_simple(float, float, float, float);

#ifdef __cplusplus
}
#endif

#endif /* VALVE_FILTERS_H */
