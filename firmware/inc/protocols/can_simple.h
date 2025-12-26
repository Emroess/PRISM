/*
 * can_simple.h - Odrive CANSIMPLE Protocol Implementation
 *
 * Implements the Odrive CANSIMPLE protocol for motor control.
 * Protocol details: https://docs.odriverobotics.com/v/latest/can-protocol.html
 *
 * Message ID Format: (node_id << 5) | cmd_id
 * - node_id: 0-63 (6 bits)
 * - cmd_id: 0-31 (5 bits)
 */

#ifndef CAN_SIMPLE_H
#define CAN_SIMPLE_H

#include <stdbool.h>
#include <stdint.h>

#include "fdcan.h"
#include "status.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * CANSIMPLE Command IDs (5 bits)
 */
enum can_simple_cmd {
	/* Get/Set Commands */
	CAN_SIMPLE_CMD_HEARTBEAT           = 0x01,  /* Get: Axis error, state, control mode */
	CAN_SIMPLE_CMD_ESTOP               = 0x02,  /* Set: Emergency stop */
	CAN_SIMPLE_CMD_GET_MOTOR_ERROR     = 0x03,  /* Get: Motor error flags */
	CAN_SIMPLE_CMD_GET_ENCODER_ERROR   = 0x04,  /* Get: Encoder error flags */
	CAN_SIMPLE_CMD_GET_SENSORLESS_ERROR= 0x05,  /* Get: Sensorless error flags */
	CAN_SIMPLE_CMD_SET_AXIS_NODE_ID    = 0x06,  /* Set: Change node ID */
	CAN_SIMPLE_CMD_SET_AXIS_STATE      = 0x07,  /* Set: Requested axis state */
	CAN_SIMPLE_CMD_GET_ENCODER_ESTIMATES=0x09,  /* Get: Pos (float), vel (float) */
	CAN_SIMPLE_CMD_GET_ENCODER_COUNT   = 0x0A,  /* Get: Shadow count (int32), CPR (int32) */
	CAN_SIMPLE_CMD_SET_CONTROLLER_MODE = 0x0B,  /* Set: Control mode, input mode */
	CAN_SIMPLE_CMD_SET_INPUT_POS       = 0x0C,  /* Set: Position (float), vel FF (int16), torque FF (int16) */
	CAN_SIMPLE_CMD_SET_INPUT_VEL       = 0x0D,  /* Set: Velocity (float), torque FF (float) */
	CAN_SIMPLE_CMD_SET_INPUT_TORQUE    = 0x0E,  /* Set: Torque (float), vel FF (int16), pos FF (int16) */
	CAN_SIMPLE_CMD_SET_LIMITS          = 0x0F,  /* Set: Vel limit (float), current limit (float) */
	CAN_SIMPLE_CMD_START_ANTICOGGING   = 0x10,  /* Set: Start anticogging calibration */
	CAN_SIMPLE_CMD_SET_TRAJ_VEL_LIMIT  = 0x11,  /* Set: Trajectory vel limit (float) */
	CAN_SIMPLE_CMD_SET_TRAJ_ACCEL_LIMITS=0x12,  /* Set: Traj accel limit (float), decel limit (float) */
	CAN_SIMPLE_CMD_SET_TRAJ_INERTIA    = 0x13,  /* Set: Inertia (float) */
	CAN_SIMPLE_CMD_GET_IQ              = 0x14,  /* Get: Iq setpoint (float), Iq measured (float) */
	CAN_SIMPLE_CMD_GET_TEMPERATURE     = 0x15,  /* Get: FET temp (float), motor temp (float) */
	CAN_SIMPLE_CMD_REBOOT              = 0x16,  /* Set: Reboot controller */
	CAN_SIMPLE_CMD_GET_BUS_VOLTAGE_CURRENT=0x17,/* Get: Bus voltage (float), bus current (float) */
	CAN_SIMPLE_CMD_CLEAR_ERRORS        = 0x18,  /* Set: Clear all errors */
	CAN_SIMPLE_CMD_SET_LINEAR_COUNT    = 0x19,  /* Set: Position (int32) */
	CAN_SIMPLE_CMD_SET_POS_GAIN        = 0x1A,  /* Set: Position gain (float) */
	CAN_SIMPLE_CMD_SET_VEL_GAINS       = 0x1B,  /* Set: Vel gain (float), vel integrator gain (float) */
};

/*
 * Axis States (for SET_AXIS_STATE command)
 */
enum can_simple_axis_state {
	AXIS_STATE_UNDEFINED              = 0x00,
	AXIS_STATE_IDLE                   = 0x01,
	AXIS_STATE_STARTUP_SEQUENCE       = 0x02,
	AXIS_STATE_FULL_CALIBRATION       = 0x03,
	AXIS_STATE_MOTOR_CALIBRATION      = 0x04,
	AXIS_STATE_ENCODER_INDEX_SEARCH   = 0x06,
	AXIS_STATE_ENCODER_OFFSET_CALIBRATION = 0x07,
	AXIS_STATE_CLOSED_LOOP_CONTROL    = 0x08,
	AXIS_STATE_LOCKIN_SPIN            = 0x09,
	AXIS_STATE_ENCODER_DIR_FIND       = 0x0A,
	AXIS_STATE_HOMING                 = 0x0B,
	AXIS_STATE_ENCODER_HALL_POLARITY_CALIBRATION = 0x0C,
	AXIS_STATE_ENCODER_HALL_PHASE_CALIBRATION = 0x0D,
};

