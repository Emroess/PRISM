<div align="center">
   <img src="../../assets/images/PRISMlogo.png" width="500" alt="PRISM Logo">

   <h3>CLI Reference</h3>

   <p>Complete command reference for the PRISM command-line interface.</p>

   <a href="../../../README.md">
      <img src="https://img.shields.io/badge/⬅_Back_to_README-e34c26?style=for-the-badge" alt="Back to README">
   </a>
   &nbsp;
   <a href="Getting%20Started.md">
      <img src="https://img.shields.io/badge/Getting_Started-2ea44f?style=for-the-badge" alt="Getting Started">
   </a>
</div>

---

> [!NOTE]
> All commands are **case-sensitive** and **lowercase**. Arguments are separated by spaces. Press Enter to execute.
> For a walkthrough of connecting and first use, see [Getting Started](Getting%20Started.md).

---

## Table of Contents

| Section | Commands |
|:--------|:---------|
| [Valve Control](#valve-control) | `valve_start` · `valve_stop` · `valve_status` · `valve_energy` · `valve_timing` |
| [Valve Configuration](#valve-configuration) | `valve_damping` · `valve_friction` · `valve_epsilon` · `valve_torquelimit` · `valve_wall_k` · `valve_wall_c` · `valve_scale` |
| [Valve Presets](#valve-presets) | `valve_preset` · `valve_preset_save` · `valve_preset_show` |
| [ODrive Motor Control](#odrive-motor-control) | `odrive_ping` · `odrive_status` · `odrive_enable` · `odrive_disable` · `odrive_estop` · `odrive_clear` · `odrive_calibrate` · `odrive_torque` · `odrive_velocity` · `odrive_position` |
| [ODrive Configuration](#odrive-configuration) | `odrive_mode` · `odrive_limits` · `odrive_pos_gain` · `odrive_vel_gains` |
| [CAN Bus](#can-bus) | `can_encoder` · `can_telemetry` · `can_status` |
| [Network](#network) | `ethstatus` · `setip` · `ping` · `nvm_status` · `http` · `eth_stream` |
| [Performance Monitoring](#performance-monitoring) | `perf_stats` · `perf_rms` · `perf_dump` |
| [Diagnostics](#diagnostics) | `fault_last` |

---

## Valve Control

These commands control the valve's operational state and provide real-time status information.

| Command | Description |
|:--------|:------------|
| `valve_start` | Start the haptic control loop |
| `valve_stop` | Stop haptic feedback |
| `valve_status` | Show position, velocity, torque, and parameters |
| `valve_energy` | Display passivity energy tank level |
| `valve_timing` | Show control loop timing diagnostics |

<details>
<summary><code>valve_start</code></summary>
<br>

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

</details>

<details>
<summary><code>valve_stop</code></summary>
<br>

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

</details>

<details>
<summary><code>valve_status</code></summary>
<br>

Display comprehensive valve system status including current position, velocity, torque, and control parameters.

**Usage:**

```
valve_status
```

**Example:**

```
> valve_status
Valve Status:
State:    	 					IDLE​
Position:    					45.2 deg
Velocity:    					0.078 rad/s
Torque:      					0.123 N·m
Torque limit:					0.01 N·m·s
Viscous Damping:      0.050 N·m·s/rad
Coulomb Friction:    	0.010 N·m
Wall stiffness:				1.000 N·m/turn
Wall damping:      		0.000 N·m·s/turn
Scale: 								360.000 deg/turn
Epsilon: 							10.000000
Energy tank: 					-0.341437 J
Loop time: 						4 us
```

**Information Provided:**

- Current encoder position (raw counts and mechanical degrees)
- Current velocity (counts/s and mechanical degrees/s)
- Current applied torque
- Control system state (IDLE, RUNNING, ERROR)
- Active haptic parameters
- ...and more

</details>

<details>
<summary><code>valve_energy</code></summary>
<br>

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

</details>

<details>
<summary><code>valve_timing</code></summary>
<br>

Show detailed timing diagnostics for the control loop including execution time, cycle period, and timing violations.

**Usage:**

```
valve_timing
```

**Example:**

```
> valve_timing
Loop Timing Diagnostics:
Loop count:     		26105
Min loop time:      4 μs
Max loop time:      5 μs
Avg loop time:			4 μs
Timing samples:			35212
```

**Information Provided:**

- Target control loop frequency
- Average execution time per cycle
- Maximum execution time observed
- Number of timing overruns (late cycles)

---

</details>


---

## Valve Configuration

These commands adjust the haptic feedback characteristics and physical parameters of the valve simulation.

| Command | Parameter | Unit | Typical Range |
|:--------|:----------|:-----|:--------------|
| `valve_damping <val>` | Viscous damping | N·m·s/rad | 0.01 – 0.5 |
| `valve_friction <val>` | Coulomb friction | N·m | 0.005 – 0.05 |
| `valve_epsilon <val>` | Smoothing parameter | dimensionless | 0.0001 – 0.01 |
| `valve_torquelimit <val>` | Max torque (safety) | N·m | 0.1 – 2.0 |
| `valve_wall_k <val>` | Wall stiffness | N·m/turn | 0.5 – 5.0 |
| `valve_wall_c <val>` | Wall damping | N·m·s/turn | 0.05 – 0.5 |
| `valve_scale <val>` | Mechanical scale | deg/turn | hardware-dependent |

<details>
<summary><code>valve_damping</code></summary>
<br>

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

</details>

<details>
<summary><code>valve_friction</code></summary>
<br>

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

</details>

<details>
<summary><code>valve_epsilon</code></summary>
<br>

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

</details>

<details>
<summary><code>valve_torquelimit</code></summary>
<br>

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

</details>

<details>
<summary><code>valve_wall_k</code></summary>
<br>

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

</details>

<details>
<summary><code>valve_wall_c</code></summary>
<br>

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

</details>

<details>
<summary><code>valve_scale</code></summary>
<br>

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

</details>


---

## Valve Presets

Presets allow you to save and recall complete valve configurations for different applications or testing scenarios.

| Command | Description |
|:--------|:------------|
| `valve_preset <idx>` | Load a saved preset (0–3) |
| `valve_preset_save <idx>` | Save current config to a preset slot |
| `valve_preset_show` | Display all presets and their parameters |

<details>
<summary><code>valve_preset</code></summary>
<br>

Load a previously saved valve preset configuration.

**Usage:**

```
valve_preset <preset_idx>
```

**Parameters:**

- `preset_idx` - Index of the preset to load (0-3)

**Example:**

```
> valve_preset 0
Loaded preset: 0
```

**Available Presets:**

- `90-valve` - 90-degree limited travel valve feel
- `h-wrench` - Heavy wrench with high resistance
- `doorhandle` - Spring-return doorhandle feel
- `turnwheel` - Continuous turnwheel with low resistance

**Notes:**

- Instantly updates all valve parameters
- Changes take effect on next control cycle
- Does not require stopping the valve

</details>

<details>
<summary><code>valve_preset_save</code></summary>
<br>

Save the current valve configuration as a named preset.

**Usage:**

```
valve_preset_save <preset_idx>
```

**Parameters:**

- `preset_idx` - Index for the new preset (0-3)

**Example:**

```
> valve_preset_save 1
Preset saved: 1
```

**Notes:**

- Saves all current valve parameters
- Overwrites preset if name already exists
- Stored in non-volatile memory
- Survives power cycles

</details>

<details>
<summary><code>valve_preset_show</code></summary>
<br>

Display all available presets and their parameter values.

**Usage:**

```
valve_preset_show
```

**Example:**

```
> valve_preset_show
Available Presets:
Preset 0: 90-valve
  Torque limit:     8.000 Nm
  Default travel:   90.0 deg
  Viscous damping:  0.010 Nms/rad
  Coulomb friction: 0.800 Nm
  Wall stiffness:   100.000 Nm/turn
  Wall damping:     0.000 Nms/rad 
  Smoothing eps:    10.000000
  ...
Prset 1: h-wrench
  Torque limit:     10.000 Nm
  ...
```

**Notes:**

- Shows all parameters for each preset
- Helps compare different configurations
- Useful for documentation and version control

---

</details>


---

## ODrive Motor Control

These commands control the ODrive motor controller that drives PRISM's haptic feedback motor.

| Command | Description |
|:--------|:------------|
| `odrive_ping` | Test CAN connectivity |
| `odrive_status` | Show comprehensive ODrive status |
| `odrive_enable` | Enable closed-loop control |
| `odrive_disable` | Disable motor (coast) |
| `odrive_estop` | Emergency stop |
| `odrive_clear` | Clear active errors |
| `odrive_calibrate` | Run motor + encoder calibration |
| `odrive_torque <val>` | Direct torque command (N·m) |
| `odrive_velocity <val>` | Direct velocity command (turns/s) |
| `odrive_position <val>` | Direct position command (turns) |

<details>
<summary><code>odrive_ping</code></summary>
<br>

> [!WARNING]
> TODO: does not work in CLI (returns: Unknown command: odrive_ping) (Also shows up in the `help` call)

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

</details>

<details>
<summary><code>odrive_status</code></summary>
<br>

Display comprehensive ODrive status including state, errors, position, velocity, and current.

**Usage:**

```
odrive_status
```

**Example:**

```
> odrive_status
ODrive Status:
Axis error:         0x00000000
Axis state:         0x01
Motor flags:        0x00
Encoder flags:      0x00
Controller status:  0x00
```

**Information Provided:**

- Current operational state
- Encoder position and velocity
- Applied torque/current
- Power supply voltage and current
- Any active errors or warnings

</details>

<details>
<summary><code>odrive_enable</code></summary>
<br>

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

</details>

<details>
<summary><code>odrive_disable</code></summary>
<br>

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

</details>

<details>
<summary><code>odrive_estop</code></summary>
<br>

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

</details>

<details>
<summary><code>odrive_clear</code></summary>
<br>

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

</details>

<details>
<summary><code>odrive_calibrate</code></summary>
<br>

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

</details>

<details>
<summary><code>odrive_torque</code></summary>
<br>

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

</details>

<details>
<summary><code>odrive_velocity</code></summary>
<br>

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

</details>

<details>
<summary><code>odrive_position</code></summary>
<br>

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

</details>


---

## ODrive Configuration

These commands configure the ODrive's control parameters and operational limits.

| Command | Description |
|:--------|:------------|
| `odrive_mode <mode>` | Set control mode (0–3) |
| `odrive_limits <vel> <cur>` | Set velocity and current limits |
| `odrive_pos_gain <kp>` | Set position Kp |
| `odrive_vel_gains <kp> <ki>` | Set velocity Kp and Ki |

<details>
<summary><code>odrive_mode</code></summary>
<br>

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

</details>

<details>
<summary><code>odrive_limits</code></summary>
<br>

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

</details>

<details>
<summary><code>odrive_pos_gain</code></summary>
<br>

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

</details>

<details>
<summary><code>odrive_vel_gains</code></summary>
<br>

> [!WARNING]
> TODO: command results in error: Unknown command: odrive_vel_gains. odrive_vel_gain also doesn't exist. (Both show up in the `help` call though)

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

</details>


---

## CAN Bus

Diagnostics and monitoring for CAN bus communication with the ODrive.

| Command | Description |
|:--------|:------------|
| `can_encoder` | Read encoder position and velocity |
| `can_telemetry` | Read voltage, current, and temperatures |
| `can_status` | Show CAN bus statistics |

<details>
<summary><code>can_encoder</code></summary>
<br>

Read the current encoder position and velocity from the ODrive over CAN.

**Usage:**

```
can_encoder
```

**Example:**

```
> can_encoder
Encoder feedback:
Position: 1234.56 turns
Velocity: 78.90 turns/s
```

**Notes:**

- Raw encoder data from ODrive
- Updates continuously in real-time
- Useful for verifying encoder operation
- Compare with valve_status for consistency

</details>

<details>
<summary><code>can_telemetry</code></summary>
<br>

> [!WARNING]
> TODO: this command works, but gives weird response: "Failed to read Telemetry" \n "Error: Buffer empty"

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

</details>

<details>
<summary><code>can_status</code></summary>
<br>

Display CAN bus communication statistics and status.

**Usage:**

```
can_status
```

**Example:**

```
> can_status
CAN Bus Status:
TX count:         0
RX count:         0
Error count:      0
Last error code:  0x000000
```

**Information Provided:**

- CAN bitrate configuration
- Message transmit/receive counts
- Error counters
- Bus operational state
- Node ID configuration

---

</details>


---

## Network

Configure and monitor the Ethernet network interface.

| Command | Description |
|:--------|:------------|
| `ethstatus` | Show IP configuration and link status |
| `setip <ip> <mask> <gw>` | Set static IP address |
| `ping <ip>` | Test network connectivity |
| `nvm_status` | Show stored network config from flash |
| `http start\|stop\|status\|log` | Control the HTTP web server |
| `eth_stream start [ms]\|stop` | Control real-time TCP data streaming |

<details>
<summary><code>ethstatus</code></summary>
<br>

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

</details>

<details>
<summary><code>setip</code></summary>
<br>

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

</details>

<details>
<summary><code>ping</code></summary>
<br>

> [!WARNING]
> TODO: does not return any results, i.e. just says "Ping initiated" but doesn't give info like regular terminals do.

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

</details>

<details>
<summary><code>nvm_status</code></summary>
<br>

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
Netmask: 255.255.255.0
Gateway: 10.0.0.1

Loaded Config:
Magic: 0x4E455457 (expected 0x4E455457)
...
```

**Notes:**

- Diagnostic tool for network configuration storage
- Shows raw flash data and validation
- Helps troubleshoot boot configuration issues
- Magic number indicates valid configuration
- Checksum verifies data integrity

</details>

<details>
<summary><code>http</code></summary>
<br>

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

Note: After `stop`, you can do `http start` until you have closed the HTTP webpage

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

</details>

<details>
<summary><code>eth_stream</code></summary>
<br>

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

</details>


---

## Performance Monitoring

Detailed performance metrics and data logging.

> [!WARNING]
> TODO: `perf_stats`, `perf_rms`, and `perf_dump` do not exist in the CLI — "Unknown command". They also don't appear in the `help` output.

| Command | Description |
|:--------|:------------|
| `perf_stats` | Min/max/mean statistics |
| `perf_rms` | RMS values for position, velocity, torque |
| `perf_dump` | Export CSV data for offline analysis |

<details>
<summary><code>perf_stats</code></summary>
<br>

> [!WARNING]
> TODO: doesn't exist in the CLI: "Unknown command: perf_stats". Also doesn't exist when doing `help`

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

</details>

<details>
<summary><code>perf_rms</code></summary>
<br>

> [!WARNING]
> TODO: same as `perf_stats`

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

</details>

<details>
<summary><code>perf_dump</code></summary>
<br>

> [!WARNING]
> TODO: same as `perf_stats`

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

</details>


---

## Diagnostics

<details>
<summary><code>fault_last</code></summary>
<br>

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

</details>


---

## Command Syntax Notes

### General Conventions

- Commands are **case-sensitive** (all lowercase)
- Arguments separated by spaces
- String arguments with spaces are not supported
- Numeric arguments accept decimal format
- Backspace to delete characters; commands echo as you type

### Error Handling

```
> valve_damping xyz
Failed to set damping
Error: Invalid parameter
```

| Common Cause | Fix |
|:-------------|:----|
| Missing required arguments | Check command syntax above |
| Invalid numeric format | Use decimal numbers (e.g., `0.05`) |
| System not in correct state | Enable ODrive, set correct mode |
| Hardware communication failure | Check CAN wiring, power |

---

## Quick Reference

> [!WARNING]
> TODO: clear out the Monitoring row (since `perf_*` don't exist). Also clear out other commands confirmed to not work.

| Category | Key Commands |
|:---------|:-------------|
| **Basic Control** | `valve_start` · `valve_stop` · `valve_status` |
| **ODrive Control** | `odrive_enable` · `odrive_disable` · `odrive_status` |
| **Configuration** | `valve_damping` · `valve_friction` · `valve_torquelimit` |
| **Presets** | `valve_preset` · `valve_preset_save` · `valve_preset_show` |
| **Network** | `ethstatus` · `setip` · `http` · `eth_stream` |
| **Monitoring** | `perf_stats` · `perf_rms` · `perf_dump` |
| **Diagnostics** | `can_status` · `valve_timing` · `fault_last` |

---

## See Also

| Resource | Link |
|:---------|:-----|
| Getting Started | [Getting Started](Getting%20Started.md) |
| Web Interface Guide | [Web GUI](../web/web-interface-guide.md) |
| REST API Reference | [REST API](../rest/rest-api.md) |
| REST API Examples | [REST Examples](../rest/rest-api-examples.md) |
| Streaming Guide | [Streaming](../stream/streaming-guide.md) |
| Streaming Examples | [Streaming Examples](../stream/streaming-examples.md) |
| Firmware Source | `firmware/src/app/cli.c` |

---

<div align="center">
   <sub>
      <a href="../../../README.md">Back to Main README</a> · <a href="Getting%20Started.md">Getting Started</a>
   </sub>
</div>
