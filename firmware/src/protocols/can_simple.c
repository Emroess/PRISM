/*
 * can_simple.c - Odrive CANSIMPLE Protocol Implementation
 *
 * Implements command/response handling for Odrive motor controllers.
 *
 * Protocol Reference:
 * https://docs.odriverobotics.com/v/latest/can-protocol.html
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "board.h"
#include "can_simple.h"
#include "fdcan.h"
#include "status.h"

/*
 * CANSIMPLE Handle Structure
 */
struct can_simple_handle {
	struct fdcan_handle *can;  /* FDCAN handle */
	uint8_t node_id;           /* Odrive node ID */
	uint32_t timeout_ms;       /* Default timeout */
	volatile uint32_t encoder_seq;
	volatile uint32_t encoder_timestamp_ms;
	float encoder_pos_turns;
	float encoder_vel_turns_s;
	volatile uint32_t heartbeat_timestamp_ms;
	volatile uint32_t heartbeat_axis_error;
	volatile uint8_t heartbeat_axis_state;
	volatile uint8_t heartbeat_procedure_result;
	volatile uint8_t heartbeat_trajectory_done;
	volatile uint8_t heartbeat_controller_status;
	/* Phase 2: Cached Iq and temperature data */
	volatile uint32_t iq_timestamp_ms;
	float iq_setpoint;
	float iq_measured;
	volatile uint32_t temperature_timestamp_ms;
	float fet_temperature_c;
	float motor_temperature_c;
	/* Phase 2: Cached bus voltage/current data */
	volatile uint32_t bus_voltage_timestamp_ms;
	float bus_voltage;
	float bus_current;
};

/*
 * Static handle (single Odrive instance)
 */
static struct can_simple_handle can_simple_ctx;

static void can_simple_fdcan_rx_callback(const struct can_frame *frame, void *context);

/*
 * Helper: Construct CAN message ID from node_id and cmd_id
 */
static uint32_t
make_msg_id(uint8_t node_id, uint8_t cmd_id)
{
	return ((uint32_t)node_id << 5) | (cmd_id & 0x1FU);
}

/*
 * can_simple_parse_float - Parse IEEE 754 float from little-endian bytes
 */
float
can_simple_parse_float(const uint8_t *data)
{
	union {
		uint32_t u32;
		float f;
	} converter;

	/* Little-endian byte order */
	converter.u32 = ((uint32_t)data[0])       |
	                ((uint32_t)data[1] << 8)  |
	                ((uint32_t)data[2] << 16) |
	                ((uint32_t)data[3] << 24);

	return converter.f;
}

/*
 * can_simple_pack_float - Pack IEEE 754 float to little-endian bytes
 *
 * Uses type-punning union (legal in C99) to avoid UB.
 * Assumes ARM Cortex-M7 is little-endian (always true for STM32).
 * Explicit byte extraction ensures wire format is LE regardless.
 */
void
can_simple_pack_float(uint8_t *data, float value)
{
	union {
		uint32_t u32;
		float f;
	} converter;

	converter.f = value;

	/* Little-endian byte order */
	data[0] = (uint8_t)(converter.u32 & 0xFFU);
	data[1] = (uint8_t)((converter.u32 >> 8) & 0xFFU);
	data[2] = (uint8_t)((converter.u32 >> 16) & 0xFFU);
	data[3] = (uint8_t)((converter.u32 >> 24) & 0xFFU);
}

