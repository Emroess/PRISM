/*
 * cli.c - Command-line interface for valve firmware with manual positioning
 *
 * NOTE: This firmware uses newlib-nano printf which doesn't support floating-point
 * format specifiers (%f) to save space. Float values are converted to integers
 * with rounding by adding 0.5f before casting: (int)(float_value + 0.5f)
 */

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "arm_math.h"

#include "lwip/ip_addr.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"

#include "board.h"
#include "cli.h"
#include "cli_internal.h"
#include "config/all.h"
#include "drivers/uart.h"
#include "ethernetif.h"
#include "network/http.h"
#include "network/manager.h"
#include "network/net_init.h"
#include "network/nvm.h"
#include "network/ping.h"
#include "network/stream.h"
#include "odrive_manager.h"
#include "protocols/can_simple.h"
#include "valve_manager.h"
#include "valve_nvm.h"
#include "valve_presets.h"

extern struct netif gnetif;

/* Forward declaration of hard fault info structure from stm32h7xx_it.c */
struct hard_fault_info {
	uint32_t stacked_r0;
	uint32_t stacked_r1;
	uint32_t stacked_r2;
	uint32_t stacked_r3;
	uint32_t stacked_r12;
	uint32_t stacked_lr;
	uint32_t stacked_pc;
	uint32_t stacked_psr;
	uint32_t cfsr;
	uint32_t hfsr;
	uint32_t mmfar;
	uint32_t bfar;
	uint32_t valid;
};
extern volatile struct hard_fault_info g_hard_fault_info;

/* Dedicated valve context storage owned by the CLI */
static struct valve_context cli_valve_context;

/* Command buffer */
#define CLI_CMD_BUFFER_SIZE 128
static char cli_cmd_buffer[CLI_CMD_BUFFER_SIZE];
static uint32_t cli_cmd_index = 0;

/*
 * cli_print_prompt - Print command prompt
 */
static void
cli_print_prompt(struct cli_context *ctx)
{
	uart_write_string(ctx->uart, "\r\n> ", 100);
}

/*
 * cli_find_command - Find command by name using binary search
 */
const struct cli_command *
cli_find_command(const char *name)
{
	size_t low = 0;
	size_t high = cli_commands_count - 1;

	while (low <= high) {
		size_t mid = low + (high - low) / 2;
		int cmp = strcmp(name, cli_commands[mid].name);
		if (cmp == 0) {
			return &cli_commands[mid];
		} else if (cmp < 0) {
			if (mid == 0) {
				break;
			}
			high = mid - 1;
		} else {
			low = mid + 1;
		}
	}
	return NULL;
}

/*
 * cli_cmd_help - Show help for all commands
 */
static int
cli_cmd_help(struct cli_context *ctx, int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	uart_write_string(ctx->uart, "\r\nAvailable commands:\r\n", 100);
	for (size_t i = 0; i < cli_commands_count; i++) {
		uart_printf(ctx->uart, "  %s - %s\r\n", cli_commands[i].name, cli_commands[i].help);
	}
	return 0;
}

/*
 * cli_cmd_valve_damping - Set viscous damping
 */
static int
cli_cmd_valve_damping(struct cli_context *ctx, int argc, char *argv[])
{
	if (argc < 2) {
		uart_write_string(ctx->uart, "\r\nUsage: valve_damping <val>\r\n", 100);
		return 0;
	}
	
	float val = strtof(argv[1], NULL);
	status_t status = valve_set_damping(ctx->valve_ctx, val);
	if (status != STATUS_OK) {
		uart_write_string(ctx->uart, "\r\nFailed to set damping\r\n", 100);
		uart_printf(ctx->uart, "Error: %s\r\n", status_to_string(status));
	} else {
		uart_write_string(ctx->uart, "\r\nViscous damping set\r\n", 100);
	}
	return 0;
}

/*
 * cli_cmd_valve_start - Start valve control
 */
static int
cli_cmd_valve_start(struct cli_context *ctx, int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	status_t status = valve_start(ctx->valve_ctx);
	if (status == STATUS_OK) {
		uart_write_string(ctx->uart, "\r\nValve started\r\n", 100);
	} else {
		uart_write_string(ctx->uart, "\r\nFailed to start valve\r\n", 100);
		uart_printf(ctx->uart, "Error code: %d\r\n", status);
		
		/* Provide specific error messages */
		if (status == 2) {
			uart_write_string(ctx->uart, "TIMEOUT: Failed to read encoder\r\n", 100);
		} else if (status == 3) {
			uart_write_string(ctx->uart, "ERROR: Valve not in IDLE state\r\n", 100);
		} else if (status == 7) {
			uart_write_string(ctx->uart, "ERROR: Failed to set controller mode (torque control)\r\n", 100);
		} else if (status == 9) {
			uart_write_string(ctx->uart, "ERROR: Odrive reports axis error\r\n", 100);
			uart_write_string(ctx->uart, "Run 'odrive_status' to see error code\r\n", 100);
		} else if (status == 10) {
			uart_write_string(ctx->uart, "ERROR: Odrive not in closed loop state\r\n", 100);
		} else if (status == 11) {
			uart_write_string(ctx->uart, "ERROR: Failed to enable closed loop control\r\n", 100);
		}
	}
	return 0;
}

/*
 * cli_cmd_valve_stop - Stop valve control
 */
static int
cli_cmd_valve_stop(struct cli_context *ctx, int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	valve_stop(ctx->valve_ctx);
	uart_write_string(ctx->uart, "\r\nValve stopped\r\n", 100);
	return 0;
}

/*
 * cli_cmd_valve_status - Show valve status
 */
