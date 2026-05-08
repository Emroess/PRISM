<div align="center">
   <img src="../../assets/images/PRISMlogo.png" width="500" alt="PRISM Logo">

   <h3>Getting Started</h3>

   <p>Connect to your PRISM system over USB and run your first haptic session.</p>

   <a href="../../../README.md">
      <img src="https://img.shields.io/badge/⬅_Back_to_README-e34c26?style=for-the-badge" alt="Back to README">
   </a>
   &nbsp;
   <a href="CLI%20Reference.md">
      <img src="https://img.shields.io/badge/CLI_Reference-0969da?style=for-the-badge" alt="CLI Reference">
   </a>
</div>

---

> [!NOTE]
> This guide walks you from an unboxed PRISM unit to a running haptic session. For the full command list, see the [CLI Reference](CLI%20Reference.md).

---

## What You'll Need

### Hardware

| Item | Description |
|:-----|:------------|
| **PRISM Device** | STM32 Nucleo-H753ZI with firmware flashed |
| **USB Cable** | Standard Micro-USB cable |
| **Computer** | Windows, Mac, or Linux PC |

### Software

| Platform | Recommended Terminal |
|:---------|:---------------------|
| Windows | PuTTY, TeraTerm, or Windows Terminal |
| Mac / Linux | `screen`, `minicom`, or `picocom` |
| Cross-platform | Arduino IDE Serial Monitor |

### Optional

| Item | Purpose |
|:-----|:--------|
| Ethernet cable | Network features (Web GUI, REST API, streaming) |

---

## Connecting to PRISM

### Physical Connection

1. **Locate the ST-LINK USB port** on your Nucleo-H753ZI board
   - This is typically the USB connection farthest from the Ethernet port
   - Labeled "USB ST-LINK" or "CN1" on the board

   > [!TIP]
   > If the 3D-printed enclosure and USB extension is installed, this connection will be to the side of the AC power input cable.

2. **Connect the USB cable** between your computer and the ST-LINK port
3. **Power up the board**
   - The board is powered through the USB connection
   - You should see LED indicators light up (green = normal operation)
4. **Wait for driver installation** (first time only)
   - Windows: ST-LINK drivers install automatically
   - Mac / Linux: Usually works without additional drivers

---

### Identifying the Serial Port

<details>
<summary><strong>Windows</strong></summary>
<br>

1. Open **Device Manager** (`Win + X` → Device Manager)
2. Look under **Ports (COM & LPT)**
3. Find `STMicroelectronics STLink Virtual COM Port (COMx)`
4. Note the COM port number (e.g., `COM3`, `COM7`)

</details>

<details>
<summary><strong>Mac</strong></summary>
<br>

```bash
ls /dev/tty.usbmodem*
```

The device will appear as something like `/dev/tty.usbmodem1234`

</details>

<details>
<summary><strong>Linux</strong></summary>
<br>

```bash
ls /dev/ttyACM*
```

The device will appear as something like `/dev/ttyACM0`

</details>

---

## Opening a Serial Terminal

<details>
<summary><strong>Windows — PuTTY</strong></summary>
<br>

1. Download and install PuTTY from https://www.putty.org
2. Launch PuTTY
3. Select **Serial** as the connection type
4. Enter your COM port (e.g., `COM3`)
5. Set Speed to **115200**
6. Click **Open**

</details>

<details>
<summary><strong>Windows — TeraTerm</strong></summary>
<br>

1. Download and install TeraTerm
2. Launch TeraTerm
3. Select **File → New Connection**
4. Choose **Serial** and select your COM port
5. Set Speed to **115200**
6. Click **OK**

</details>

<details>
<summary><strong>Mac — screen</strong></summary>
<br>

```bash
screen /dev/tty.usbmodem1234 115200
```

Replace with your actual device name. To exit: `Ctrl+A`, then `K`, then `Y`.

</details>

<details>
<summary><strong>Linux — screen</strong></summary>
<br>

You may first need to add yourself to the `dialout` group:

```bash
sudo usermod -a -G dialout $USER
```

Then log out and back in. Connect with:

```bash
screen /dev/ttyACM0 115200
```

Replace with your actual device name. To exit: `Ctrl+A`, then `K`, then `Y`.

</details>

---

### Connection Parameters

> [!IMPORTANT]
> Always use these serial settings:

| Parameter | Value |
|:----------|:------|
| Baud rate | **115200** |
| Data bits | 8 |
| Parity | None |
| Stop bits | 1 |
| Flow control | None |

---

## First Commands

After connecting, you should see a welcome prompt:

```
Type 'help' for available commands
>
```

> [!TIP]
> If you don't see the `>` prompt, press **Enter**.

### Verify Connection

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

```
> valve_status

Valve Status:
Position:    0 counts (0.0 deg)
Velocity:    0 counts/s (0.0 deg/s)
Torque:      0.000 N·m
State:       IDLE
```

### Check Network Configuration

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

A typical startup sequence for running a haptic session:

| Step | Command | What It Does |
|:----:|:--------|:-------------|
| 1 | `odrive_ping` | Verify ODrive CAN connection |
| 2 | `odrive_calibrate` | Calibrate motor and encoder *(if needed)* |
| 3 | `odrive_enable` | Enable closed-loop motor control |
| 4 | `valve_preset 0` | Load a haptic preset *(optional)* |
| 5 | `valve_start` | Start the haptic control loop |
| 6 | `valve_status` | Monitor position, velocity, and torque |
| 7 | `valve_stop` | Stop the control loop |
| 8 | `odrive_disable` | Disable the motor |