static void
can_simple_fdcan_rx_callback(const struct can_frame *frame, void *context)
{
	struct can_simple_handle *h = (struct can_simple_handle *)context;
	uint32_t id;
	uint8_t node;
	uint8_t cmd;
	uint32_t now_ms;

	if (h == NULL || frame == NULL) {
		return;
	}

	if (frame->extended != 0U || frame->rtr != 0U) {
		return;
	}

	id = frame->id;
	node = (uint8_t)(id >> 5);
	if (node != h->node_id) {
		return;
	}

	cmd = (uint8_t)(id & 0x1FU);
	now_ms = board_get_systick_ms();

	if (cmd == CAN_SIMPLE_CMD_GET_ENCODER_ESTIMATES && frame->dlc >= 8) {
		const float pos = can_simple_parse_float(&frame->data[0]);
		const float vel = can_simple_parse_float(&frame->data[4]);
		uint32_t next_seq = h->encoder_seq + 1U;
		if (next_seq == 0U) {
			next_seq = 1U;
		}
		h->encoder_pos_turns = pos;
		h->encoder_vel_turns_s = vel;
		h->encoder_timestamp_ms = now_ms;
		h->encoder_seq = next_seq;
		return;
	}

	if (cmd == CAN_SIMPLE_CMD_HEARTBEAT && frame->dlc >= 8) {
		h->heartbeat_axis_error = ((uint32_t)frame->data[0]) |
		                          ((uint32_t)frame->data[1] << 8) |
		                          ((uint32_t)frame->data[2] << 16) |
		                          ((uint32_t)frame->data[3] << 24);
		h->heartbeat_axis_state = frame->data[4];
		h->heartbeat_procedure_result = frame->data[5];
		h->heartbeat_trajectory_done = frame->data[6];
		h->heartbeat_controller_status = frame->data[7];
		h->heartbeat_timestamp_ms = now_ms;
		return;
	}

	/* Phase 2: Cache Iq data (broadcast every 100ms by S1) */
	if (cmd == CAN_SIMPLE_CMD_GET_IQ && frame->dlc >= 8) {
		h->iq_setpoint = can_simple_parse_float(&frame->data[0]);
		h->iq_measured = can_simple_parse_float(&frame->data[4]);
		h->iq_timestamp_ms = now_ms;
		return;
	}

	/* Phase 2: Cache temperature data (broadcast every 1000ms by S1) */
	if (cmd == CAN_SIMPLE_CMD_GET_TEMPERATURE && frame->dlc >= 8) {
		h->fet_temperature_c = can_simple_parse_float(&frame->data[0]);
		h->motor_temperature_c = can_simple_parse_float(&frame->data[4]);
		h->temperature_timestamp_ms = now_ms;
		return;
	}

	/* Phase 2: Cache bus voltage/current data (broadcast by S1) */
	if (cmd == CAN_SIMPLE_CMD_GET_BUS_VOLTAGE_CURRENT && frame->dlc >= 8) {
		h->bus_voltage = can_simple_parse_float(&frame->data[0]);
		h->bus_current = can_simple_parse_float(&frame->data[4]);
		h->bus_voltage_timestamp_ms = now_ms;
		return;
	}
}

/*
 * can_simple_init - Initialize CANSIMPLE protocol handler
 */