static int
cli_cmd_valve_status(struct cli_context *ctx, int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	struct valve_state *state = valve_get_state(ctx->valve_ctx);
	const char *state_str = valve_state_to_string(state->status);
	
	uart_printf(ctx->uart, "\r\nValve Status:\r\n");
	uart_printf(ctx->uart, "State:              %s\r\n", state_str);
	uart_printf(ctx->uart, "Position:           %.3f deg\r\n", state->position_deg);
	uart_printf(ctx->uart, "Velocity:           %.3f rad/s\r\n", state->omega_rad_s);
	uart_printf(ctx->uart, "Torque:             %.3f N·m\r\n", state->torque_nm);
	uart_printf(ctx->uart, "Torque limit:       %.3f N·m\r\n", ctx->valve_ctx->config.hil_tau_max_limit_nm);
	uart_printf(ctx->uart, "Viscous damping:    %.3f N·m·s/rad\r\n", ctx->valve_ctx->config.hil_b_viscous_nm_s_per_rad);
	uart_printf(ctx->uart, "Coulomb friction:   %.3f N·m\r\n", ctx->valve_ctx->config.hil_tau_c_coulomb_nm);
	uart_printf(ctx->uart, "Wall stiffness:     %.3f N·m/turn\r\n", ctx->valve_ctx->config.hil_k_w_wall_stiffness_nm_per_turn);
	uart_printf(ctx->uart, "Wall damping:       %.3f N·m·s/turn\r\n", ctx->valve_ctx->config.hil_c_w_wall_damping_nm_s_per_turn);
	uart_printf(ctx->uart, "Scale:              %.3f deg/turn\r\n", ctx->valve_ctx->config.degrees_per_turn);
	uart_printf(ctx->uart, "Epsilon:            %.6f\r\n", ctx->valve_ctx->config.hil_eps_smoothing);
	uart_printf(ctx->uart, "Energy tank:        %.6f J\r\n", state->passivity_energy_j);
	uart_printf(ctx->uart, "Loop time:          %lu us\r\n", (unsigned long)state->diag.last_loop_time_us);
	
	return 0;
}

/*
 * cli_cmd_fault_last - Show last captured hard fault registers
 */
static int
cli_cmd_fault_last(struct cli_context *ctx, int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	
	extern volatile struct hard_fault_info g_hard_fault_info; /* from stm32h7xx_it.c */
	if (g_hard_fault_info.valid == 0U) {
		uart_write_string(ctx->uart, "\r\nNo hard fault captured\r\n", 100);
	} else {
		uart_write_string(ctx->uart, "\r\n=== Last Hard Fault ===\r\n", 100);
		uart_printf(ctx->uart, "PC=0x%08lX LR=0x%08lX PSR=0x%08lX\r\n",
			(unsigned long)g_hard_fault_info.stacked_pc, (unsigned long)g_hard_fault_info.stacked_lr,
			(unsigned long)g_hard_fault_info.stacked_psr);
		uart_printf(ctx->uart, "CFSR=0x%08lX HFSR=0x%08lX MMFAR=0x%08lX BFAR=0x%08lX\r\n",
			(unsigned long)g_hard_fault_info.cfsr, (unsigned long)g_hard_fault_info.hfsr,
			(unsigned long)g_hard_fault_info.mmfar, (unsigned long)g_hard_fault_info.bfar);
		uart_printf(ctx->uart, "R0=0x%08lX R1=0x%08lX R2=0x%08lX R3=0x%08lX R12=0x%08lX\r\n",
			(unsigned long)g_hard_fault_info.stacked_r0, (unsigned long)g_hard_fault_info.stacked_r1,
			(unsigned long)g_hard_fault_info.stacked_r2, (unsigned long)g_hard_fault_info.stacked_r3,
			(unsigned long)g_hard_fault_info.stacked_r12);
	}
	return 0;
}

/*
 * cli_cmd_valve_friction - Set Coulomb friction
 */
static int
cli_cmd_valve_friction(struct cli_context *ctx, int argc, char *argv[])
{
	if (argc < 2) {
		uart_write_string(ctx->uart, "\r\nUsage: valve_friction <val>\r\n", 100);
		return 0;
	}
	
	float val = strtof(argv[1], NULL);
	status_t status = valve_set_friction(ctx->valve_ctx, val);
	if (status != STATUS_OK) {
		uart_write_string(ctx->uart, "\r\nFailed to set friction\r\n", 100);
		uart_printf(ctx->uart, "Error: %s\r\n", status_to_string(status));
	} else {
		uart_write_string(ctx->uart, "\r\nCoulomb friction set\r\n", 100);
	}
	return 0;
}

/*
 * cli_cmd_ping - Ping a remote host
 */
static int
cli_cmd_ping(struct cli_context *ctx, int argc, char *argv[])
{
	if (argc < 2) {
		uart_write_string(ctx->uart, "\r\nUsage: ping <ip_address>\r\n", 100);
		return 0;
	}
	
	status_t status = network_ping(argv[1], 1);
	if (status == STATUS_OK) {
		uart_write_string(ctx->uart, "\r\nPing initiated\r\n", 100);
	} else {
		uart_write_string(ctx->uart, "\r\nFailed to initiate ping\r\n", 100);
		uart_printf(ctx->uart, "Error: %s\r\n", status_to_string(status));
	}
	return 0;
}

/*
 * cli_cmd_ethstatus - Show Ethernet status and configuration
 */
static int
cli_cmd_ethstatus(struct cli_context *ctx, int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	
	struct network_info info;
	status_t status = network_get_info(&info);
	if (status != STATUS_OK) {
		uart_write_string(ctx->uart, "\r\nFailed to get network info\r\n", 100);
		return 0;
	}
	
	uart_write_string(ctx->uart, "\r\nEthernet Status:\r\n", 100);
	uart_printf(ctx->uart, "MAC address:       %s\r\n", info.mac_addr);
	uart_printf(ctx->uart, "IP address:        %s\r\n", info.ip_addr);
	uart_printf(ctx->uart, "Subnet mask:       %s\r\n", info.netmask);
	uart_printf(ctx->uart, "Gateway:           %s\r\n", info.gateway);
	uart_printf(ctx->uart, "Link status:       %s\r\n", info.link_up ? "UP" : "DOWN");
	uart_printf(ctx->uart, "Interface status:  %s\r\n", info.netif_up ? "UP" : "DOWN");
	
	return 0;
}

/*
 * cli_cmd_setip - Set static IP address, subnet mask, and gateway
 */
static int
cli_cmd_setip(struct cli_context *ctx, int argc, char *argv[])
{
	if (argc != 4) {
		uart_write_string(ctx->uart, "\r\nUsage: setip <ip> <netmask> <gateway>\r\n", 100);
		uart_write_string(ctx->uart, "Example: setip 192.168.1.100 255.255.255.0 192.168.1.1\r\n", 100);
		return 0;
	}
	
	status_t status = network_set_static_ip(argv[1], argv[2], argv[3]);
	if (status == STATUS_OK) {
		uart_write_string(ctx->uart, "\r\nIP configuration updated\r\n", 100);
		uart_printf(ctx->uart, "New IP: %s\r\n", argv[1]);
		uart_printf(ctx->uart, "New Netmask: %s\r\n", argv[2]);
		uart_printf(ctx->uart, "New Gateway: %s\r\n", argv[3]);
		uart_write_string(ctx->uart, "Note: Changes take effect immediately but may require network restart\r\n", 100);
	} else {
		uart_write_string(ctx->uart, "\r\nFailed to set IP configuration\r\n", 100);
	}
	
	return 0;
}

