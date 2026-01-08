# Getting Started with STEVE

Welcome! This guide will help you connect to your STEVE (Simulated Task Exploration | Valve Emulation) system and start using the command-line interface over USB.

## Table of Contents

- [What You'll Need](#what-youll-need)
- [Connecting to STEVE](#connecting-to-steve)
- [Opening a Serial Terminal](#opening-a-serial-terminal)
- [First Commands](#first-commands)
- [Basic Operation Workflow](#basic-operation-workflow)
- [Common Tasks](#common-tasks)
- [Troubleshooting](#troubleshooting)

---

## What You'll Need

### Hardware
- **STEVE Device** - STM32 Nucleo-H753ZI development board with firmware
- **USB Cable** - Standard Micro-USB cable
- **Computer** - Windows, Mac, or Linux PC

### Software
- **Serial Terminal** - One of the following:
  - Windows: PuTTY, TeraTerm, or Windows Terminal
  - Mac/Linux: screen, minicom, or picocom
  - Cross-platform: Arduino IDE Serial Monitor

### Optional
- **Ethernet Cable** - For network features (web interface, streaming)

---

## Connecting to STEVE

### Physical Connection

1. **Locate the ST-LINK USB port** on your Nucleo-H753ZI board
   - This is typically the USB connection farthest fromthe Ethernet port.
     
        > **Note:** If the 3D printed enclosure and USB extention is installed, this connection will be to the side of the AC power input cable. 
   - Labeled "USB ST-LINK" or "CN1" on the board

2. **Connect the USB cable** between your computer and the ST-LINK port

3. **Power up the board**
   - The board is powered through the USB connection
   - You should see LED indicators light up
   - Green LED typically indicates normal operation

4. **Wait for driver installation** (first time only)
   - Windows: ST-LINK drivers install automatically
   - Mac/Linux: Usually works without additional drivers

### Identifying the Serial Port

**Windows:**
- Open Device Manager (Win+X, then Device Manager)
- Look under "Ports (COM & LPT)"
- Find "STMicroelectronics STLink Virtual COM Port (COMx)"
- Note the COM port number (e.g., COM3, COM7)

**Mac:**
- Open Terminal
- Run: `ls /dev/tty.usbmodem*`
- The device will be something like `/dev/tty.usbmodem1234`

**Linux:**
- Open Terminal
- Run: `ls /dev/ttyACM*`
- The device will be something like `/dev/ttyACM0`

---

## Opening a Serial Terminal

### Windows (PuTTY)

1. Download and install PuTTY from https://www.putty.org
2. Launch PuTTY
3. Select "Serial" as the connection type
4. Enter your COM port (e.g., COM3)
5. Set Speed to: **115200**
6. Click "Open"

### Windows (TeraTerm)

1. Download and install TeraTerm
2. Launch TeraTerm
3. Select File > New Connection
4. Choose "Serial" and select your COM port
5. Set Speed to: **115200**
6. Click "OK"

### Mac (screen)

1. Open Terminal
2. Run: `screen /dev/tty.usbmodem1234 115200`
   - Replace with your actual device name
3. To exit screen: Press Ctrl+A, then K, then Y

### Linux (screen)

1. Open Terminal
2. You may need to add yourself to the dialout group:
   ```bash
   sudo usermod -a -G dialout $USER
   ```
   Then log out and back in

3. Run: `screen /dev/ttyACM0 115200`
   - Replace with your actual device name
4. To exit screen: Press Ctrl+A, then K, then Y

### Connection Parameters

Always use these settings:
- **Baud rate:** 115200
- **Data bits:** 8
- **Parity:** None
- **Stop bits:** 1
- **Flow control:** None

---

## First Commands

### Verify Connection

After connecting, you should see a welcome message:
```
Type 'help' for available commands
> 
```

The `>` symbol is the command prompt. If you don't see it, press Enter.

### Get Help

Type `help` and press Enter to see all available commands:

```
> help

Available commands:
  can_encoder - Read CAN encoder position and velocity
  can_status - Show CAN bus status
  can_telemetry - Read CAN bus voltage, current, and temperatures
  eth_stream - Start/stop Ethernet data streaming
  ethstatus - Show Ethernet status and configuration
  help - Show this help
  odrive_enable - Enable ODrive closed loop control
  valve_start - Start valve control
  valve_status - Show valve status
  ...
```

### Check System Status

Get basic status information:

```
> valve_status

Valve Status:
Position:    0 counts (0.0 deg)
Velocity:    0 counts/s (0.0 deg/s)
Torque:      0.000 N·m
State:       IDLE
```

### Check Network Configuration

If using network features:

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

---

## Basic Operation Workflow

Here's a typical sequence for starting up the valve control system:

### 1. Check ODrive Connection

```
> odrive_ping

ODrive heartbeat received
Node ID: 0
Axis state: IDLE
```

### 2. Calibrate ODrive (if needed)

```
> odrive_calibrate

ODrive calibration started
Note: Motor will move during calibration
```

Wait for calibration to complete (a few seconds).

### 3. Enable ODrive (Only use if Manual Odrive Control is Neeeded)

```
> odrive_enable

ODrive enabled
```

### 4. Load a Preset (optional)

```
> valve_preset default

Loaded preset: default
```

### 5. Start Valve Control

```
> valve_start

Valve started
```

### 6. Monitor Status

```
> valve_status

Valve Status:
Position:    1234 counts (45.2 deg)
Velocity:    567 counts/s (20.8 deg/s)
Torque:      0.123 N·m
State:       RUNNING
```

### 7. Stop When Finished

```
> valve_stop

Valve stopped

> odrive_disable

ODrive disabled
```

---

## Common Tasks

### Adjusting Feel

Change the haptic characteristics in real-time:

**Make it smoother:**
```
> valve_damping 0.02
> valve_friction 0.005
```

**Make it stiffer:**
```
> valve_damping 0.1
> valve_friction 0.02
```

**Adjust wall boundaries:**
```
> valve_wall_k 2.0
> valve_wall_c 0.2
```

### Saving Your Configuration

Once you have settings you like:

```
> valve_preset_save myconfig

Preset saved: myconfig
```

Load it later:
```
> valve_preset myconfig

Loaded preset: myconfig
```

### Viewing All Presets

See what configurations are available:

```
> valve_preset_show

Available Presets:
[default]
  damping: 0.050
  friction: 0.010
  wall_k: 1.000
  wall_c: 0.100
[myconfig]
  damping: 0.020
  friction: 0.005
  ...
```

### Network Configuration

Set a static IP address:

```
> setip 192.168.1.150 255.255.255.0 192.168.1.1

IP configuration updated
```

Start the web interface:

```
> http start

HTTP server started (port 8080)
```

Then open a browser to: `http://192.168.1.150:8080`

### Data Logging

Start streaming data for analysis:

```
> eth_stream start 50

Ethernet streaming started (port 8888)
```

Connect with a client application to port 8888 to receive real-time data.

Or export recorded data:

```
> perf_dump

time_ms,position_deg,velocity_deg_s,torque_nm
0,0.0,0.0,0.0
10,0.5,2.3,0.012
...
```

Copy and paste this data into Excel or your analysis tool.

---

## Troubleshooting

### Can't Connect to Serial Port

**Problem:** No response when typing commands

**Solutions:**
1. Verify USB cable is connected firmly
2. Check you have the correct COM port / device name
3. Confirm baud rate is 115200
4. Try unplugging and replugging the USB cable
5. Close other programs that might be using the serial port
6. On Linux, ensure you're in the dialout group

### ODrive Not Responding

**Problem:** `odrive_ping` shows no response

**Solutions:**
1. Check CAN bus wiring (CAN_H, CAN_L, GND)
2. Verify CAN termination resistors (120Ω on each end)
3. Ensure ODrive is powered on
4. Check ODrive node ID matches firmware configuration (typically 0)
5. Verify CAN bus bitrate matches (typically 1 Mbps)

### Valve Control Won't Start

**Problem:** `valve_start` fails with error

**Solutions:**
1. Ensure ODrive is enabled first: `odrive_enable`
2. Clear any ODrive errors: `odrive_clear`
3. Check ODrive mode is correct: `odrive_mode 1` (torque control)
4. Verify no hard faults: `fault_last`
5. Check ODrive status: `odrive_status`

### Commands Not Working

**Problem:** Commands not recognized or producing errors

**Solutions:**
1. Check spelling (commands are case-sensitive, all lowercase)
2. Ensure proper number of arguments
3. Type `help` to see correct command list
4. Press Enter after typing command
5. Try power cycling the board

### Network Not Connecting

**Problem:** Can't access web interface or streaming

**Solutions:**
1. Check Ethernet cable is connected
2. Verify IP address with `ethstatus`
3. Ensure computer is on same subnet
4. Try pinging the device from your computer
5. Reconfigure IP if needed with `setip`
6. Check if HTTP server is started: `http status`

### Motor Oscillating or Unstable

**Problem:** Motor vibrates or behaves erratically

**Solutions:**
1. Reduce damping: `valve_damping 0.03`
2. Lower torque limit: `valve_torquelimit 0.3`
3. Check ODrive gains aren't too high
4. Ensure motor is properly mounted and not mechanically bound
5. Verify encoder is working: `can_encoder`

### Need to Reset

**Problem:** System in bad state

**Solutions:**
1. Stop valve control: `valve_stop`
2. Disable ODrive: `odrive_disable`
3. Emergency stop if needed: `odrive_estop`
4. Clear errors: `odrive_clear`
5. Power cycle the entire system
6. Check for hard faults: `fault_last`

---

## Next Steps

Now that you're connected and familiar with basic commands:

1. **Explore presets** - Try different configurations to understand the effects
2. **Read the CLI Reference** - Learn all commands in detail ([CLI Reference](cli-reference.md))
3. **Set up networking** - Enable remote monitoring and control
4. **Experiment with parameters** - Understand how each setting affects the feel
5. **Log data** - Collect data for analysis and characterization

For detailed information on all commands, see the [CLI Reference Guide](../cli/cli-reference.md).

For project overview and architecture, see the [STEVE Project Overview](../README.md).

For remote control and automation, see the [REST API Reference](../rest/rest-api.md).

---

## Quick Command Cheatsheet

### Essential Commands
```
help                    # Show all commands
valve_status            # Check current status
odrive_status          # Check motor controller
ethstatus              # Check network status
```

### Basic Operation
```
odrive_enable          # Enable motor
valve_start            # Start control
valve_stop             # Stop control
odrive_disable         # Disable motor
```

### Configuration
```
valve_damping 0.05     # Set damping
valve_friction 0.01    # Set friction
valve_torquelimit 0.5  # Set safety limit
valve_preset default   # Load preset
```

### Monitoring
```
valve_timing           # Check loop performance
perf_stats             # View statistics
can_telemetry          # Check power/temp
```

### Network
```
setip 192.168.1.100 255.255.255.0 192.168.1.1  # Configure network
http start             # Start web server
eth_stream start       # Start data streaming
```

Happy exploring with STEVE!
