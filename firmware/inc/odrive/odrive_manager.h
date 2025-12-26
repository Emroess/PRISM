/*
 * odrive_manager.h - ODrive service layer
 *
 * Provides unified API for ODrive operations used by both CLI and HTTP API.
 * Single source of truth for all ODrive business logic.
 */

#ifndef ODRIVE_MANAGER_H
#define ODRIVE_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#include "protocols/can_simple.h"
#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * ODrive connection and basic operations
 */
status_t odrive_init(struct can_simple_handle *odrive);
status_t odrive_ping(struct can_simple_handle *odrive);

/*
 * ODrive control operations
 */
status_t odrive_enable(struct can_simple_handle *odrive);
status_t odrive_disable(struct can_simple_handle *odrive);
status_t odrive_estop(struct can_simple_handle *odrive);
status_t odrive_clear_errors(struct can_simple_handle *odrive);
status_t odrive_calibrate(struct can_simple_handle *odrive);

/*
 * ODrive configuration
 */
status_t odrive_set_limits(struct can_simple_handle *odrive, float vel_limit, float current_limit);
status_t odrive_set_controller_mode(struct can_simple_handle *odrive, uint8_t mode);
status_t odrive_set_position_gain(struct can_simple_handle *odrive, float gain);
status_t odrive_set_velocity_gains(struct can_simple_handle *odrive, float kp, float ki);

/*
 * ODrive commands
 */
status_t odrive_set_torque(struct can_simple_handle *odrive, float torque_nm);
status_t odrive_set_velocity(struct can_simple_handle *odrive, float velocity_turns_s);
status_t odrive_set_position(struct can_simple_handle *odrive, float position_turns);

/*
 * ODrive status queries - returns status_t, populates output parameters
 */
struct odrive_status {
	uint32_t axis_error;
	uint8_t axis_state;
	uint8_t motor_flags;
	uint8_t encoder_flags;
	uint8_t controller_status;
};

struct odrive_encoder {
	float pos_estimate;
	float vel_estimate;
};

struct odrive_telemetry {
	float bus_voltage;
	float bus_current;
	float fet_temp;
	float motor_temp;
};

status_t odrive_get_status(struct can_simple_handle *odrive, struct odrive_status *status);
status_t odrive_get_encoder(struct can_simple_handle *odrive, struct odrive_encoder *encoder);
status_t odrive_get_telemetry(struct can_simple_handle *odrive, struct odrive_telemetry *telemetry);

/*
 * CAN debug/diagnostics
 */
struct can_bus_status {
	uint32_t tx_count;
	uint32_t rx_count;
	uint32_t error_count;
	uint32_t last_error_code;
};

status_t can_get_bus_status(struct can_simple_handle *handle, struct can_bus_status *status);
status_t can_send_raw_frame(struct can_simple_handle *, uint32_t,
    uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t, uint8_t);

#ifdef __cplusplus
}
#endif

#endif /* ODRIVE_MANAGER_H */