/*
 * cli_cmd_nvm_status - Show NVM network config status
 */
static int
cli_cmd_nvm_status(struct cli_context *ctx, int argc, char *argv[])
{
	const struct network_nvm_config *flash_config;
	struct network_nvm_config config;
	bool loaded;

	(void)argc;
	(void)argv;

	loaded = network_nvm_load(&config);

	uart_write_string(ctx->uart, "\r\nNVM Network Config Status:\r\n", 100);
	uart_printf(ctx->uart, "Loaded: %s\r\n", loaded ? "YES" : "NO");

	/* Read raw flash data */
	flash_config = (const struct network_nvm_config *)NETWORK_CONFIG_ADDR;
	uart_write_string(ctx->uart, "\r\nRaw Flash Data:\r\n", 100);
	uart_printf(ctx->uart, "Magic: 0x%08lX\r\n", (unsigned long)flash_config->magic);
	uart_printf(ctx->uart, "Version: %lu\r\n", (unsigned long)flash_config->version);
	uart_printf(ctx->uart, "Checksum: 0x%08lX\r\n", (unsigned long)flash_config->checksum);
	uart_printf(ctx->uart, "IP: %.16s\r\n", flash_config->ip_addr);
	uart_printf(ctx->uart, "Netmask: %.16s\r\n", flash_config->netmask);
	uart_printf(ctx->uart, "Gateway: %.16s\r\n", flash_config->gateway);

	if (loaded) {
		uint32_t expected;

		uart_printf(ctx->uart, "\r\nLoaded Config:\r\n", 100);
		uart_printf(ctx->uart, "Magic: 0x%08lX (expected 0x%08lX)\r\n", (unsigned long)config.magic, (unsigned long)NETWORK_NVM_MAGIC);
		uart_printf(ctx->uart, "Version: %lu (expected %lu)\r\n", (unsigned long)config.version, (unsigned long)NETWORK_NVM_VERSION);
		uart_printf(ctx->uart, "Checksum: 0x%08lX\r\n", (unsigned long)config.checksum);
		expected = network_nvm_calculate_checksum(&config);
		uart_printf(ctx->uart, "Expected Checksum: 0x%08lX\r\n", (unsigned long)expected);
		uart_printf(ctx->uart, "IP: %s\r\n", config.ip_addr);
		uart_printf(ctx->uart, "Netmask: %s\r\n", config.netmask);
		uart_printf(ctx->uart, "Gateway: %s\r\n", config.gateway);
		uart_printf(ctx->uart, "Valid: %s\r\n", (config.magic == NETWORK_NVM_MAGIC && config.version == NETWORK_NVM_VERSION && expected == config.checksum) ? "YES" : "NO");
	} else {
		uart_write_string(ctx->uart, "Reason: Load failed\r\n", 100);
	}
	
	return 0;
}

/*
 * cli_cmd_http - HTTP server control
 */
static int
cli_cmd_http(struct cli_context *ctx, int argc, char *argv[])
{
	if (argc < 2) {
		uart_write_string(ctx->uart, "\r\nUsage: http start | stop | status | log on|off\r\n", 100);
		return 0;
	}
	
	if (strcmp(argv[1], "start") == 0) {
		status_t status = network_http_start();
		if (status == STATUS_OK) {
			uart_write_string(ctx->uart, "\r\nHTTP server started (port 8080)\r\n", 100);
		} else {
			uart_write_string(ctx->uart, "\r\nFailed to start HTTP server\r\n", 100);
		}
	} else if (strcmp(argv[1], "stop") == 0) {
		status_t status = network_http_stop();
		if (status == STATUS_OK) {
			uart_write_string(ctx->uart, "\r\nHTTP server stopped\r\n", 100);
		} else {
			uart_write_string(ctx->uart, "\r\nFailed to stop HTTP server\r\n", 100);
		}
	} else if (strcmp(argv[1], "status") == 0) {
		bool running;
		status_t status = network_http_get_status(&running);
		if (status == STATUS_OK) {
			uart_printf(ctx->uart, "\r\nHTTP server: %s\r\n", running ? "RUNNING" : "STOPPED");
		} else {
			uart_write_string(ctx->uart, "\r\nFailed to get HTTP status\r\n", 100);
		}
	} else if (strcmp(argv[1], "log") == 0) {
		if (argc < 3) {
			uart_write_string(ctx->uart, "\r\nUsage: http log on|off\r\n", 100);
			return 0;
		}
		bool enable = strcmp(argv[2], "on") == 0;
		status_t status = network_http_set_logging(enable);
		if (status == STATUS_OK) {
			uart_printf(ctx->uart, "\r\nHTTP logging %s\r\n", enable ? "enabled" : "disabled");
		} else {
			uart_write_string(ctx->uart, "\r\nFailed to set HTTP logging\r\n", 100);
		}
	} else {
		uart_write_string(ctx->uart, "\r\nUsage: http start | stop | status | log on|off\r\n", 100);
	}
	return 0;
}

/*
 * cli_cmd_eth_stream - Ethernet data streaming control
 */
static int
cli_cmd_eth_stream(struct cli_context *ctx, int argc, char *argv[])
{
	if (argc < 2) {
		uart_write_string(ctx->uart, "\r\nUsage: eth_stream start [interval_ms] | stop\r\n", 100);
		return 0;
	}
	
	if (strcmp(argv[1], "start") == 0) {
		uint32_t interval = 100; /* default 100ms */
		if (argc >= 3) {
			interval = strtoul(argv[2], NULL, 10);
		}
		status_t status = network_stream_start(interval);
		if (status == STATUS_OK) {
			uart_write_string(ctx->uart, "\r\nEthernet streaming started (port 8888)\r\n", 100);
		} else {
			uart_write_string(ctx->uart, "\r\nFailed to start Ethernet streaming\r\n", 100);
		}
	} else if (strcmp(argv[1], "stop") == 0) {
		status_t status = network_stream_stop();
		if (status == STATUS_OK) {
			uart_write_string(ctx->uart, "\r\nEthernet streaming stopped\r\n", 100);
		} else {
			uart_write_string(ctx->uart, "\r\nFailed to stop Ethernet streaming\r\n", 100);
		}
	} else {
		uart_write_string(ctx->uart, "\r\nUsage: eth_stream start [interval_ms] | stop\r\n", 100);
	}
	return 0;
}

/*
 * cli_cmd_valve_energy - Show passivity energy tank status
 */