status_t
can_simple_init(struct can_simple_handle **h, const struct can_simple_config *cfg)
{
	if (h == NULL || cfg == NULL || cfg->can == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	if (cfg->node_id > 63U) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	/* Initialize static handle */
	memset(&can_simple_ctx, 0, sizeof(can_simple_ctx));
	can_simple_ctx.can = cfg->can;
	can_simple_ctx.node_id = cfg->node_id;
	can_simple_ctx.timeout_ms = cfg->timeout_ms;
	can_simple_ctx.encoder_seq = 0U;
	can_simple_ctx.encoder_timestamp_ms = 0U;
	can_simple_ctx.encoder_pos_turns = 0.0f;
	can_simple_ctx.encoder_vel_turns_s = 0.0f;
	can_simple_ctx.heartbeat_timestamp_ms = 0U;
	can_simple_ctx.heartbeat_axis_error = 0U;
	can_simple_ctx.heartbeat_axis_state = 0U;

	fdcan_set_rx_callback(cfg->can, can_simple_fdcan_rx_callback, &can_simple_ctx);
	(void)fdcan_enable_interrupts(cfg->can);

	/* Configure hardware filter to accept only messages from this ODrive node.
	 * ODrive uses node_id in bits [10:5] of the CAN ID: (node_id << 5) | msg_type
	 * Accept range [node_id << 5, (node_id << 5) | 0x1F] to capture all message
	 * types (commands, responses, broadcasts) from this node.
	 * This eliminates RX interrupts for other nodes on the bus. */
	uint16_t node_base = (uint16_t)(cfg->node_id << 5);
	uint16_t node_end = (uint16_t)(node_base | 0x1FU);
	(void)fdcan_set_std_range_filter(cfg->can, node_base, node_end, 0);

	*h = &can_simple_ctx;

	return STATUS_OK;
}

/*
 * can_simple_deinit - Deinitialize CANSIMPLE handler
 */
status_t
can_simple_deinit(struct can_simple_handle *h)
{
	if (h == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	memset(h, 0, sizeof(*h));

	return STATUS_OK;
}

/*
 * can_simple_flush_rx - Flush RX buffer to discard stale messages
 */
void
can_simple_flush_rx(struct can_simple_handle *h)
{
	struct can_frame discard;
	
	if (h == NULL) {
		return;
	}
	
	/* Read and discard all pending messages */
	while (fdcan_receive(h->can, &discard) == STATUS_OK) {
		/* Discard */
	}
}

/*
 * can_simple_set_axis_state - Set requested axis state
 */
status_t
can_simple_set_axis_state(struct can_simple_handle *h, uint32_t requested_state)
{
	struct can_frame frame;
	status_t status;

	if (h == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	/* Construct CAN frame */
	frame.id = make_msg_id(h->node_id, CAN_SIMPLE_CMD_SET_AXIS_STATE);
	frame.dlc = 4;
	frame.extended = 0;
	frame.rtr = 0;

	/* Pack requested state (uint32_t, little-endian) */
	frame.data[0] = (uint8_t)(requested_state & 0xFFU);
	frame.data[1] = (uint8_t)((requested_state >> 8) & 0xFFU);
	frame.data[2] = (uint8_t)((requested_state >> 16) & 0xFFU);
	frame.data[3] = (uint8_t)((requested_state >> 24) & 0xFFU);

	/* Send frame */
	status = fdcan_transmit(h->can, &frame, h->timeout_ms);

	return status;
}

status_t
can_simple_set_axis_state_nb(struct can_simple_handle *h, uint32_t requested_state)
{
	struct can_frame frame;

	if (h == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	frame.id = make_msg_id(h->node_id, CAN_SIMPLE_CMD_SET_AXIS_STATE);
	frame.dlc = 4;
	frame.extended = 0;
	frame.rtr = 0;

	frame.data[0] = (uint8_t)(requested_state & 0xFFU);
	frame.data[1] = (uint8_t)((requested_state >> 8) & 0xFFU);
	frame.data[2] = (uint8_t)((requested_state >> 16) & 0xFFU);
	frame.data[3] = (uint8_t)((requested_state >> 24) & 0xFFU);

	return fdcan_transmit(h->can, &frame, 0);
}

/*
 * can_simple_set_controller_mode - Set control mode and input mode
 */
status_t
can_simple_set_controller_mode(struct can_simple_handle *h,
                               uint32_t control_mode,
                               uint32_t input_mode)
{
	struct can_frame frame;
	status_t status;

	if (h == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	/* Construct CAN frame */
	frame.id = make_msg_id(h->node_id, CAN_SIMPLE_CMD_SET_CONTROLLER_MODE);
	frame.dlc = 8;
	frame.extended = 0;
	frame.rtr = 0;

	/* Pack control mode (uint32_t) and input mode (uint32_t) */
	frame.data[0] = (uint8_t)(control_mode & 0xFFU);
	frame.data[1] = (uint8_t)((control_mode >> 8) & 0xFFU);
	frame.data[2] = (uint8_t)((control_mode >> 16) & 0xFFU);
	frame.data[3] = (uint8_t)((control_mode >> 24) & 0xFFU);
	
	frame.data[4] = (uint8_t)(input_mode & 0xFFU);
	frame.data[5] = (uint8_t)((input_mode >> 8) & 0xFFU);
	frame.data[6] = (uint8_t)((input_mode >> 16) & 0xFFU);
	frame.data[7] = (uint8_t)((input_mode >> 24) & 0xFFU);

	/* Debug: Print what we're sending */
	/* Note: This would require UART access, so we'll add a debug command instead */

	/* Send frame */
	status = fdcan_transmit(h->can, &frame, h->timeout_ms);

	return status;
}

/*
 * can_simple_set_velocity - Set velocity setpoint
 */
status_t
can_simple_set_velocity(struct can_simple_handle *h, float velocity, float torque_ff)
{
	struct can_frame frame;
	status_t status;

	if (h == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	/* Construct CAN frame */
	frame.id = make_msg_id(h->node_id, CAN_SIMPLE_CMD_SET_INPUT_VEL);
	frame.dlc = 8;
	frame.extended = 0;
	frame.rtr = 0;

	/* Pack velocity (float) and torque FF (float) */
	can_simple_pack_float(&frame.data[0], velocity);
	can_simple_pack_float(&frame.data[4], torque_ff);

	/* Send frame */
	status = fdcan_transmit(h->can, &frame, h->timeout_ms);

	return status;
}

/*
 * can_simple_set_position - Set position setpoint
 */
status_t
can_simple_set_position(struct can_simple_handle *h, float position,
                        int16_t velocity_ff, int16_t torque_ff)
{
	struct can_frame frame;
	status_t status;

	if (h == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	/* Construct CAN frame */
	frame.id = make_msg_id(h->node_id, CAN_SIMPLE_CMD_SET_INPUT_POS);
	frame.dlc = 8;
	frame.extended = 0;
	frame.rtr = 0;

	/* Pack position (float), velocity FF (int16), torque FF (int16) */
	can_simple_pack_float(&frame.data[0], position);
	frame.data[4] = (uint8_t)(velocity_ff & 0xFFU);
	frame.data[5] = (uint8_t)((velocity_ff >> 8) & 0xFFU);
	frame.data[6] = (uint8_t)(torque_ff & 0xFFU);
	frame.data[7] = (uint8_t)((torque_ff >> 8) & 0xFFU);

	/* Send frame */
	status = fdcan_transmit(h->can, &frame, h->timeout_ms);

	return status;
}

status_t
can_simple_set_position_nb(struct can_simple_handle *h, float position,
				int16_t velocity_ff, int16_t torque_ff)
{
	struct can_frame frame;
	status_t status;

	if (h == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	frame.id = make_msg_id(h->node_id, CAN_SIMPLE_CMD_SET_INPUT_POS);
	frame.dlc = 8;
	frame.extended = 0;
	frame.rtr = 0;

	can_simple_pack_float(&frame.data[0], position);
	frame.data[4] = (uint8_t)(velocity_ff & 0xFFU);
	frame.data[5] = (uint8_t)((velocity_ff >> 8) & 0xFFU);
	frame.data[6] = (uint8_t)(torque_ff & 0xFFU);
	frame.data[7] = (uint8_t)((torque_ff >> 8) & 0xFFU);

	status = fdcan_transmit(h->can, &frame, 0);
	return status;
}

/*
 * can_simple_set_input_torque - Set torque setpoint
 */
status_t
can_simple_set_input_torque(struct can_simple_handle *h, float torque)
{
	struct can_frame frame;
	status_t status;

	if (h == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	frame.id = make_msg_id(h->node_id, CAN_SIMPLE_CMD_SET_INPUT_TORQUE);
	frame.dlc = 8;
	frame.extended = 0;
	frame.rtr = 0;

	can_simple_pack_float(&frame.data[0], torque);
	/* Velocity and position feedforwards not used in torque control - set to 0 */
	frame.data[4] = 0;
	frame.data[5] = 0;
	frame.data[6] = 0;
	frame.data[7] = 0;

	status = fdcan_transmit(h->can, &frame, 10);

	return status;
}

status_t
can_simple_set_input_torque_nb(struct can_simple_handle *h, float torque)
{
	struct can_frame frame;
	status_t status;

	if (h == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	frame.id = make_msg_id(h->node_id, CAN_SIMPLE_CMD_SET_INPUT_TORQUE);
	frame.dlc = 8;
	frame.extended = 0;
	frame.rtr = 0;

	can_simple_pack_float(&frame.data[0], torque);
	/* Velocity and position feedforwards not used in torque control - set to 0 */
	frame.data[4] = 0;
	frame.data[5] = 0;
	frame.data[6] = 0;
	frame.data[7] = 0;

	status = fdcan_transmit(h->can, &frame, 0);
	return status;
}

/*
 * can_simple_set_limits - Set velocity and current limits
 */
status_t
can_simple_set_limits(struct can_simple_handle *h, float vel_limit, float current_limit)
{
	struct can_frame frame;
	status_t status;

	if (h == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	/* Construct CAN frame */
	frame.id = make_msg_id(h->node_id, CAN_SIMPLE_CMD_SET_LIMITS);
	frame.dlc = 8;
	frame.extended = 0;
	frame.rtr = 0;

	/* Pack velocity limit (float) and current limit (float) */
	can_simple_pack_float(&frame.data[0], vel_limit);
	can_simple_pack_float(&frame.data[4], current_limit);

	/* Send frame */
	status = fdcan_transmit(h->can, &frame, h->timeout_ms);

	return status;
}

/*
 * can_simple_set_traj_vel_limit - Set trajectory velocity limit
 */
status_t
can_simple_set_traj_vel_limit(struct can_simple_handle *h, float traj_vel_limit)
{
	struct can_frame frame;
	status_t status;

	if (h == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	/* Construct CAN frame */
	frame.id = make_msg_id(h->node_id, CAN_SIMPLE_CMD_SET_TRAJ_VEL_LIMIT);
	frame.dlc = 4;
	frame.extended = 0;
	frame.rtr = 0;

	/* Pack trajectory velocity limit (float) */
	can_simple_pack_float(&frame.data[0], traj_vel_limit);

	/* Send frame */
	status = fdcan_transmit(h->can, &frame, h->timeout_ms);

	return status;
}

/*
 * can_simple_set_traj_accel_limits - Set trajectory acceleration/deceleration limits
 */
status_t
can_simple_set_traj_accel_limits(struct can_simple_handle *h, float accel_limit, float decel_limit)
{
	struct can_frame frame;
	status_t status;

	if (h == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	/* Construct CAN frame */
	frame.id = make_msg_id(h->node_id, CAN_SIMPLE_CMD_SET_TRAJ_ACCEL_LIMITS);
	frame.dlc = 8;
	frame.extended = 0;
	frame.rtr = 0;

	/* Pack accel limit (float) and decel limit (float) */
	can_simple_pack_float(&frame.data[0], accel_limit);
	can_simple_pack_float(&frame.data[4], decel_limit);

	/* Send frame */
	status = fdcan_transmit(h->can, &frame, h->timeout_ms);

	return status;
}

/*
 * can_simple_set_pos_gain - Set position controller gain
 */
status_t
can_simple_set_pos_gain(struct can_simple_handle *h, float pos_gain)
{
	struct can_frame frame;
	status_t status;

	if (h == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	/* Construct CAN frame */
	frame.id = make_msg_id(h->node_id, CAN_SIMPLE_CMD_SET_POS_GAIN);
	frame.dlc = 4;
	frame.extended = 0;
	frame.rtr = 0;

	/* Pack position gain (float) */
	can_simple_pack_float(&frame.data[0], pos_gain);

	/* Send frame */
	status = fdcan_transmit(h->can, &frame, h->timeout_ms);

	return status;
}

/*
 * can_simple_set_vel_gains - Set velocity controller gains
 */
status_t
can_simple_set_vel_gains(struct can_simple_handle *h, float vel_gain, float vel_integrator_gain)
{
	struct can_frame frame;
	status_t status;

	if (h == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	/* Construct CAN frame */
	frame.id = make_msg_id(h->node_id, CAN_SIMPLE_CMD_SET_VEL_GAINS);
	frame.dlc = 8;
	frame.extended = 0;
	frame.rtr = 0;

	/* Pack vel gain (float) and vel integrator gain (float) */
	can_simple_pack_float(&frame.data[0], vel_gain);
	can_simple_pack_float(&frame.data[4], vel_integrator_gain);

	/* Send frame */
	status = fdcan_transmit(h->can, &frame, h->timeout_ms);

	return status;
}

/*
 * can_simple_set_linear_count - Set position using raw encoder counts
 */
status_t
can_simple_set_linear_count(struct can_simple_handle *h, int32_t position_counts)
{
	struct can_frame frame;
	status_t status;

	if (h == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	/* Construct CAN frame */
	frame.id = make_msg_id(h->node_id, CAN_SIMPLE_CMD_SET_LINEAR_COUNT);
	frame.dlc = 4;
	frame.extended = 0;
	frame.rtr = 0;

	/* Pack position_counts (int32, little-endian) */
	frame.data[0] = (uint8_t)(position_counts & 0xFFU);
	frame.data[1] = (uint8_t)((position_counts >> 8) & 0xFFU);
	frame.data[2] = (uint8_t)((position_counts >> 16) & 0xFFU);
	frame.data[3] = (uint8_t)((position_counts >> 24) & 0xFFU);

	/* Send frame */
	status = fdcan_transmit(h->can, &frame, h->timeout_ms);

	return status;
}

/*
 * can_simple_get_heartbeat - Request heartbeat (axis state/errors)
 */
status_t
can_simple_get_heartbeat(struct can_simple_handle *h,
                         struct can_simple_heartbeat *heartbeat)
{
	struct can_frame tx_frame, rx_frame;
	status_t status;
	uint32_t expected_id;

	if (h == NULL || heartbeat == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	/* Send heartbeat request (DLC=0) */
	tx_frame.id = make_msg_id(h->node_id, CAN_SIMPLE_CMD_HEARTBEAT);
	tx_frame.dlc = 0;
	tx_frame.extended = 0;
	tx_frame.rtr = 0;

	status = fdcan_transmit(h->can, &tx_frame, h->timeout_ms);
	if (status != STATUS_OK) {
		return status;
	}

	/* Wait for response with same message ID */
	expected_id = tx_frame.id;
	
	/* Simple polling for response (timeout in ms) */
	uint32_t attempts = (h->timeout_ms + 9U) / 10U;  /* ~10 polls per ms */
	if (attempts == 0U) {
		attempts = 1U;
	} else if (attempts > 100U) {
		attempts = 100U;
	}

	for (uint32_t i = 0; i < attempts; i++) {
		status = fdcan_receive(h->can, &rx_frame);
		if (status == STATUS_OK && rx_frame.id == expected_id) {
			/* Parse heartbeat data */
			if (rx_frame.dlc >= 8) {
				heartbeat->axis_error = ((uint32_t)rx_frame.data[0]) |
				                       ((uint32_t)rx_frame.data[1] << 8) |
				                       ((uint32_t)rx_frame.data[2] << 16) |
				                       ((uint32_t)rx_frame.data[3] << 24);
				heartbeat->axis_state = rx_frame.data[4];
				heartbeat->procedure_result = rx_frame.data[5];
				heartbeat->trajectory_done = rx_frame.data[6];
				heartbeat->controller_status = rx_frame.data[7];
				return STATUS_OK;
			}
		}
		/* Small delay between polls */
			for (volatile int d = 0; d < 10000; d++) ;
	}

	return STATUS_ERROR_TIMEOUT;
}

status_t
can_simple_get_cached_heartbeat(struct can_simple_handle *h,
                                struct can_simple_heartbeat *heartbeat,
                                uint32_t *age_ms)
{
	uint32_t timestamp;

	if (h == NULL || heartbeat == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	timestamp = h->heartbeat_timestamp_ms;
	if (timestamp == 0U) {
		return STATUS_ERROR_BUFFER_EMPTY;
	}

	heartbeat->axis_state = h->heartbeat_axis_state;
	heartbeat->axis_error = h->heartbeat_axis_error;
	heartbeat->procedure_result = h->heartbeat_procedure_result;
	heartbeat->trajectory_done = h->heartbeat_trajectory_done;
	heartbeat->controller_status = h->heartbeat_controller_status;

	if (age_ms != NULL) {
		uint32_t now_ms = board_get_systick_ms();
		*age_ms = now_ms - timestamp;
	}

	return STATUS_OK;
}

status_t
can_simple_get_cached_encoder(struct can_simple_handle *h,
                              struct can_simple_encoder_estimates *est,
                              uint32_t *age_ms,
                              uint32_t *seq_out)
{
	uint32_t seq_before;
	uint32_t seq_after;
	uint32_t timestamp;
	float pos;
	float vel;

	if (h == NULL || est == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	if (h->encoder_seq == 0U) {
		return STATUS_ERROR_BUFFER_EMPTY;
	}

	do {
		seq_before = h->encoder_seq;
		pos = h->encoder_pos_turns;
		vel = h->encoder_vel_turns_s;
		timestamp = h->encoder_timestamp_ms;
		seq_after = h->encoder_seq;
	} while (seq_before != seq_after);

	est->position = pos;
	est->velocity = vel;

	if (seq_out != NULL) {
		*seq_out = seq_after;
	}
	if (age_ms != NULL) {
		uint32_t now_ms = board_get_systick_ms();
		*age_ms = now_ms - timestamp;
	}

	return STATUS_OK;
}

void
can_simple_reset_encoder_cache(struct can_simple_handle *h)
{
	if (h == NULL) {
		return;
	}

	h->encoder_seq = 0U;
	h->encoder_timestamp_ms = 0U;
}

/*
 * can_simple_get_encoder_count - Get raw encoder count and CPR
 */
status_t
can_simple_get_encoder_count(struct can_simple_handle *h,
                             struct can_simple_encoder_count *count)
{
	struct can_frame tx_frame, rx_frame;
	status_t status;
	uint32_t expected_id;

	if (h == NULL || count == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	/* Send request */
	tx_frame.id = make_msg_id(h->node_id, CAN_SIMPLE_CMD_GET_ENCODER_COUNT);
	tx_frame.dlc = 0;
	tx_frame.extended = 0;
	tx_frame.rtr = 0;

	status = fdcan_transmit(h->can, &tx_frame, h->timeout_ms);
	if (status != STATUS_OK) {
		return status;
	}

	/* Wait for response */
	expected_id = tx_frame.id;
	
	uint32_t attempts = h->timeout_ms / 10;
	for (uint32_t i = 0; i < attempts; i++) {
		status = fdcan_receive(h->can, &rx_frame);
		if (status == STATUS_OK && rx_frame.id == expected_id) {
			if (rx_frame.dlc >= 8) {
				/* Parse shadow_count (int32) and CPR (int32) - little-endian */
				count->shadow_count = (int32_t)(((uint32_t)rx_frame.data[0])       |
				                                ((uint32_t)rx_frame.data[1] << 8)  |
				                                ((uint32_t)rx_frame.data[2] << 16) |
				                                ((uint32_t)rx_frame.data[3] << 24));
				count->cpr = (int32_t)(((uint32_t)rx_frame.data[4])       |
				                       ((uint32_t)rx_frame.data[5] << 8)  |
				                       ((uint32_t)rx_frame.data[6] << 16) |
				                       ((uint32_t)rx_frame.data[7] << 24));
				return STATUS_OK;
			}
		}
		for (volatile int d = 0; d < 10000; d++) ;
	}

	return STATUS_ERROR_TIMEOUT;
}

/*
 * can_simple_get_motor_error - Get motor error flags
 */
status_t
can_simple_get_motor_error(struct can_simple_handle *h,
                           struct can_simple_motor_error *err)
{
	struct can_frame tx_frame, rx_frame;
	status_t status;
	uint32_t expected_id;

	if (h == NULL || err == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	/* Send request */
	tx_frame.id = make_msg_id(h->node_id, CAN_SIMPLE_CMD_GET_MOTOR_ERROR);
	tx_frame.dlc = 0;
	tx_frame.extended = 0;
	tx_frame.rtr = 0;

	status = fdcan_transmit(h->can, &tx_frame, h->timeout_ms);
	if (status != STATUS_OK) {
		return status;
	}

	/* Wait for response */
	expected_id = tx_frame.id;
	
	uint32_t attempts = h->timeout_ms / 10;
	for (uint32_t i = 0; i < attempts; i++) {
		status = fdcan_receive(h->can, &rx_frame);
		if (status == STATUS_OK && rx_frame.id == expected_id) {
			if (rx_frame.dlc >= 4) {
				/* Parse 32-bit error flags (little-endian) */
				err->error_flags = ((uint32_t)rx_frame.data[0])       |
				                   ((uint32_t)rx_frame.data[1] << 8)  |
				                   ((uint32_t)rx_frame.data[2] << 16) |
				                   ((uint32_t)rx_frame.data[3] << 24);
				return STATUS_OK;
			}
		}
		for (volatile int d = 0; d < 10000; d++) ;
	}

	return STATUS_ERROR_TIMEOUT;
}

/*
 * can_simple_get_encoder_error - Get encoder error flags
 */
status_t
can_simple_get_encoder_error(struct can_simple_handle *h,
                             struct can_simple_encoder_error *err)
{
	struct can_frame tx_frame, rx_frame;
	status_t status;
	uint32_t expected_id;

	if (h == NULL || err == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	/* Send request */
	tx_frame.id = make_msg_id(h->node_id, CAN_SIMPLE_CMD_GET_ENCODER_ERROR);
	tx_frame.dlc = 0;
	tx_frame.extended = 0;
	tx_frame.rtr = 0;

	status = fdcan_transmit(h->can, &tx_frame, h->timeout_ms);
	if (status != STATUS_OK) {
		return status;
	}

	/* Wait for response */
	expected_id = tx_frame.id;
	
	uint32_t attempts = h->timeout_ms / 10;
	for (uint32_t i = 0; i < attempts; i++) {
		status = fdcan_receive(h->can, &rx_frame);
		if (status == STATUS_OK && rx_frame.id == expected_id) {
			if (rx_frame.dlc >= 4) {
				/* Parse 32-bit error flags (little-endian) */
				err->error_flags = ((uint32_t)rx_frame.data[0])       |
				                   ((uint32_t)rx_frame.data[1] << 8)  |
				                   ((uint32_t)rx_frame.data[2] << 16) |
				                   ((uint32_t)rx_frame.data[3] << 24);
				return STATUS_OK;
			}
		}
		for (volatile int d = 0; d < 10000; d++) ;
	}

	return STATUS_ERROR_TIMEOUT;
}

/*
 * can_simple_get_sensorless_error - Get sensorless estimator error flags
 */
status_t
can_simple_get_sensorless_error(struct can_simple_handle *h,
                                struct can_simple_sensorless_error *err)
{
	struct can_frame tx_frame, rx_frame;
	status_t status;
	uint32_t expected_id;

	if (h == NULL || err == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	/* Send request */
	tx_frame.id = make_msg_id(h->node_id, CAN_SIMPLE_CMD_GET_SENSORLESS_ERROR);
	tx_frame.dlc = 0;
	tx_frame.extended = 0;
	tx_frame.rtr = 0;

	status = fdcan_transmit(h->can, &tx_frame, h->timeout_ms);
	if (status != STATUS_OK) {
		return status;
	}

	/* Wait for response */
	expected_id = tx_frame.id;
	
	uint32_t attempts = h->timeout_ms / 10;
	for (uint32_t i = 0; i < attempts; i++) {
		status = fdcan_receive(h->can, &rx_frame);
		if (status == STATUS_OK && rx_frame.id == expected_id) {
			if (rx_frame.dlc >= 4) {
				/* Parse 32-bit error flags (little-endian) */
				err->error_flags = ((uint32_t)rx_frame.data[0])       |
				                   ((uint32_t)rx_frame.data[1] << 8)  |
				                   ((uint32_t)rx_frame.data[2] << 16) |
				                   ((uint32_t)rx_frame.data[3] << 24);
				return STATUS_OK;
			}
		}
		for (volatile int d = 0; d < 10000; d++) ;
	}

	return STATUS_ERROR_TIMEOUT;
}

/*
 * can_simple_clear_errors - Clear all errors
 */
status_t
can_simple_clear_errors(struct can_simple_handle *h)
{
	struct can_frame frame;
	status_t status;

	if (h == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	/* Send clear errors command */
	frame.id = make_msg_id(h->node_id, CAN_SIMPLE_CMD_CLEAR_ERRORS);
	frame.dlc = 0;
	frame.extended = 0;
	frame.rtr = 0;

	status = fdcan_transmit(h->can, &frame, h->timeout_ms);

	return status;
}

/*
 * can_simple_estop - Trigger emergency stop
 */
status_t
can_simple_estop(struct can_simple_handle *h)
{
	struct can_frame frame;
	status_t status;

	if (h == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	/* Send emergency stop command */
	frame.id = make_msg_id(h->node_id, CAN_SIMPLE_CMD_ESTOP);
	frame.dlc = 0;
	frame.extended = 0;
	frame.rtr = 0;

	status = fdcan_transmit(h->can, &frame, h->timeout_ms);

	return status;
}

status_t
can_simple_estop_nb(struct can_simple_handle *h)
{
	struct can_frame frame;

	if (h == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	frame.id = make_msg_id(h->node_id, CAN_SIMPLE_CMD_ESTOP);
	frame.dlc = 0;
	frame.extended = 0;
	frame.rtr = 0;

	return fdcan_transmit(h->can, &frame, 0);
}

/*
 * can_simple_reboot - Reboot the Odrive controller
 */
status_t
can_simple_reboot(struct can_simple_handle *h)
{
	struct can_frame frame;
	status_t status;

	if (h == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	/* Send reboot command */
	frame.id = make_msg_id(h->node_id, CAN_SIMPLE_CMD_REBOOT);
	frame.dlc = 0;
	frame.extended = 0;
	frame.rtr = 0;

	status = fdcan_transmit(h->can, &frame, h->timeout_ms);

	return status;
}

/*
 * can_simple_set_timeout - Update default timeout (ms)
 */
void
can_simple_set_timeout(struct can_simple_handle *h, uint32_t timeout_ms)
{
	if (h == NULL) {
		return;
	}

	if (timeout_ms == 0U) {
		timeout_ms = 1U;
	}

	h->timeout_ms = timeout_ms;
}

/*
 * can_simple_abort_all_tx - Abort pending TX frames and flush RX FIFO
 */
void
can_simple_abort_all_tx(struct can_simple_handle *h)
{
	if (h == NULL) {
		return;
	}

	fdcan_abort_all_tx(h->can);
	can_simple_flush_rx(h);
}

/*
 * Phase 2: Get cached Iq data from S1 broadcasts
 */
status_t
can_simple_get_cached_iq(struct can_simple_handle *h,
                         float *iq_setpoint,
                         float *iq_measured,
                         uint32_t *age_ms)
{
uint32_t now_ms;

if (h == NULL || iq_setpoint == NULL || iq_measured == NULL) {
return STATUS_ERROR_INVALID_PARAM;
}

if (h->iq_timestamp_ms == 0U) {
return STATUS_ERROR_BUFFER_EMPTY;
}

*iq_setpoint = h->iq_setpoint;
*iq_measured = h->iq_measured;

if (age_ms != NULL) {
now_ms = board_get_systick_ms();
*age_ms = now_ms - h->iq_timestamp_ms;
}

return STATUS_OK;
}

/*
 * Phase 2: Get cached temperature data from S1 broadcasts
 */
status_t
can_simple_get_cached_temperature(struct can_simple_handle *h,
                                  float *fet_temp_c,
                                  float *motor_temp_c,
                                  uint32_t *age_ms)
{
uint32_t now_ms;

if (h == NULL || fet_temp_c == NULL || motor_temp_c == NULL) {
return STATUS_ERROR_INVALID_PARAM;
}

if (h->temperature_timestamp_ms == 0U) {
return STATUS_ERROR_BUFFER_EMPTY;
}

*fet_temp_c = h->fet_temperature_c;
*motor_temp_c = h->motor_temperature_c;

if (age_ms != NULL) {
now_ms = board_get_systick_ms();
*age_ms = now_ms - h->temperature_timestamp_ms;
}

return STATUS_OK;
}

/*
 * Phase 2: Get cached bus voltage/current data from S1 broadcasts
 */
status_t
can_simple_get_cached_bus_voltage_current(struct can_simple_handle *h,
                                          float *bus_voltage,
                                          float *bus_current,
                                          uint32_t *age_ms)
{
uint32_t now_ms;

if (h == NULL || bus_voltage == NULL || bus_current == NULL) {
return STATUS_ERROR_INVALID_PARAM;
}

if (h->bus_voltage_timestamp_ms == 0U) {
return STATUS_ERROR_BUFFER_EMPTY;
}

*bus_voltage = h->bus_voltage;
*bus_current = h->bus_current;

if (age_ms != NULL) {
now_ms = board_get_systick_ms();
*age_ms = now_ms - h->bus_voltage_timestamp_ms;
}

return STATUS_OK;
}
