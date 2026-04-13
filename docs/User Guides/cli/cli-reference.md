[⬅ Back to Main README](../../../README.md#user-guides)

# PRISM CLI Reference Guide

This document provides a complete reference for all commands available in the PRISM command-line interface.

## Table of Contents

- [Getting Help](#getting-help)
- [Valve Control Commands](#valve-control-commands)
- [Valve Configuration Commands](#valve-configuration-commands)
- [Valve Preset Commands](#valve-preset-commands)
- [ODrive Motor Control Commands](#odrive-motor-control-commands)
- [ODrive Configuration Commands](#odrive-configuration-commands)
- [CAN Bus Commands](#can-bus-commands)
- [Network Commands](#network-commands)
- [Performance Monitoring Commands](#performance-monitoring-commands)
- [Diagnostics Commands](#diagnostics-commands)

---

## Getting Help

### `help`

Display a list of all available commands with brief descriptions.

**Usage:**

```
help
```

**Example:**

```
> help
Available commands:
  can_encoder - Read CAN encoder position and velocity
  can_status - Show CAN bus status
  ...
```

---

## Valve Control Commands

These commands control the valve's operational state and provide real-time status information.

### `valve_start`

Start the valve control system. This command initializes the haptic feedback control loop and begins actively controlling the valve based on the configured parameters.

**Usage:**

```
valve_start
```

**Example:**

```
> valve_start
Valve started
```

**Notes:**

- The ODrive must be enabled and in the correct control mode before starting valve control
- Ensure all safety parameters (torque limits, wall boundaries) are properly configured
- The control loop runs at high frequency (1kHz+) for responsive haptic feedback

### `valve_stop`

Stop the valve control system and disable active haptic feedback control.

**Usage:**

```
valve_stop
```

**Example:**

```
> valve_stop
Valve stopped
```

**Notes:**

- This command safely disables the control loop
- The motor remains enabled but torque commands cease
- Use this before making configuration changes

### `valve_status`

Display comprehensive valve system status including current position, velocity, torque, and control parameters.

**Usage:**

```
valve_status
```

**Example:**

```
> valve_status
Valve Status:
Position:    1234 counts (45.2 deg)
Velocity:    567 counts/s (20.8 deg/s)
Torque:      0.123 N·m
State:       RUNNING
Damping:     0.050 N·m·s/rad
Friction:    0.010 N·m
Wall K:      1.000 N·m/turn
Wall C:      0.100 N·m·s/turn
```

**Information Provided:**

- Current encoder position (raw counts and mechanical degrees)
- Current velocity (counts/s and mechanical degrees/s)
- Current applied torque
- Control system state (IDLE, RUNNING, ERROR)
- Active haptic parameters

### `valve_energy`

Display the current passivity energy tank level. This value is used to ensure system stability and prevent energy generation that could lead to unstable behavior.

**Usage:**

```
valve_energy
```

**Example:**

```
> valve_energy
Passivity Energy Tank: 0.000123 J
```

**Notes:**

- The energy tank should remain positive for passive, stable behavior
- Negative values may indicate parameter tuning issues
- Energy dissipation should match physical expectations

### `valve_timing`

Show detailed timing diagnostics for the control loop including execution time, cycle period, and timing violations.

**Usage:**

```
valve_timing
```

**Example:**

```
> valve_timing
Loop Timing Diagnostics:
Loop frequency:     1000 Hz
Avg execution:      234 μs
Max execution:      456 μs
Overruns:           0
```

**Information Provided:**

- Target control loop frequency
- Average execution time per cycle
- Maximum execution time observed
- Number of timing overruns (late cycles)

---

## Valve Configuration Commands

These commands adjust the haptic feedback characteristics and physical parameters of the valve simulation.

### `valve_damping`

Set the viscous damping coefficient for the valve. This parameter controls velocity-dependent resistance.

**Usage:**

```
valve_damping <value>
```

**Parameters:**

- `value` - Damping coefficient in N·m·s/rad (Newton-meters-seconds per radian)

**Example:**

```
> valve_damping 0.05
Viscous damping set
```

**Notes:**

- Higher values create more resistance to motion
- Typical range: 0.01 to 0.5 N·m·s/rad
- Affects feel smoothness and stability
- Too high can make the valve feel sluggish

### `valve_friction`

Set the Coulomb (static) friction torque for the valve. This parameter simulates friction that opposes motion regardless of velocity.

**Usage:**

```
valve_friction <value>
```

**Parameters:**

- `value` - Friction torque in N·m (Newton-meters)

**Example:**

```
> valve_friction 0.01
Coulomb friction set
```

**Notes:**

- Creates a constant resistance to overcome
- Typical range: 0.005 to 0.05 N·m
- Simulates bearing friction and seal resistance
- Too high can cause jerky motion

### `valve_epsilon`

Set the smoothing epsilon parameter used in friction calculations to prevent discontinuities at zero velocity.

**Usage:**

```
valve_epsilon <value>
```

**Parameters:**

- `value` - Smoothing parameter (dimensionless)

**Example:**

```
> valve_epsilon 0.001
Smoothing epsilon set
```

**Notes:**

- Smaller values create sharper transitions
- Typical range: 0.0001 to 0.01
- Affects friction behavior near zero velocity
- Balance between realism and numerical stability

### `valve_torquelimit`

Set the maximum torque that can be commanded to the motor. This is a critical safety parameter.

**Usage:**

```
valve_torquelimit <value>
```

**Parameters:**

- `value` - Maximum torque in N·m (Newton-meters)

**Example:**

```
> valve_torquelimit 0.5
Torque limit set
```

**Notes:**

- Safety limit to prevent damage
- Should be below motor and mechanical limits
- Typical range: 0.1 to 2.0 N·m depending on hardware
- Also limits during testing to prevent accidents

### `valve_wall_k`

Set the wall stiffness coefficient. This defines how hard the virtual walls are at the travel limits.

**Usage:**

```
valve_wall_k <value>
```

**Parameters:**

- `value` - Wall stiffness in N·m/turn

**Example:**

```
> valve_wall_k 1.0
Wall stiffness set
```

**Notes:**

- Higher values create harder walls
- Typical range: 0.5 to 5.0 N·m/turn
- Defines end-stop feel
- Very high values may cause instability

### `valve_wall_c`

Set the wall damping coefficient. This controls energy dissipation when hitting virtual walls.

**Usage:**

```
valve_wall_c <value>
```

**Parameters:**

- `value` - Wall damping in N·m·s/turn

**Example:**

```
> valve_wall_c 0.1
Wall damping set
```

**Notes:**

- Prevents bouncing off walls
- Typical range: 0.05 to 0.5 N·m·s/turn
- Critical for stability near limits
- Too low may cause oscillations

### `valve_scale`

Set the mechanical scaling factor that defines how many mechanical degrees correspond to one encoder revolution.

**Usage:**

```
valve_scale <value>
```

**Parameters:**

- `value` - Mechanical degrees per encoder turn

**Example:**

```
> valve_scale 360.0
Mechanical scale set
```

**Notes:**

- Depends on gear ratio or direct drive configuration
- Direct drive: typically 360.0 degrees/turn
- Geared: depends on gear ratio
- Critical for accurate position representation

---

## Valve Preset Commands

Presets allow you to save and recall complete valve configurations for different applications or testing scenarios.

### `valve_preset`

Load a previously saved valve preset configuration.

**Usage:**

```
valve_preset <preset_name>
```

**Parameters:**

- `preset_name` - Name of the preset to load (no spaces)

**Example:**

```
> valve_preset default
Loaded preset: default
```

**Available Presets:**

- `default` - Standard configuration for general use
- `smooth` - Low friction, light damping for smooth feel
- `stiff` - High stiffness for precise positioning
- `heavy` - High damping for sluggish, heavy feel
- Custom presets you've created

**Notes:**

- Instantly updates all valve parameters
- Changes take effect on next control cycle
- Does not require stopping the valve

### `valve_preset_save`

Save the current valve configuration as a named preset.

**Usage:**

```
valve_preset_save <preset_name>
```

**Parameters:**

- `preset_name` - Name for the new preset (no spaces)

**Example:**

```
> valve_preset_save myconfig
Preset saved: myconfig
```

**Notes:**

- Saves all current valve parameters
- Overwrites preset if name already exists
- Stored in non-volatile memory
- Survives power cycles

### `valve_preset_show`

Display all available presets and their parameter values.

**Usage:**

```
valve_preset_show
```

**Example:**

```
> valve_preset_show
Available Presets:
[default]
  damping: 0.050
  friction: 0.010
  wall_k: 1.000
  wall_c: 0.100
  ...
[smooth]
  damping: 0.020
  friction: 0.005
  ...
```

**Notes:**

- Shows all parameters for each preset
- Helps compare different configurations
- Useful for documentation and version control

---

## ODrive Motor Control Commands

These commands control the ODrive motor controller that drives the valve's haptic feedback motor.

### `odrive_ping`

Test connectivity with the ODrive motor controller over CAN bus.

**Usage:**

```
odrive_ping
```

**Example:**

```
> odrive_ping
ODrive heartbeat received
Node ID: 0
Axis state: IDLE
```

**Notes:**

- Verifies CAN communication is working
- Shows basic ODrive status
- Should respond within 100ms
- Troubleshoot CAN wiring if no response

### `odrive_status`

Display comprehensive ODrive status including state, errors, position, velocity, and current.

**Usage:**

```
odrive_status
```

**Example:**

```
> odrive_status
ODrive Status:
Axis State:    CLOSED_LOOP_CONTROL
Position:      1234.5 counts
Velocity:      56.7 counts/s
Torque:        0.123 N·m
Bus Voltage:   24.1 V
Bus Current:   1.45 A
Errors:        None
```

**Information Provided:**

- Current operational state
- Encoder position and velocity
- Applied torque/current
- Power supply voltage and current
- Any active errors or warnings

### `odrive_enable`

Enable the ODrive for closed-loop motor control. This transitions the ODrive from idle to active control mode.

**Usage:**

```
odrive_enable
```

**Example:**

```
> odrive_enable
ODrive enabled
```

**Notes:**

- Motor must be calibrated first
- Required before valve control can start
- Motor will actively hold position
- Draws current even when stationary

### `odrive_disable`

Disable the ODrive and enter idle mode. The motor will coast freely.

**Usage:**

```
odrive_disable
```

**Example:**

```
> odrive_disable
ODrive disabled
```

**Notes:**

- Motor will coast (no active control)
- Reduces power consumption
- Safe for making mechanical adjustments
- Use before valve_stop when shutting down

### `odrive_estop`

Trigger an emergency stop on the ODrive. This immediately disables the motor.

**Usage:**

```
odrive_estop
```

**Example:**

```
> odrive_estop
ODrive emergency stop triggered
```

**Notes:**

- Use in emergency situations only
- Immediately cuts motor power
- Requires odrive_clear before re-enabling
- Does not damage hardware

### `odrive_clear`

Clear any active errors on the ODrive.

**Usage:**

```
odrive_clear
```

**Example:**

```
> odrive_clear
ODrive errors cleared
```

**Notes:**

- Required after estop or error conditions
- Does not fix underlying problems
- Check odrive_status after clearing
- May need to recalibrate after some errors

### `odrive_calibrate`

Perform a full calibration sequence for the motor and encoder. This includes offset calibration and index search.

**Usage:**

```
odrive_calibrate
```

**Example:**

```
> odrive_calibrate
ODrive calibration started
Note: Motor will move during calibration
```

**Notes:**

- Required after power-up (if not saved)
- Motor will rotate during calibration
- Ensure clear range of motion
- Takes several seconds to complete
- Can save calibration to skip on subsequent boots

### `odrive_torque`

Send a direct torque command to the ODrive. ODrive must be in torque control mode.

**Usage:**

```
odrive_torque <value>
```

**Parameters:**

- `value` - Torque setpoint in N·m

**Example:**

```
> odrive_torque 0.1
Torque command sent: 0.100 N·m
```

**Notes:**

- Direct low-level control
- Bypasses valve control system
- Use for testing and diagnostics
- Limited by configured current limits

### `odrive_velocity`

Send a velocity command to the ODrive. ODrive must be in velocity control mode.

**Usage:**

```
odrive_velocity <value>
```

**Parameters:**

- `value` - Velocity setpoint in turns/s

**Example:**

```
> odrive_velocity 0.5
Velocity command sent: 0.500 turns/s
```

**Notes:**

- Constant velocity mode
- Subject to velocity and current limits
- Use for testing motor response
- Trajectory generation is automatic

### `odrive_position`

Send a position command to the ODrive. ODrive must be in position control mode.

**Usage:**

```
odrive_position <value>
```

**Parameters:**

- `value` - Position setpoint in turns

**Example:**

```
> odrive_position 1.5
Position command sent: 1.500 turns
```

**Notes:**

- Moves to absolute position
- Uses configured position PID gains
- Subject to velocity and current limits
- Smooth trajectory with velocity ramping

---

## ODrive Configuration Commands

These commands configure the ODrive's control parameters and operational limits.

### `odrive_mode`

Set the ODrive's control mode (voltage, torque, velocity, or position control).

**Usage:**

```
odrive_mode <mode>
```

**Parameters:**

- `mode` - Control mode:
    - `0` - Voltage control (open loop)
    - `1` - Torque control
    - `2` - Velocity control
    - `3` - Position control

**Example:**

```
> odrive_mode 1
ODrive mode set to TORQUE_CONTROL
```

**Notes:**

- Valve control typically uses torque mode (1)
- Position mode (3) for automated testing
- Mode must match command type
- Changing mode doesn't change other settings

### `odrive_limits`

Set velocity and current limits for the ODrive. These are safety parameters.

**Usage:**

```
odrive_limits <vel_limit> <current_limit>
```

**Parameters:**

- `vel_limit` - Maximum velocity in turns/s
- `current_limit` - Maximum current in Amperes

**Example:**

```
> odrive_limits 2.0 10.0
Limits set: vel=2.000 turns/s, current=10.000 A
```

**Notes:**

- Protects motor and mechanics from damage
- Velocity limit prevents overspeed
- Current limit prevents overheating
- Should match motor specifications
- Lower limits for safer testing

### `odrive_pos_gain`

Set the proportional gain (Kp) for position control mode.

**Usage:**

```
odrive_pos_gain <kp>
```

**Parameters:**

- `kp` - Position proportional gain

**Example:**

```
> odrive_pos_gain 20.0
Position gain set: Kp=20.000
```

**Notes:**

- Higher Kp = stiffer position control
- Too high causes oscillation
- Typical range: 5.0 to 50.0
- Only affects position control mode
- Should be tuned for your specific setup

### `odrive_vel_gains`

Set the proportional and integral gains (Kp, Ki) for velocity control mode.

**Usage:**

```
odrive_vel_gains <kp> <ki>
```

**Parameters:**

- `kp` - Velocity proportional gain
- `ki` - Velocity integral gain

**Example:**

```
> odrive_vel_gains 0.15 0.3
Velocity gains set: Kp=0.150, Ki=0.300
```

**Notes:**

- Kp affects responsiveness
- Ki eliminates steady-state error
- Higher gains = faster response but less stability
- Typical Kp range: 0.05 to 0.5
- Typical Ki range: 0.1 to 1.0
- Critical for smooth velocity tracking

---

## CAN Bus Commands

These commands provide diagnostics and monitoring for the CAN bus communication with the ODrive.

### `can_encoder`

Read the current encoder position and velocity from the ODrive over CAN.

**Usage:**

```
can_encoder
```

**Example:**

```
> can_encoder
Encoder feedback:
Position: 1234.56 counts
Velocity: 78.90 counts/s
```

**Notes:**

- Raw encoder data from ODrive
- Updates continuously in real-time
- Useful for verifying encoder operation
- Compare with valve_status for consistency

### `can_telemetry`

Read bus voltage, current, and temperature data from the ODrive.

**Usage:**

```
can_telemetry
```

**Example:**

```
> can_telemetry
Telemetry:
Bus Voltage:    24.1 V
Bus Current:    1.45 A
FET Temp:       35.2 °C
Motor Temp:     38.7 °C
```

**Notes:**

- Monitor for overheating
- Bus current indicates load
- FET temp should stay below 80°C
- Motor temp depends on motor specs
- High current = high force/acceleration

### `can_status`

Display CAN bus communication statistics and status.

**Usage:**

```
can_status
```

**Example:**

```
> can_status
CAN Bus Status:
Bitrate:        1 Mbps
Messages TX:    12345
Messages RX:    12340
Errors:         0
Bus state:      ACTIVE
```

**Information Provided:**

- CAN bitrate configuration
- Message transmit/receive counts
- Error counters
- Bus operational state
- Node ID configuration

---

## Network Commands

These commands configure and monitor the Ethernet network interface.

### `ethstatus`

Display current Ethernet interface status and IP configuration.

**Usage:**

```
ethstatus
```

**Example:**

```
> ethstatus
Ethernet Status:
MAC address:       02:00:00:12:34:56
IP address:        192.168.1.100
Subnet mask:       255.255.255.0
Gateway:           192.168.1.1
Link status:       UP
Interface status:  UP
```

**Notes:**

- Shows active network configuration
- Link status indicates physical connection
- Interface status shows if network stack is running
- MAC address is unique to device

### `setip`

Configure static IP address, subnet mask, and gateway. Changes take effect immediately and are saved to non-volatile memory.

**Usage:**

```
setip <ip_address> <netmask> <gateway>
```

**Parameters:**

- `ip_address` - Static IP address (e.g., 192.168.1.100)
- `netmask` - Subnet mask (e.g., 255.255.255.0)
- `gateway` - Default gateway (e.g., 192.168.1.1)

**Example:**

```
> setip 192.168.1.100 255.255.255.0 192.168.1.1
IP configuration updated
New IP: 192.168.1.100
New Netmask: 255.255.255.0
New Gateway: 192.168.1.1
```

**Notes:**

- Configuration persists across reboots
- Ensure IP doesn't conflict with other devices
- Gateway must be on same subnet
- Changes take effect immediately
- May briefly interrupt network connections

### `ping`

Send ICMP ping packets to test network connectivity to a remote host.

**Usage:**

```
ping <ip_address>
```

**Parameters:**

- `ip_address` - Target IP address to ping

**Example:**

```
> ping 192.168.1.1
Ping initiated
```

**Notes:**

- Tests network reachability
- Results appear in subsequent output
- Use to verify gateway connectivity
- Timeout if host unreachable
- Requires functional network configuration

### `nvm_status`

Display detailed information about the network configuration stored in non-volatile memory, including magic numbers, checksums, and validation status.

**Usage:**

```
nvm_status
```

**Example:**

```
> nvm_status
NVM Network Config Status:
Loaded: YES

Raw Flash Data:
Magic: 0xABCD1234
Version: 1
Checksum: 0x12345678
IP: 192.168.1.100
...
```

**Notes:**

- Diagnostic tool for network configuration storage
- Shows raw flash data and validation
- Helps troubleshoot boot configuration issues
- Magic number indicates valid configuration
- Checksum verifies data integrity

### `http`

Control the HTTP web server for browser-based control interface.

**Usage:**

```
http start | stop | status | log on|off
```

**Subcommands:**

- `start` - Start HTTP server on port 8080
- `stop` - Stop HTTP server
- `status` - Show if server is running
- `log on|off` - Enable/disable HTTP request logging

**Examples:**

```
> http start
HTTP server started (port 8080)

> http status
HTTP server: RUNNING

> http log on
HTTP logging enabled

> http stop
HTTP server stopped
```

**Notes:**

- Provides web-based control interface
- Access via http://[device-ip]:8080
- REST API available for automation
- Logging useful for debugging
- Server must be manually started after boot

### `eth_stream`

Control real-time data streaming over TCP for data logging and visualization.

**Usage:**

```
eth_stream start [interval_ms] | stop
```

**Parameters:**

- `interval_ms` - Optional streaming interval in milliseconds (default: 100ms)

**Examples:**

```
> eth_stream start
Ethernet streaming started (port 8888)

> eth_stream start 50
Ethernet streaming started (port 8888)

> eth_stream stop
Ethernet streaming stopped
```

**Notes:**

- Streams position, velocity, torque data
- Client connects to port 8888
- Lower interval = higher data rate
- Minimum practical interval: ~10ms
- Multiple clients supported
- Binary format for efficiency

---

## Performance Monitoring Commands

These commands provide detailed performance metrics and data logging capabilities.

### `perf_stats`

Display comprehensive performance statistics including min/max/mean values for key measurements.

**Usage:**

```
perf_stats
```

**Example:**

```
> perf_stats
Performance Statistics:
Position:
  Min:     -10.5 deg
  Max:     45.2 deg
  Mean:    17.3 deg
Velocity:
  Min:     -120.3 deg/s
  Max:     98.7 deg/s
  Mean:    5.2 deg/s
Torque:
  Min:     -0.234 N·m
  Max:     0.198 N·m
  Mean:    0.012 N·m
Samples:   10000
```

**Notes:**

- Statistics over recent time window
- Useful for characterizing system behavior
- Min/max values help identify extremes
- Mean values show typical operation
- Reset when control restarts

### `perf_rms`

Display root-mean-square (RMS) values for position, velocity, and torque. RMS provides a measure of signal magnitude over time.

**Usage:**

```
perf_rms
```

**Example:**

```
> perf_rms
RMS Values:
Position:    23.4 deg
Velocity:    45.6 deg/s
Torque:      0.089 N·m
```

**Notes:**

- RMS better represents signal energy than mean
- Useful for power and frequency analysis
- Higher RMS = more active system
- Compare across configurations
- Computed over recent time window

### `perf_dump`

Export recorded performance data in CSV format for offline analysis.

**Usage:**

```
perf_dump
```

**Example:**

```
> perf_dump
time_ms,position_deg,velocity_deg_s,torque_nm
0,0.0,0.0,0.0
10,0.5,2.3,0.012
20,1.2,4.1,0.023
...
```

**Notes:**

- CSV format for easy import to Excel/Python/MATLAB
- Timestamp in milliseconds
- Position in degrees
- Velocity in degrees per second
- Torque in Newton-meters
- Buffer size limited (typical: 1000 samples)
- Data from most recent recording session

---

## Diagnostics Commands

### `fault_last`

Display information about the last hard fault (crash) that occurred, including register values for debugging.

**Usage:**

```
fault_last
```

**Example:**

```
> fault_last
Last Hard Fault:
Valid:     YES
PC:        0x08001234
LR:        0x08005678
R0:        0x12345678
...
CFSR:      0x00000001
```

**Notes:**

- Debugging tool for firmware developers
- Shows ARM Cortex-M7 fault registers
- PC (Program Counter) indicates fault location
- CFSR provides fault type details
- Information persists across resets
- "Valid: NO" means no fault recorded
- Helps identify firmware bugs

---

## Command Syntax Notes

### General Conventions

- Commands are case-sensitive (all lowercase)
- Arguments are separated by spaces
- String arguments with spaces are not currently supported
- Numeric arguments accept decimal format
- Press Enter to execute command
- Backspace to delete characters
- Commands echo as you type

### Error Handling

If a command fails, you'll see an error message:

```
> valve_damping xyz
Failed to set damping
Error: Invalid parameter
```

Common error causes:

- Missing required arguments
- Invalid numeric format
- System not in correct state
- Hardware communication failure

### Special Characters

- `>` - Command prompt (do not type this)
- `\r\n` - Line endings (automatic)
- No escape sequences needed

---

## Quick Reference Table

| Command Category | Key Commands                                             |
| ---------------- | -------------------------------------------------------- |
| Basic Control    | `valve_start`, `valve_stop`, `valve_status`              |
| ODrive Control   | `odrive_enable`, `odrive_disable`, `odrive_status`       |
| Configuration    | `valve_damping`, `valve_friction`, `valve_torquelimit`   |
| Presets          | `valve_preset`, `valve_preset_save`, `valve_preset_show` |
| Network          | `ethstatus`, `setip`, `http`, `eth_stream`               |
| Monitoring       | `perf_stats`, `perf_rms`, `perf_dump`                    |
| Diagnostics      | `can_status`, `valve_timing`, `fault_last`               |

---

## See Also

- [Getting Started Guide](../getting-started/getting-started.md) - Initial setup and first commands
- [Web Interface Guide](../html/web-interface-guide.md) - Browser-based control panel
- [REST API Reference](../rest/rest-api.md) - Remote control via HTTP
- [REST API Examples](../rest/rest-api-examples.md) - Integration code samples
- [Streaming Guide](../stream/streaming-guide.md) - Real-time TCP data streaming
- [Streaming Examples](../stream/streaming-examples.md) - Streaming integration code
- [PRISM Project Overview](../README.md) - Project introduction and architecture
- Firmware source: `firmware/src/app/cli.c`