static int
cli_cmd_valve_energy(struct cli_context *ctx, int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	
	float energy = valve_get_energy(ctx->valve_ctx);
	uart_printf(ctx->uart, "\r\nPassivity Energy Tank: %.6f J\r\n", energy);
	return 0;
}

/*
 * cli_cmd_valve_epsilon - Set smoothing epsilon
 */
static int
cli_cmd_valve_epsilon(struct cli_context *ctx, int argc, char *argv[])
{
	if (argc < 2) {
		uart_write_string(ctx->uart, "\r\nUsage: valve_epsilon <val>\r\n", 100);
		return 0;
	}
	
	float val = strtof(argv[1], NULL);
	status_t status = valve_set_epsilon(ctx->valve_ctx, val);
	if (status != STATUS_OK) {
		uart_write_string(ctx->uart, "\r\nFailed to set epsilon\r\n", 100);
		uart_printf(ctx->uart, "Error: %s\r\n", status_to_string(status));
	} else {
		uart_write_string(ctx->uart, "\r\nSmoothing epsilon set\r\n", 100);
	}
	return 0;
}

/*
 * cli_cmd_valve_torquelimit - Set torque limit
 */
static int
cli_cmd_valve_torquelimit(struct cli_context *ctx, int argc, char *argv[])
{
	if (argc < 2) {
		uart_write_string(ctx->uart, "\r\nUsage: valve_torquelimit <val>\r\n", 100);
		return 0;
	}
	
	float val = strtof(argv[1], NULL);
	status_t status = valve_set_torque_limit(ctx->valve_ctx, val);
	if (status != STATUS_OK) {
		uart_write_string(ctx->uart, "\r\nFailed to set torque limit\r\n", 100);
		uart_printf(ctx->uart, "Error: %s\r\n", status_to_string(status));
	} else {
		uart_write_string(ctx->uart, "\r\nTorque limit set\r\n", 100);
	}
	return 0;
}

/*
 * cli_cmd_valve_wall_c - Set wall damping
 */
static int
cli_cmd_valve_wall_c(struct cli_context *ctx, int argc, char *argv[])
{
	if (argc < 2) {
		uart_write_string(ctx->uart, "\r\nUsage: valve_wall_c <val>\r\n", 100);
		return 0;
	}
	
	float val = strtof(argv[1], NULL);
	status_t status = valve_set_wall_damping(ctx->valve_ctx, val);
	if (status != STATUS_OK) {
		uart_write_string(ctx->uart, "\r\nFailed to set wall damping\r\n", 100);
		uart_printf(ctx->uart, "Error: %s\r\n", status_to_string(status));
	} else {
		uart_write_string(ctx->uart, "\r\nWall damping set\r\n", 100);
	}
	return 0;
}

/*
 * cli_cmd_valve_wall_k - Set wall stiffness
 */
static int
cli_cmd_valve_wall_k(struct cli_context *ctx, int argc, char *argv[])
{
	if (argc < 2) {
		uart_write_string(ctx->uart, "\r\nUsage: valve_wall_k <val>\r\n", 100);
		return 0;
	}
	
	float val = strtof(argv[1], NULL);
	status_t status = valve_set_wall_stiffness(ctx->valve_ctx, val);
	if (status != STATUS_OK) {
		uart_write_string(ctx->uart, "\r\nFailed to set wall stiffness\r\n", 100);
		uart_printf(ctx->uart, "Error: %s\r\n", status_to_string(status));
	} else {
		uart_write_string(ctx->uart, "\r\nWall stiffness set\r\n", 100);
	}
	return 0;
}

/*
 * cli_cmd_valve_scale - Set mechanical degrees per encoder turn
 */
static int
cli_cmd_valve_scale(struct cli_context *ctx, int argc, char *argv[])
{
	if (argc < 2) {
		uart_write_string(ctx->uart, "\r\nUsage: valve_scale <deg/turn>\r\n", 100);
		return 0;
	}
	
	float val = strtof(argv[1], NULL);
	status_t status = valve_set_scale(ctx->valve_ctx, val);
	if (status != STATUS_OK) {
		uart_write_string(ctx->uart, "\r\nFailed to set scale\r\n", 100);
		uart_printf(ctx->uart, "Error: %s\r\n", status_to_string(status));
	} else {
		uart_write_string(ctx->uart, "\r\nScale set\r\n", 100);
	}
	return 0;
}

/*
 * cli_cmd_valve_timing - Show loop timing diagnostics
 */
static int
cli_cmd_valve_timing(struct cli_context *ctx, int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	
	struct valve_state *state = valve_get_state(ctx->valve_ctx);
	uart_write_string(ctx->uart, "\r\nLoop Timing Diagnostics:\r\n", 100);
	uart_printf(ctx->uart, "Loop count:         %lu\r\n", (unsigned long)state->diag.loop_count);
	uart_printf(ctx->uart, "Min loop time:      %lu us\r\n", (unsigned long)state->diag.loop_time_min_us);
	uart_printf(ctx->uart, "Max loop time:      %lu us\r\n", (unsigned long)state->diag.loop_time_max_us);
	uart_printf(ctx->uart, "Avg loop time:      %lu us\r\n", (unsigned long)(state->diag.loop_time_sum_us / state->diag.timing_sample_count));
	uart_printf(ctx->uart, "Timing samples:     %lu\r\n", (unsigned long)state->diag.timing_sample_count);
	return 0;
}

/*
 * cli_cmd_odrive_ping - Ping ODrive to check connectivity
 */
static int
cli_cmd_odrive_ping(struct cli_context *ctx, int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	
	status_t status = odrive_ping(ctx->odrive);
	if (status == STATUS_OK) {
		uart_write_string(ctx->uart, "\r\nODrive ping successful\r\n", 100);
	} else {
		uart_write_string(ctx->uart, "\r\nODrive ping failed\r\n", 100);
		uart_printf(ctx->uart, "Error: %s\r\n", status_to_string(status));
	}
	return 0;
}

/*
 * cli_cmd_odrive_status - Show ODrive status
 */