/*
 * Control Modes
 */
enum can_simple_control_mode {
	CONTROL_MODE_VOLTAGE_CONTROL      = 0x00,
	CONTROL_MODE_TORQUE_CONTROL       = 0x01,
	CONTROL_MODE_VELOCITY_CONTROL     = 0x02,
	CONTROL_MODE_POSITION_CONTROL     = 0x03,
};

/*
 * Input Modes
 */
enum can_simple_input_mode {
	INPUT_MODE_INACTIVE               = 0x00,
	INPUT_MODE_PASSTHROUGH            = 0x01,
	INPUT_MODE_VEL_RAMP               = 0x02,
	INPUT_MODE_POS_FILTER             = 0x03,
	INPUT_MODE_MIX_CHANNELS           = 0x04,
	INPUT_MODE_TRAP_TRAJ              = 0x05,
	INPUT_MODE_TORQUE_RAMP            = 0x06,
	INPUT_MODE_MIRROR                 = 0x07,
	INPUT_MODE_TUNING                 = 0x08,
};

/*
 * Heartbeat Response Structure
 */
struct can_simple_heartbeat {
	uint32_t axis_error;      /* Axis error flags */
	uint8_t axis_state;       /* Current axis state */
	uint8_t procedure_result; /* Startup procedure result */
	uint8_t trajectory_done;  /* Trajectory done flag */
	uint8_t controller_status; /* Controller status (reserved/not implemented in current ODrive firmware) */
};

/*
 * Encoder Estimates Structure
 */
struct can_simple_encoder_estimates {
	float position;  /* Position in turns */
	float velocity;  /* Velocity in turns/s */
};

/*
 * Encoder Count Structure
 */
struct can_simple_encoder_count {
	int32_t shadow_count;  /* Cumulative encoder position in counts */
	int32_t cpr;           /* Counts per revolution */
};

/*
 * Bus Voltage/Current Structure
 */
struct can_simple_bus_voltage_current {
	float bus_voltage;  /* Bus voltage in volts */
	float bus_current;  /* Bus current in amps */
};

/*
 * Temperature Structure
 */
struct can_simple_temperature {
	float fet_temp;    /* FET temperature in °C */
	float motor_temp;  /* Motor temperature in °C */
};

/*
 * Iq Current Structure
 */
struct can_simple_iq {
	float iq_setpoint;  /* Iq setpoint in amps */
	float iq_measured;  /* Iq measured in amps */
};

/*
 * Motor Error Structure
 */
struct can_simple_motor_error {
	uint32_t error_flags;  /* Motor error flags (bitfield) */
};

/*
 * Encoder Error Structure
 */
struct can_simple_encoder_error {
	uint32_t error_flags;  /* Encoder error flags (bitfield) */
};

/*
 * Sensorless Error Structure
 */
struct can_simple_sensorless_error {
	uint32_t error_flags;  /* Sensorless estimator error flags (bitfield) */
};

/*
 * CANSIMPLE Handle (opaque)
 */
struct can_simple_handle;

/*
 * CANSIMPLE Configuration
 */
struct can_simple_config {
	struct fdcan_handle *can;  /* FDCAN handle */
	uint8_t node_id;           /* Odrive node ID (0-63) */
	uint32_t timeout_ms;       /* Default timeout for commands */
};

/*
 * can_simple_init - Initialize CANSIMPLE protocol handler
 */
status_t can_simple_init(struct can_simple_handle **h, 
                         const struct can_simple_config *cfg);

/*
 * can_simple_deinit - Deinitialize CANSIMPLE handler
 */
status_t can_simple_deinit(struct can_simple_handle *h);

/*
 * can_simple_flush_rx - Flush RX buffer to discard stale messages
 */
void can_simple_flush_rx(struct can_simple_handle *h);

/*
 * can_simple_set_axis_state - Set requested axis state
 */
status_t can_simple_set_axis_state(struct can_simple_handle *h, 
                                   uint32_t requested_state);

/*
 * can_simple_set_controller_mode - Set control mode and input mode
 */
status_t can_simple_set_controller_mode(struct can_simple_handle *h,
                                        uint32_t control_mode,
                                        uint32_t input_mode);

/*
 * can_simple_set_velocity - Set velocity setpoint
 */
status_t can_simple_set_velocity(struct can_simple_handle *h, 
                                 float velocity, 
                                 float torque_ff);

/*
 * can_simple_set_position - Set position setpoint
 */
status_t can_simple_set_position(struct can_simple_handle *h,
                                 float position,
                                 int16_t velocity_ff,
                                 int16_t torque_ff);

status_t can_simple_set_position_nb(struct can_simple_handle *h,
									float position,
									int16_t velocity_ff,
									int16_t torque_ff);