> [!WARNING]
> The motor **will move** during `odrive_calibrate`. Ensure a clear range of motion before calibrating.

---

## Common Tasks

### Adjusting Haptic Feel

<details>
<summary>Make it smoother</summary>
<br>

```
> valve_damping 0.02
> valve_friction 0.005
```

</details>

<details>
<summary>Make it stiffer</summary>
<br>

```
> valve_damping 0.1
> valve_friction 0.02
```

</details>

<details>
<summary>Adjust wall boundaries</summary>
<br>

```
> valve_wall_k 2.0
> valve_wall_c 0.2
```

</details>

---

### Saving & Loading Presets

```
> valve_preset_save 0       # Save current configuration to slot 0
> valve_preset 0            # Load preset from slot 0
> valve_preset_show         # View all available presets
```

---

### Network Configuration

```
> setip 192.168.1.150 255.255.255.0 192.168.1.1    # Set a static IP
> http start                                         # Start the web server
```

Then open a browser to: `http://192.168.1.150:8080`

---

### Data Streaming

```
> eth_stream start 50       # Start streaming at 50 ms interval (port 8888)
> eth_stream stop            # Stop streaming
```

Connect with a client application to port `8888` to receive real-time telemetry.

---

## 🔧 Troubleshooting

<details>
<summary><strong>Can't connect to serial port</strong> — No response when typing commands</summary>
<br>

1. Verify USB cable is connected firmly
2. Check you have the correct COM port / device name
3. Confirm baud rate is **115200**
4. Try unplugging and replugging the USB cable
5. Close other programs that might be using the serial port
6. On Linux, ensure you're in the `dialout` group

</details>

<details>
<summary><strong>ODrive not responding</strong> — <code>odrive_ping</code> shows no response</summary>
<br>

1. Check CAN bus wiring (CAN_H, CAN_L, GND)
2. Verify CAN termination resistors (120 Ω on each end)
3. Ensure ODrive is powered on
4. Check ODrive node ID matches firmware configuration (typically `0`)
5. Verify CAN bus bitrate matches (typically 1 Mb/s)

</details>

<details>
<summary><strong>Valve control won't start</strong> — <code>valve_start</code> fails with error</summary>
<br>

1. Ensure ODrive is enabled first: `odrive_enable`
2. Clear any ODrive errors: `odrive_clear`
3. Check ODrive mode is correct: `odrive_mode 1` (torque control)
4. Verify no hard faults: `fault_last`
5. Check ODrive status: `odrive_status`

</details>

<details>
<summary><strong>Commands not working</strong> — Commands not recognized or producing errors</summary>
<br>

1. Check spelling — commands are **case-sensitive** (all lowercase)
2. Ensure proper number of arguments
3. Type `help` to see the correct command list
4. Press **Enter** after typing the command
5. Try power cycling the board

</details>

<details>
<summary><strong>Network not connecting</strong> — Can't access web interface or streaming</summary>
<br>

1. Check Ethernet cable is connected
2. Verify IP address with `ethstatus`
3. Ensure your computer is on the same subnet
4. Try pinging the device from your computer
5. Reconfigure IP if needed with `setip`
6. Check if HTTP server is started: `http status`

</details>

<details>
<summary><strong>Motor oscillating or unstable</strong> — Motor vibrates or behaves erratically</summary>
<br>

1. Reduce damping: `valve_damping 0.03`
2. Lower torque limit: `valve_torquelimit 0.3`
3. Check ODrive gains aren't too high
4. Ensure motor is properly mounted and not mechanically bound
5. Verify encoder is working: `can_encoder`

</details>

<details>
<summary><strong>Need to reset</strong> — System in bad state</summary>
<br>

1. Stop valve control: `valve_stop`
2. Disable ODrive: `odrive_disable`
3. Emergency stop if needed: `odrive_estop`
4. Clear errors: `odrive_clear`
5. Power cycle the entire system
6. Check for hard faults: `fault_last`

</details>

---

## ⌨️ Quick Command Cheatsheet

| Category | Commands |
|:---------|:---------|
| **Essential** | `help` · `valve_status` · `odrive_status` · `ethstatus` |
| **Operation** | `odrive_enable` · `valve_start` · `valve_stop` · `odrive_disable` |
| **Tuning** | `valve_damping` · `valve_friction` · `valve_torquelimit` · `valve_preset` |
| **Monitoring** | `valve_timing` · `can_telemetry` |
| **Network** | `setip` · `http start` · `eth_stream start` |

---

## Next Steps

| Goal | Resource |
|:-----|:---------|
| Learn all commands in detail | [CLI Reference](CLI%20Reference.md) |
| Control PRISM from a browser | [Web Interface Guide](../web/web-interface-guide.md) |
| Automate via HTTP | [REST API Reference](../rest/rest-api.md) |
| Stream real-time telemetry | [Streaming Guide](../stream/streaming-guide.md) |

---

<div align="center">
   <sub>
      <a href="../../../README.md">⬅ Back to Main README</a> · <a href="CLI%20Reference.md">CLI Reference →</a>
   </sub>
</div>