static int
cli_cmd_odrive_status(struct cli_context *ctx, int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	
	struct odrive_status status_data;
	status_t status = odrive_get_status(ctx->odrive, &status_data);
	if (status != STATUS_OK) {
		uart_write_string(ctx->uart, "\r\nFailed to get ODrive status\r\n", 100);
		uart_printf(ctx->uart, "Error: %s\r\n", status_to_string(status));
		return 0;
	}
	
	uart_write_string(ctx->uart, "\r\nODrive Status:\r\n", 100);
	uart_printf(ctx->uart, "Axis error:         0x%08lX\r\n", (unsigned long)status_data.axis_error);
	uart_printf(ctx->uart, "Axis state:         0x%02X\r\n", status_data.axis_state);
	uart_printf(ctx->uart, "Motor flags:        0x%02X\r\n", status_data.motor_flags);
	uart_printf(ctx->uart, "Encoder flags:      0x%02X\r\n", status_data.encoder_flags);
	uart_printf(ctx->uart, "Controller status:  0x%02X\r\n", status_data.controller_status);
	return 0;
}

/*
 * cli_cmd_odrive_enable - Enable ODrive closed loop control
 */
static int
cli_cmd_odrive_enable(struct cli_context *ctx, int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	
	status_t status = odrive_enable(ctx->odrive);
	if (status == STATUS_OK) {
		uart_write_string(ctx->uart, "\r\nODrive enabled\r\n", 100);
	} else {
		uart_write_string(ctx->uart, "\r\nFailed to enable ODrive\r\n", 100);
		uart_printf(ctx->uart, "Error: %s\r\n", status_to_string(status));
	}
	return 0;
}

/*
 * cli_cmd_odrive_disable - Disable ODrive (idle mode)
 */
static int
cli_cmd_odrive_disable(struct cli_context *ctx, int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	
	status_t status = odrive_disable(ctx->odrive);
	if (status == STATUS_OK) {
		uart_write_string(ctx->uart, "\r\nODrive disabled\r\n", 100);
	} else {
		uart_write_string(ctx->uart, "\r\nFailed to disable ODrive\r\n", 100);
		uart_printf(ctx->uart, "Error: %s\r\n", status_to_string(status));
	}
	return 0;
}

/*
 * cli_cmd_odrive_estop - Emergency stop ODrive
 */
static int
cli_cmd_odrive_estop(struct cli_context *ctx, int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	
	status_t status = odrive_estop(ctx->odrive);
	if (status == STATUS_OK) {
		uart_write_string(ctx->uart, "\r\nODrive emergency stop triggered\r\n", 100);
	} else {
		uart_write_string(ctx->uart, "\r\nFailed to trigger emergency stop\r\n", 100);
		uart_printf(ctx->uart, "Error: %s\r\n", status_to_string(status));
	}
	return 0;
}

/*
 * cli_cmd_odrive_clear - Clear ODrive errors
 */
static int
cli_cmd_odrive_clear(struct cli_context *ctx, int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	
	status_t status = odrive_clear_errors(ctx->odrive);
	if (status == STATUS_OK) {
		uart_write_string(ctx->uart, "\r\nODrive errors cleared\r\n", 100);
	} else {
		uart_write_string(ctx->uart, "\r\nFailed to clear ODrive errors\r\n", 100);
		uart_printf(ctx->uart, "Error: %s\r\n", status_to_string(status));
	}
	return 0;
}

/*
 * cli_cmd_odrive_calibrate - Calibrate ODrive motor and encoder
 */
static int
cli_cmd_odrive_calibrate(struct cli_context *ctx, int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	
	uart_write_string(ctx->uart, "\r\nStarting ODrive calibration...\r\n", 100);
	status_t status = odrive_calibrate(ctx->odrive);
	if (status == STATUS_OK) {
		uart_write_string(ctx->uart, "ODrive calibration complete\r\n", 100);
	} else {
		uart_write_string(ctx->uart, "ODrive calibration failed\r\n", 100);
		uart_printf(ctx->uart, "Error: %s\r\n", status_to_string(status));
	}
	return 0;
}

/*
 * cli_cmd_odrive_torque - Set ODrive torque command
 */
static int
cli_cmd_odrive_torque(struct cli_context *ctx, int argc, char *argv[])
{
	if (argc < 2) {
		uart_write_string(ctx->uart, "\r\nUsage: odrive_torque <torque_nm>\r\n", 100);
		return 0;
	}
	
	float torque = strtof(argv[1], NULL);
	status_t status = odrive_set_torque(ctx->odrive, torque);
	if (status == STATUS_OK) {
		uart_printf(ctx->uart, "\r\nODrive torque set to %.3f N·m\r\n", torque);
	} else {
		uart_write_string(ctx->uart, "\r\nFailed to set ODrive torque\r\n", 100);
		uart_printf(ctx->uart, "Error: %s\r\n", status_to_string(status));
	}
	return 0;
}

/*
 * cli_cmd_odrive_velocity - Set ODrive velocity command
 */
static int
cli_cmd_odrive_velocity(struct cli_context *ctx, int argc, char *argv[])
{
	if (argc < 2) {
		uart_write_string(ctx->uart, "\r\nUsage: odrive_velocity <vel_turns_s>\r\n", 100);
		return 0;
	}
	
	float velocity = strtof(argv[1], NULL);
	status_t status = odrive_set_velocity(ctx->odrive, velocity);
	if (status == STATUS_OK) {
		uart_printf(ctx->uart, "\r\nODrive velocity set to %.3f turns/s\r\n", velocity);
	} else {
		uart_write_string(ctx->uart, "\r\nFailed to set ODrive velocity\r\n", 100);
		uart_printf(ctx->uart, "Error: %s\r\n", status_to_string(status));
	}
	return 0;
}

/*
 * cli_cmd_odrive_position - Set ODrive position command
 */
static int
cli_cmd_odrive_position(struct cli_context *ctx, int argc, char *argv[])
{
	if (argc < 2) {
		uart_write_string(ctx->uart, "\r\nUsage: odrive_position <pos_turns>\r\n", 100);
		return 0;
	}
	
	float position = strtof(argv[1], NULL);
	status_t status = odrive_set_position(ctx->odrive, position);
	if (status == STATUS_OK) {
		uart_printf(ctx->uart, "\r\nODrive position set to %.3f turns\r\n", position);
	} else {
		uart_write_string(ctx->uart, "\r\nFailed to set ODrive position\r\n", 100);
		uart_printf(ctx->uart, "Error: %s\r\n", status_to_string(status));
	}
	return 0;
}

/*
 * cli_cmd_odrive_limits - Set ODrive velocity and current limits
 */