/*
 * can_simple_set_input_torque - Set torque setpoint
 */
status_t can_simple_set_input_torque(struct can_simple_handle *h,
                                     float torque);

/*
 * can_simple_set_limits - Set velocity and current limits
 */
status_t can_simple_set_limits(struct can_simple_handle *h,
                               float vel_limit,
                               float current_limit);

/*
 * can_simple_set_traj_vel_limit - Set trajectory velocity limit
 */
status_t can_simple_set_traj_vel_limit(struct can_simple_handle *h,
                                       float traj_vel_limit);

/*
 * can_simple_set_traj_accel_limits - Set trajectory acceleration/deceleration limits
 */
status_t can_simple_set_traj_accel_limits(struct can_simple_handle *h,
                                          float accel_limit,
                                          float decel_limit);

/*
 * can_simple_set_pos_gain - Set position controller gain
 */
status_t can_simple_set_pos_gain(struct can_simple_handle *h,
                                 float pos_gain);

/*
 * can_simple_set_vel_gains - Set velocity controller gains
 */
status_t can_simple_set_vel_gains(struct can_simple_handle *h,
                                  float vel_gain,
                                  float vel_integrator_gain);

/*
 * can_simple_set_linear_count - Set position using raw encoder counts
 */
status_t can_simple_set_linear_count(struct can_simple_handle *h,
                                     int32_t position_counts);

/*
 * can_simple_get_heartbeat - Request heartbeat (axis state/errors)
 */
status_t can_simple_get_heartbeat(struct can_simple_handle *h,
                                  struct can_simple_heartbeat *heartbeat);

status_t can_simple_get_cached_heartbeat(struct can_simple_handle *h,
                                         struct can_simple_heartbeat *heartbeat,
                                         uint32_t *age_ms);

status_t can_simple_get_cached_encoder(struct can_simple_handle *h,
                                       struct can_simple_encoder_estimates *est,
                                       uint32_t *age_ms,
                                       uint32_t *seq);

void can_simple_reset_encoder_cache(struct can_simple_handle *h);

/*
 * can_simple_get_encoder_count - Get raw encoder count and CPR
 * Note: This requires sending a request and waiting for response
 */
status_t can_simple_get_encoder_count(struct can_simple_handle *h,
                                      struct can_simple_encoder_count *count);

/*
 * Phase 2: Get cached Iq data (broadcast by S1 every 100ms)
 */
status_t can_simple_get_motor_error(struct can_simple_handle *h,
                                    struct can_simple_motor_error *err);

/*
 * can_simple_get_encoder_error - Get encoder error flags
 */
status_t can_simple_get_encoder_error(struct can_simple_handle *h,
                                      struct can_simple_encoder_error *err);

/*
 * can_simple_get_sensorless_error - Get sensorless estimator error flags
 */
status_t can_simple_get_sensorless_error(struct can_simple_handle *h,
                                         struct can_simple_sensorless_error *err);

/*
 * can_simple_clear_errors - Clear all errors
 */
status_t can_simple_clear_errors(struct can_simple_handle *h);

/*
 * can_simple_estop - Trigger emergency stop
 */
status_t can_simple_estop(struct can_simple_handle *h);

/*
 * can_simple_reboot - Reboot the Odrive controller
 */
status_t can_simple_reboot(struct can_simple_handle *h);

/*
 * can_simple_set_timeout - Set default timeout for operations (ms)
 */
void can_simple_set_timeout(struct can_simple_handle *h, uint32_t timeout_ms);

/*
 * can_simple_abort_all_tx - Abort pending TX frames and flush RX FIFO
 */
void can_simple_abort_all_tx(struct can_simple_handle *h);

status_t can_simple_set_input_torque_nb(struct can_simple_handle *h,
                                          float torque);

status_t can_simple_set_axis_state_nb(struct can_simple_handle *h,
                                      uint32_t requested_state);

status_t can_simple_estop_nb(struct can_simple_handle *h);

/*
 * Phase 2: Get cached Iq data (broadcast by S1 every 100ms)
 */
status_t can_simple_get_cached_iq(struct can_simple_handle *h,
                                  float *iq_setpoint,
                                  float *iq_measured,
                                  uint32_t *age_ms);

/*
 * Phase 2: Get cached temperature data (broadcast by S1 every 1000ms)
 */
status_t can_simple_get_cached_temperature(struct can_simple_handle *h,
                                           float *fet_temp_c,
                                           float *motor_temp_c,
                                           uint32_t *age_ms);

/*
 * Phase 2: Get cached bus voltage/current data (broadcast by S1)
 */
status_t can_simple_get_cached_bus_voltage_current(struct can_simple_handle *h,
                                                   float *bus_voltage,
                                                   float *bus_current,
                                                   uint32_t *age_ms);

/*
 * Utility: Parse float from CAN data (little-endian IEEE 754)
 */
float can_simple_parse_float(const uint8_t *);

/*
 * Utility: Pack float to CAN data (little-endian IEEE 754)
 */
void can_simple_pack_float(uint8_t *, float);

#ifdef __cplusplus
}
#endif

#endif /* CAN_SIMPLE_H */
