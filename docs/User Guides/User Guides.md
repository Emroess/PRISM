[⬅ Back to Build Guide](../Build%20Guide/Build%20Guide.md)

# PRISM User Guide

This guide details how to interact with the PRISM system, from basic Command-Line Interface (CLI) configuration to complex software control integrations via Python, MuJoCo, Gymnasium, and Isaac Sim.

## 1. Connecting to PRISM

### Physical Connection

1. Connect via USB to the **CN1 (ST-LINK)** port of the PRISM Nucleo board.
2. The board is bus-powered. Use a serial terminal like PuTTY, TeraTerm (Windows) or `screen` (Mac/Linux).
3. **Baud Rate**: `115200`

For more details / other connection options, see here: [docs/getting-started/getting-started.md#opening-a-serial-terminal](getting-started/getting-started.md#opening-a-serial-terminal)

### Basic CLI Operations

Once connected, you will see the `Type 'help' for available commands` message. Basic tasks:

1. **Verify Connection**: Type `help`.
2. **Ping Motor**: 

   ```bash
   STEVE> odrive_ping
   ```
3. **Run Calibration** (if necessary): `odrive_calibrate`
4. **Enable Control Loop**:

   ```bash
   STEVE> odrive_enable
   ```
5. **Start Haptic Valve Feedback**:

   ```bash
   STEVE> valve_start
   ```
6. **Stop Operations**: `valve_stop` and `odrive_disable`

A complete CLI Reference Guide can be found at [docs/cli/cli-reference.md](cli/cli-reference.md)

---

## 2. Haptic Parameter Tuning & Profiles

PRISM can emulate nearly any real-world rotary constraint using live parameter configurations.

### Key Adjustable Parameters

- **Viscous Damping** (`valve_damping 0.05`): Velocity-dependent resistance. Range 0.01 - 0.5.
- **Coulomb Friction** (`valve_friction 0.01`): Constant resistance to motion. Range 0.005 - 0.05.
- **Wall Stiffness** (`valve_wall_k 1.0`): End-stop spring constant. Range 0.5 - 5.0.
- **Wall Damping** (`valve_wall_c 0.1`): End-stop energy dissipation.
- **Torque Limit** (`valve_torquelimit 0.5`): Maximum output torque (safety boundary).

### Using Presets

You can save and load combinations of the above parameters into non-volatile memory or via the CLI:

```bash
STEVE> valve_preset smooth
STEVE> valve_preset_show
STEVE> valve_preset_save my_custom_valve
```

Several presets like `default`, `smooth`, `stiff`, and `heavy` are already built-in.

---

## 3. Advanced Software Integrations

PRISM can be heavily automated using Python (via "PySteve").

### **[Please refer to client/PyPRISM.md](../../client/PyPRISM.md)**

---

## 4. Alternative Control Interfaces

### Web Control Panel

If your PRISM unit is connected via Ethernet:

1. Find its IP using the serial CLI `ethstatus`.
2. Open `http://<device-ip>:8080` in any web browser.
3. Access real-time sparkline visualization, click to run presets, and tune control parameters without installing Python libraries.

For more details, please see the STEVE Web Interface Guide: [docs/html/web-interface-guide.md](html/web-interface-guide.md)

### Real-Time Network Data Streaming

You can stream hardware diagnostics (Torque, position, velocity) to up to 6 simultaneous clients using TCP.

```bash
STEVE> eth_stream start 50
```

Then, connect a TCP socket viewer or your raw python UDP stream handler to port `8888`.

For more details, please see the STEVE TCP Streaming Guide: [docs/stream/streaming-guide.md](stream/streaming-guide.md).
Streaming Examples can be found at [docs/stream/streaming-examples.md](stream/streaming-examples.md).

### Custom REST API

Direct JSON API for integration via local networks without using the PySteve framework:

```bash
curl -H "X-API-Key: steve-valve-2025" http://192.168.1.100:8080/api/v1/status
```

Full docs on the REST API endpoints can be found in [docs/rest/rest-api.md](rest/rest-api.md)

---

### Next: [Rotation Library](../Rotation%20Library/Rotation%20Library.md) ➡