static int
cli_cmd_odrive_limits(struct cli_context *ctx, int argc, char *argv[])
{
	if (argc < 3) {
		uart_write_string(ctx->uart, "\r\nUsage: odrive_limits <vel_limit> <current_limit>\r\n", 100);
		return 0;
	}
	
	float vel_limit = strtof(argv[1], NULL);
	float current_limit = strtof(argv[2], NULL);
	status_t status = odrive_set_limits(ctx->odrive, vel_limit, current_limit);
	if (status == STATUS_OK) {
		uart_write_string(ctx->uart, "\r\nODrive limits set\r\n", 100);
	} else {
		uart_write_string(ctx->uart, "\r\nFailed to set ODrive limits\r\n", 100);
		uart_printf(ctx->uart, "Error: %s\r\n", status_to_string(status));
	}
	return 0;
}

/*
 * cli_cmd_odrive_mode - Set ODrive controller mode
 */
static int
cli_cmd_odrive_mode(struct cli_context *ctx, int argc, char *argv[])
{
	if (argc < 2) {
		uart_write_string(ctx->uart, "\r\nUsage: odrive_mode <mode>\r\n", 100);
		uart_write_string(ctx->uart, "  0 = voltage control\r\n", 100);
		uart_write_string(ctx->uart, "  1 = torque control\r\n", 100);
		uart_write_string(ctx->uart, "  2 = velocity control\r\n", 100);
		uart_write_string(ctx->uart, "  3 = position control\r\n", 100);
		return 0;
	}
	
	uint8_t mode = (uint8_t)atoi(argv[1]);
	if (mode > 3) {
		uart_write_string(ctx->uart, "\r\nERROR: Invalid mode (must be 0-3)\r\n", 100);
		return 0;
	}
	
	status_t status = odrive_set_controller_mode(ctx->odrive, mode);
	if (status == STATUS_OK) {
		const char *mode_str;
		switch (mode) {
		case 0: mode_str = "voltage"; break;
		case 1: mode_str = "torque"; break;
		case 2: mode_str = "velocity"; break;
		case 3: mode_str = "position"; break;
		default: mode_str = "unknown"; break;
		}
		uart_printf(ctx->uart, "\r\nODrive mode set to %s control\r\n", mode_str);
	} else {
		uart_write_string(ctx->uart, "\r\nFailed to set ODrive mode\r\n", 100);
		uart_printf(ctx->uart, "Error: %s\r\n", status_to_string(status));
	}
	return 0;
}

/*
 * cli_cmd_odrive_pos_gain - Set ODrive position gain (Kp)
 */
static int
cli_cmd_odrive_pos_gain(struct cli_context *ctx, int argc, char *argv[])
{
	if (argc < 2) {
		uart_write_string(ctx->uart, "\r\nUsage: odrive_pos_gain <kp>\r\n", 100);
		return 0;
	}
	float kp = strtof(argv[1], NULL);
	status_t status = odrive_set_position_gain(ctx->odrive, kp);
	if (status == STATUS_OK) {
		uart_printf(ctx->uart, "\r\nODrive position gain set to %.3f\r\n", kp);
	} else {
		uart_write_string(ctx->uart, "\r\nFailed to set position gain\r\n", 100);
		uart_printf(ctx->uart, "Error: %s\r\n", status_to_string(status));
	}
	return 0;
}

/*
 * cli_cmd_odrive_vel_gains - Set ODrive velocity gains (Kp, Ki)
 */
static int
cli_cmd_odrive_vel_gains(struct cli_context *ctx, int argc, char *argv[])
{
	if (argc < 3) {
		uart_write_string(ctx->uart, "\r\nUsage: odrive_vel_gains <kp> <ki>\r\n", 100);
		return 0;
	}
	float kp = strtof(argv[1], NULL);
	float ki = strtof(argv[2], NULL);
	status_t status = odrive_set_velocity_gains(ctx->odrive, kp, ki);
	if (status == STATUS_OK) {
		uart_printf(ctx->uart, "\r\nODrive velocity gains set (kp=%.3f, ki=%.3f)\r\n", kp, ki);
	} else {
		uart_write_string(ctx->uart, "\r\nFailed to set velocity gains\r\n", 100);
		uart_printf(ctx->uart, "Error: %s\r\n", status_to_string(status));
	}
	return 0;
}

/*
 * cli_cmd_can_encoder - Read CAN encoder position and velocity
 */
static int
cli_cmd_can_encoder(struct cli_context *ctx, int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	
	struct odrive_encoder encoder;
	status_t status = odrive_get_encoder(ctx->odrive, &encoder);
	if (status != STATUS_OK) {
		uart_write_string(ctx->uart, "\r\nFailed to read encoder\r\n", 100);
		uart_printf(ctx->uart, "Error: %s\r\n", status_to_string(status));
		return 0;
	}
	
	uart_write_string(ctx->uart, "\r\nEncoder Data:\r\n", 100);
	uart_printf(ctx->uart, "Position:  %.6f turns\r\n", encoder.pos_estimate);
	uart_printf(ctx->uart, "Velocity:  %.6f turns/s\r\n", encoder.vel_estimate);
	return 0;
}

/*
 * cli_cmd_can_telemetry - Read CAN bus voltage, current, and temperatures
 */
static int
cli_cmd_can_telemetry(struct cli_context *ctx, int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	
	struct odrive_telemetry telemetry;
	status_t status = odrive_get_telemetry(ctx->odrive, &telemetry);
	if (status != STATUS_OK) {
		uart_write_string(ctx->uart, "\r\nFailed to read telemetry\r\n", 100);
		uart_printf(ctx->uart, "Error: %s\r\n", status_to_string(status));
		return 0;
	}
	
	uart_write_string(ctx->uart, "\r\nODrive Telemetry:\r\n", 100);
	uart_printf(ctx->uart, "Bus voltage:  %.2f V\r\n", telemetry.bus_voltage);
	uart_printf(ctx->uart, "Bus current:  %.2f A\r\n", telemetry.bus_current);
	uart_printf(ctx->uart, "FET temp:     %.1f °C\r\n", telemetry.fet_temp);
	uart_printf(ctx->uart, "Motor temp:   %.1f °C\r\n", telemetry.motor_temp);
	return 0;
}

/*
 * cli_cmd_can_status - Show CAN bus status
 */
static int
cli_cmd_can_status(struct cli_context *ctx, int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	
	struct can_bus_status bus_status;
	status_t status = can_get_bus_status(ctx->odrive, &bus_status);
	if (status != STATUS_OK) {
		uart_write_string(ctx->uart, "\r\nFailed to get CAN status\r\n", 100);
		uart_printf(ctx->uart, "Error: %s\r\n", status_to_string(status));
		return 0;
	}
	
	uart_write_string(ctx->uart, "\r\nCAN Bus Status:\r\n", 100);
	uart_printf(ctx->uart, "TX count:        %lu\r\n", (unsigned long)bus_status.tx_count);
	uart_printf(ctx->uart, "RX count:        %lu\r\n", (unsigned long)bus_status.rx_count);
	uart_printf(ctx->uart, "Error count:     %lu\r\n", (unsigned long)bus_status.error_count);
	uart_printf(ctx->uart, "Last error code: 0x%08lX\r\n", (unsigned long)bus_status.last_error_code);
	return 0;
}



