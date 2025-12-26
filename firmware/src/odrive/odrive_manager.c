/*
 * odrive_manager.c - ODrive service layer implementation
 *
 * Wraps can_simple operations with proper error handling and business logic.
 */

#include <string.h>

#include "board.h"
#include "odrive_manager.h"

status_t
odrive_init(struct can_simple_handle *odrive)
{
	/* Ping to verify connectivity */
	return odrive_ping(odrive);
}

status_t
odrive_ping(struct can_simple_handle *odrive)
{
	struct can_simple_heartbeat hb;
	return can_simple_get_heartbeat(odrive, &hb);
}

status_t
odrive_enable(struct can_simple_handle *odrive)
{
	return can_simple_set_axis_state(odrive, AXIS_STATE_CLOSED_LOOP_CONTROL);
}

status_t
odrive_disable(struct can_simple_handle *odrive)
{
	return can_simple_set_axis_state(odrive, AXIS_STATE_IDLE);
}

status_t
odrive_estop(struct can_simple_handle *odrive)
{
	return can_simple_estop(odrive);
}

status_t
odrive_clear_errors(struct can_simple_handle *odrive)
{
	return can_simple_clear_errors(odrive);
}

status_t
odrive_calibrate(struct can_simple_handle *odrive)
{
	status_t status;
	struct can_simple_heartbeat hb;
	uint32_t timeout_start;
	const uint32_t timeout_ms = 30000;

	/* Request calibration */
	status = can_simple_set_axis_state(odrive, AXIS_STATE_FULL_CALIBRATION);
	if (status != STATUS_OK)
		return status;

	/* Wait for calibration to complete (typically 10-20 seconds) */
	timeout_start = board_get_systick_ms();

	while ((board_get_systick_ms() - timeout_start) < timeout_ms) {
		board_delay_ms(500);
		status = can_simple_get_heartbeat(odrive, &hb);
		if (status != STATUS_OK)
			return status;
		if (hb.axis_state == AXIS_STATE_IDLE) {
			/* Calibration complete, check for errors */
			if (hb.axis_error != 0)
				return STATUS_ERROR_HARDWARE_FAULT;
			return STATUS_OK;
		}
	}

	return STATUS_ERROR_TIMEOUT;
}

status_t
odrive_set_limits(struct can_simple_handle *odrive, float vel_limit, float current_limit)
{
	return can_simple_set_limits(odrive, vel_limit, current_limit);
}

status_t
odrive_set_controller_mode(struct can_simple_handle *odrive, uint8_t mode)
{
	switch (mode) {
	case 0: /* voltage */
		return can_simple_set_controller_mode(odrive, CONTROL_MODE_VOLTAGE_CONTROL, INPUT_MODE_PASSTHROUGH);
	case 1: /* torque */
		return can_simple_set_controller_mode(odrive, CONTROL_MODE_TORQUE_CONTROL, INPUT_MODE_PASSTHROUGH);
	case 2: /* velocity */
		return can_simple_set_controller_mode(odrive, CONTROL_MODE_VELOCITY_CONTROL, INPUT_MODE_PASSTHROUGH);
	case 3: /* position */
		return can_simple_set_controller_mode(odrive, CONTROL_MODE_POSITION_CONTROL, INPUT_MODE_PASSTHROUGH);
	default:
		return STATUS_ERROR_INVALID_PARAM;
	}
}

status_t
odrive_set_position_gain(struct can_simple_handle *odrive, float gain)
{
	return can_simple_set_pos_gain(odrive, gain);
}

status_t
odrive_set_velocity_gains(struct can_simple_handle *odrive, float kp, float ki)
{
	return can_simple_set_vel_gains(odrive, kp, ki);
}

status_t
odrive_set_torque(struct can_simple_handle *odrive, float torque_nm)
{
	return can_simple_set_input_torque(odrive, torque_nm);
}

status_t
odrive_set_velocity(struct can_simple_handle *odrive, float velocity_turns_s)
{
	return can_simple_set_velocity(odrive, velocity_turns_s, 0.0f);
}

status_t
odrive_set_position(struct can_simple_handle *odrive, float position_turns)
{
	return can_simple_set_position(odrive, position_turns, 0, 0);
}

status_t
odrive_get_status(struct can_simple_handle *odrive, struct odrive_status *status)
{
	struct can_simple_heartbeat hb;
	status_t result = can_simple_get_heartbeat(odrive, &hb);
	if (result != STATUS_OK) {
		return result;
	}
	
	status->axis_error = hb.axis_error;
	status->axis_state = hb.axis_state;
	status->motor_flags = 0;
	status->encoder_flags = 0;
	status->controller_status = hb.controller_status;
	
	return STATUS_OK;
}

status_t
odrive_get_encoder(struct can_simple_handle *odrive, struct odrive_encoder *encoder)
{
	struct can_simple_encoder_estimates est;
	uint32_t age_ms, seq;
	status_t result = can_simple_get_cached_encoder(odrive, &est, &age_ms, &seq);
	if (result != STATUS_OK) {
		return result;
	}
	
	encoder->pos_estimate = est.position;
	encoder->vel_estimate = est.velocity;
	return STATUS_OK;
}

status_t
odrive_get_telemetry(struct can_simple_handle *odrive, struct odrive_telemetry *telemetry)
{
	status_t result;
	uint32_t age_ms;
	
	result = can_simple_get_cached_bus_voltage_current(odrive, &telemetry->bus_voltage, 
	                                                    &telemetry->bus_current, &age_ms);
	if (result != STATUS_OK) {
		return result;
	}
	
	result = can_simple_get_cached_temperature(odrive, &telemetry->fet_temp, 
	                                            &telemetry->motor_temp, &age_ms);
	return result;
}

status_t
can_get_bus_status(struct can_simple_handle *handle, struct can_bus_status *status)
{
	(void)handle;
	/* For now, return placeholder values - would need CAN driver extension */
	memset(status, 0, sizeof(*status));
	return STATUS_OK;
}

status_t
can_send_raw_frame(struct can_simple_handle *handle, uint32_t cmd_id,
                   uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3,
                   uint8_t b4, uint8_t b5, uint8_t b6, uint8_t b7)
{
	(void)handle;
	(void)cmd_id;
	(void)b0; (void)b1; (void)b2; (void)b3;
	(void)b4; (void)b5; (void)b6; (void)b7;
	/* Raw frame sending would need can_simple extension */
	return STATUS_ERROR_NOT_SUPPORTED;
}