/*
 * cli_cmd_valve_preset - Load a valve preset configuration
 */
static int
cli_cmd_valve_preset(struct cli_context *ctx, int argc, char *argv[])
{
	if (argc < 2) {
		uart_write_string(ctx->uart, "\r\nUsage: valve_preset <preset_idx>\r\n", 100);
		uart_write_string(ctx->uart, "  0 = light (butterfly/faucet)\r\n", 100);
		uart_write_string(ctx->uart, "  1 = medium (ball valve)\r\n", 100);
		uart_write_string(ctx->uart, "  2 = heavy (gate valve)\r\n", 100);
		uart_write_string(ctx->uart, "  3 = industrial (globe/gas)\r\n", 100);
		return 0;
	}
	
	int preset_idx = atoi(argv[1]);
	if (preset_idx < 0 || preset_idx > 3) {
		uart_write_string(ctx->uart, "\r\nERROR: Invalid preset (must be 0-3)\r\n", 100);
		return 0;
	}
	
	/* Use 0.0f for degrees to indicate "use preset default" */
	status_t status = valve_load_preset(ctx->valve_ctx, preset_idx, 0.0f);
	if (status == STATUS_OK) {
		uart_write_string(ctx->uart, "\r\nValve preset loaded\r\n", 100);
	} else {
		uart_write_string(ctx->uart, "\r\nFailed to load preset\r\n", 100);
		uart_printf(ctx->uart, "Error: %s\r\n", status_to_string(status));
	}
	return 0;
}

/*
 * cli_cmd_valve_preset_save - Save current configuration as preset
 */
static int
cli_cmd_valve_preset_save(struct cli_context *ctx, int argc, char *argv[])
{
	if (argc < 2) {
		uart_write_string(ctx->uart, "\r\nUsage: valve_preset_save <preset_idx>\r\n", 100);
		return 0;
	}
	
	int preset_idx = atoi(argv[1]);
	if (preset_idx < 0 || preset_idx > 3) {
		uart_write_string(ctx->uart, "\r\nERROR: Invalid preset (must be 0-3)\r\n", 100);
		return 0;
	}
	
	status_t status = valve_save_preset(ctx->valve_ctx, preset_idx);
	if (status == STATUS_OK) {
		uart_write_string(ctx->uart, "\r\nCurrent config saved as preset\r\n", 100);
	} else {
		uart_write_string(ctx->uart, "\r\nFailed to save preset\r\n", 100);
		uart_printf(ctx->uart, "Error: %s\r\n", status_to_string(status));
	}
	return 0;
}

/*
 * cli_cmd_valve_preset_show - Show all valve presets and their parameters
 */
static int
cli_cmd_valve_preset_show(struct cli_context *ctx, int argc, char *argv[])
{
	(void)argc;
	(void)argv;
	
	const struct preset_params *presets = valve_get_presets();
	
	uart_write_string(ctx->uart, "\r\nValve Presets:\r\n", 100);
	for (uint32_t i = 0; i < VALVE_PRESET_COUNT; i++) {
		uart_printf(ctx->uart, "\r\nPreset %u: %s\r\n", i, presets[i].name);
		uart_printf(ctx->uart, "  Torque limit:     %.3f N·m\r\n", presets[i].torque_limit_nm);
		uart_printf(ctx->uart, "  Default travel:   %.1f deg\r\n", presets[i].default_travel_deg);
		uart_printf(ctx->uart, "  Viscous damping:  %.3f N·m·s/rad\r\n", presets[i].hil_b_viscous_nm_s_per_rad);
		uart_printf(ctx->uart, "  Coulomb friction: %.3f N·m\r\n", presets[i].hil_tau_c_coulomb_nm);
		uart_printf(ctx->uart, "  Wall stiffness:   %.3f N·m/turn\r\n", presets[i].hil_k_w_wall_stiffness_nm_per_turn);
		uart_printf(ctx->uart, "  Wall damping:     %.3f N·m·s/turn\r\n", presets[i].hil_c_w_wall_damping_nm_s_per_turn);
		uart_printf(ctx->uart, "  Smoothing eps:    %.6f\r\n", presets[i].hil_eps_smoothing);
	}
	
	return 0;
}



/*
 * CLI command table - sorted alphabetically for binary search
 */
const struct cli_command cli_commands[] = {
	{"can_encoder", cli_cmd_can_encoder, "Read CAN encoder position and velocity"},
	{"can_status", cli_cmd_can_status, "Show CAN bus status"},
	{"can_telemetry", cli_cmd_can_telemetry, "Read CAN bus voltage, current, and temperatures"},
	{"eth_stream", cli_cmd_eth_stream, "Start/stop Ethernet data streaming (port 8888)"},
	{"ethstatus", cli_cmd_ethstatus, "Show Ethernet status and configuration"},
	{"fault_last", cli_cmd_fault_last, "Show last captured hard fault registers"},
	{"help", cli_cmd_help, "Show this help"},
	{"http", cli_cmd_http, "HTTP server control (start/stop/status/log)"},
	{"nvm_status", cli_cmd_nvm_status, "Show NVM network config status"},
	{"odrive_calibrate", cli_cmd_odrive_calibrate, "Calibrate ODrive motor and encoder"},
	{"odrive_clear", cli_cmd_odrive_clear, "Clear ODrive errors"},
	{"odrive_disable", cli_cmd_odrive_disable, "Disable ODrive (idle mode)"},
	{"odrive_enable", cli_cmd_odrive_enable, "Enable ODrive closed loop control"},
	{"odrive_estop", cli_cmd_odrive_estop, "Emergency stop ODrive"},
	{"odrive_limits", cli_cmd_odrive_limits, "Set ODrive velocity and current limits"},
	{"odrive_mode", cli_cmd_odrive_mode, "Set ODrive controller mode (0=voltage, 1=torque, 2=velocity, 3=position)"},
	{"odrive_pos_gain", cli_cmd_odrive_pos_gain, "Set ODrive position gain (Kp)"},
	{"odrive_vel_gains", cli_cmd_odrive_vel_gains, "Set ODrive velocity gains (Kp, Ki)"},
	{"odrive_ping", cli_cmd_odrive_ping, "Ping ODrive to check connectivity"},
	{"odrive_position", cli_cmd_odrive_position, "Set ODrive position command"},
	{"odrive_status", cli_cmd_odrive_status, "Show ODrive status"},
	{"odrive_torque", cli_cmd_odrive_torque, "Set ODrive torque command"},
	{"odrive_velocity", cli_cmd_odrive_velocity, "Set ODrive velocity command"},
	{"ping", cli_cmd_ping, "Ping an IP address to test network connectivity"},
	{"setip", cli_cmd_setip, "Set static IP address, subnet mask, and gateway"},
	{"valve_damping", cli_cmd_valve_damping, "Set viscous damping (N·m·s/rad)"},
	{"valve_energy", cli_cmd_valve_energy, "Show passivity energy tank status"},
	{"valve_epsilon", cli_cmd_valve_epsilon, "Set smoothing epsilon"},
	{"valve_friction", cli_cmd_valve_friction, "Set Coulomb friction (N·m)"},
	{"valve_preset", cli_cmd_valve_preset, "Load a valve preset configuration"},
	{"valve_preset_save", cli_cmd_valve_preset_save, "Save current configuration as preset"},
	{"valve_preset_show", cli_cmd_valve_preset_show, "Show all valve presets and their parameters"},
	{"valve_scale", cli_cmd_valve_scale, "Set mechanical degrees per encoder turn"},
	{"valve_start", cli_cmd_valve_start, "Start valve control"},
	{"valve_status", cli_cmd_valve_status, "Show valve status"},
	{"valve_stop", cli_cmd_valve_stop, "Stop valve control"},
	{"valve_timing", cli_cmd_valve_timing, "Show loop timing diagnostics"},
	{"valve_torquelimit", cli_cmd_valve_torquelimit, "Set torque limit (N·m)"},
	{"valve_wall_c", cli_cmd_valve_wall_c, "Set wall damping (N·m·s/turn)"},
	{"valve_wall_k", cli_cmd_valve_wall_k, "Set wall stiffness (N·m/turn)"},
};

const size_t cli_commands_count = sizeof(cli_commands) / sizeof(cli_commands[0]);

/*
 * cli_process_command - Process a single command
 */
static void
cli_process_command(struct cli_context *ctx, const char *cmd)
{
	char cmd_copy[CLI_CMD_BUFFER_SIZE];
	char *argv[10];
	int argc = 0;
	
	/* Make a copy we can modify */
	strncpy(cmd_copy, cmd, sizeof(cmd_copy) - 1);
	cmd_copy[sizeof(cmd_copy) - 1] = '\0';
	
	/* Tokenize command */
	char *token = strtok(cmd_copy, " \t");
	while (token != NULL && argc < 10) {
		argv[argc++] = token;
		token = strtok(NULL, " \t");
	}
	
	if (argc == 0) {
		return; /* Empty command */
	}
	
/* Process commands */
const struct cli_command *found_cmd = cli_find_command(argv[0]);
if (found_cmd != NULL) {
    found_cmd->handler(ctx, argc, argv);
} else {
    uart_printf(ctx->uart, "\r\nUnknown command: %s\r\n", argv[0]);
    uart_write_string(ctx->uart, "Type 'help' for available commands\r\n", 100);
}
}

/*
 * cli_process_input - Process incoming UART data
 */
static void
cli_process_input(struct cli_context *ctx, char c)
{
	if (c == '\r' || c == '\n') {
		/* End of command */
		if (cli_cmd_index > 0) {
			cli_cmd_buffer[cli_cmd_index] = '\0';
			cli_process_command(ctx, cli_cmd_buffer);
			cli_cmd_index = 0;
		}
		cli_print_prompt(ctx);
	} else if (c == '\b' || c == 0x7F) {
		/* Backspace */
		if (cli_cmd_index > 0) {
			cli_cmd_index--;
			uart_write_string(ctx->uart, "\b \b", 100); /* Erase character */
		}
	} else if (cli_cmd_index < CLI_CMD_BUFFER_SIZE - 1) {
		/* Regular character */
		cli_cmd_buffer[cli_cmd_index++] = c;
		uart_write_string(ctx->uart, &c, 1); /* Echo character */
	}
}

/*
 * cli_init - Initialize CLI with valve context
 */
status_t
cli_init(struct cli_context *ctx, struct uart_handle *uart, struct fdcan_handle *can)
{
	status_t status;

	if (ctx == NULL || uart == NULL || can == NULL) {
		return STATUS_ERROR_INVALID_PARAM;
	}

	ctx->uart = uart;
	ctx->can = can;
	ctx->valve_ctx = &cli_valve_context;

	/* Initialize CAN simple for Odrive communication */
	struct can_simple_config can_cfg = {
		.can = can,
		.node_id = ODRIVE_DEFAULT_NODE_ID,
		.timeout_ms = 1000,  /* 1 second timeout */
	};
	status = can_simple_init(&ctx->odrive, &can_cfg);
	if (status != STATUS_OK) {
		return status;
	}

	/* Initialize NVM and load presets */
	status = valve_nvm_init();
	if (status != STATUS_OK) {
		return status;
	}
	status = valve_presets_init();
	if (status != STATUS_OK) {
		return status;
	}

	/* Initialize valve haptic module */
	status = valve_haptic_init(ctx->valve_ctx, ctx->odrive);
	if (status != STATUS_OK) {
		return status;
	}

	cli_print_prompt(ctx);
	return STATUS_OK;
}

/*
 * cli_run - Run CLI main loop (never returns)
 */
void
cli_run(struct cli_context *ctx)
{
	uint8_t buffer[16];
	size_t bytes_read;
	
	/* Initial prompt is printed by cli_init, don't print another one here */
	
	while (1) {
		/* Read UART input */
		if (uart_read(ctx->uart, buffer, sizeof(buffer), &bytes_read) == STATUS_OK) {
			for (size_t i = 0; i < bytes_read; i++) {
				cli_process_input(ctx, (char)buffer[i]);
			}
		}
		
		/* Process lwIP timeouts (required for ping and other network operations) */
		sys_check_timeouts();
		
		/*
		 * Valve runs autonomously in TIM6 ISR at 1kHz.
		 * No service call needed - fully independent operation.
		 * CLI just processes commands; valve executes deterministically.
		 */
		
		/* Small delay to prevent UART busy-waiting */
		board_delay_ms(1);
		
		/* Process Ethernet tasks */
		ethernet_process();
		ethernet_stream_process();
		ethernet_http_process();
	}
}
