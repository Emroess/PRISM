<div align="center">
   <img src="../../assets/images/PRISMlogo.png" width="500" alt="PRISM Logo">

   <h3>REST API Reference</h3>

   <p>Complete reference for the PRISM HTTP API — remote control, configuration, and monitoring.</p>

   <a href="../../../README.md">
      <img src="https://img.shields.io/badge/⬅_Back_to_README-e34c26?style=for-the-badge" alt="Back to README">
   </a>
   &nbsp;
   <a href="rest-api-examples.md">
      <img src="https://img.shields.io/badge/API_Examples-0969da?style=for-the-badge" alt="API Examples">
   </a>
</div>

---

> [!NOTE]
> The PRISM REST API provides programmatic access to all valve control, configuration, and monitoring functions over HTTP/JSON.

## Table of Contents

| Section | Description |
|:--------|:------------|
| [Overview](#overview) | Design goals and key features |
| [Authentication](#authentication) | API key setup and usage |
| [Base URL and Port](#base-url-and-port) | Connection details |
| [Response Format](#response-format) | JSON response structure |
| [Error Handling](#error-handling) | Status codes and error messages |
| [API Endpoints](#api-endpoints) | Config, Control, Presets, ODrive, CAN, Perf, Streaming |
| [Complete Endpoint Reference](#complete-endpoint-reference) | Quick-reference table |

---

## Overview

| Use Case | Description |
|:---------|:------------|
| **Robotics Integration** | Control PRISM from your robot control system |
| **Automated Testing** | Script test sequences and data collection |
| **Remote Monitoring** | Monitor valve status and performance over network |
| **Custom Applications** | Build custom interfaces and control logic |
| **Research Tools** | Integrate with data analysis pipelines |

### Key Features

| Feature | Detail |
|:--------|:-------|
| JSON-based | All requests and responses use JSON format |
| RESTful design | Standard HTTP methods (GET, POST) |
| Lightweight | Minimal overhead for embedded system |
| Real-time | Low-latency updates (sub-100ms typical) |
| Secure | API key authentication required |

---

## Authentication

All API requests require authentication using an API key passed in the HTTP header.

### X-API-Key Header

Every request must include the `X-API-Key` header:

```
X-API-Key: steve-valve-2025
```

### Default API Key

> [!IMPORTANT]
> The default API key is: `steve-valve-2025`

This key is defined at compile time in the firmware Makefile and can be changed by modifying:

```makefile
-DHTTP_API_KEY=\"your-custom-key\"
```

### Unauthorized Response

If the API key is missing or incorrect, the server responds with HTTP 401:

```http
HTTP/1.1 401 Unauthorized
WWW-Authenticate: X-API-Key
Content-Type: application/json

{
  "status": "error",
  "error": "unauthorized"
}
```

### Example Authenticated Request

```bash
curl -H "X-API-Key: steve-valve-2025" \
     http://192.168.1.100:8080/api/v1/status
```

---

## Base URL and Port

### Default Configuration

| Setting | Value |
|:--------|:------|
| Protocol | HTTP |
| Port | `8080` |
| Base Path | `/api/v1` |

### Full Base URL

```
http://<device-ip>:8080/api/v1
```

Replace `<device-ip>` with your PRISM device's IP address (e.g., `192.168.1.100`).

### Example Endpoints

- Configuration: `http://192.168.1.100:8080/api/v1/config`
- Status: `http://192.168.1.100:8080/api/v1/status`
- Control: `http://192.168.1.100:8080/api/v1/control`

---

## Response Format

All API responses use JSON format with standard status indicators.

### Success Response

```json
{
  "status": "ok",
  "data": { ... }
}
```

Or for simple operations:

```json
{
  "status": "ok"
}
```

### Data Response

Most GET requests return data directly without a wrapper:

```json
{
  "field1": "value1",
  "field2": 123.45,
  "field3": true
}
```

---

## Error Handling

### Error Response Format

```json
{
  "status": "error",
  "error": "error_code_or_message"
}
```

### HTTP Status Codes

| Code | Meaning               | Common Causes                    |
| ---- | --------------------- | -------------------------------- |
| 200  | OK                    | Request succeeded                |
| 400  | Bad Request           | Invalid JSON, missing parameters |
| 401  | Unauthorized          | Missing or invalid API key       |
| 404  | Not Found             | Invalid endpoint                 |
| 500  | Internal Server Error | Firmware error, hardware issue   |
| 503  | Service Unavailable   | System not initialized           |

### Common Error Messages

| Error Code            | Description                            |
| --------------------- | -------------------------------------- |
| `invalid_request`     | Malformed request body                 |
| `json_parse_error`    | Invalid JSON syntax                    |
| `missing_action`      | Required action parameter not provided |
| `invalid_action`      | Unsupported action value               |
| `valve_uninitialized` | Valve system not ready                 |
| `odrive_unavailable`  | ODrive not connected                   |
| `unauthorized`        | Authentication failed                  |

---

## API Endpoints

### Status and Configuration

#### GET /api/v1/config

Get current valve configuration parameters.

**Request:**

```http
GET /api/v1/config HTTP/1.1
Host: 192.168.1.100:8080
X-API-Key: steve-valve-2025
```

**Response:**

```json
{
  "viscous": 0.0500,
  "coulomb": 0.0100,
  "wall_stiffness": 1.0000,
  "wall_damping": 0.1000,
  "smoothing": 0.001000,
  "torque_limit": 0.5000,
  "open_position": 90.0000,
  "closed_position": 0.0000,
  "degrees_per_turn": 360.0000
}
```

**Response Fields:**

| Field              | Type  | Unit       | Description                  |
| ------------------ | ----- | ---------- | ---------------------------- |
| `viscous`          | float | N·m·s/rad  | Viscous damping coefficient  |
| `coulomb`          | float | N·m        | Coulomb friction torque      |
| `wall_stiffness`   | float | N·m/turn   | Virtual wall spring constant |
| `wall_damping`     | float | N·m·s/turn | Virtual wall damping         |
| `smoothing`        | float | -          | Friction smoothing epsilon   |
| `torque_limit`     | float | N·m        | Maximum output torque        |
| `open_position`    | float | degrees    | Fully open position          |
| `closed_position`  | float | degrees    | Fully closed position        |
| `degrees_per_turn` | float | deg/turn   | Mechanical scaling           |

---

#### POST /api/v1/config

Update valve configuration parameters.

**Request:**

```http
POST /api/v1/config HTTP/1.1
Host: 192.168.1.100:8080
X-API-Key: steve-valve-2025
Content-Type: application/json

{
  "viscous": 0.08,
  "coulomb": 0.015,
  "wall_stiffness": 2.0,
  "wall_damping": 0.15
}
```

**Request Body** (all fields optional):

| Field              | Type  | Range         | Description        |
| ------------------ | ----- | ------------- | ------------------ |
| `viscous`          | float | 0.01 - 0.5    | Viscous damping    |
| `coulomb`          | float | 0.005 - 0.05  | Coulomb friction   |
| `wall_stiffness`   | float | 0.5 - 5.0     | Wall stiffness     |
| `wall_damping`     | float | 0.05 - 0.5    | Wall damping       |
| `smoothing`        | float | 0.0001 - 0.01 | Smoothing epsilon  |
| `torque_limit`     | float | 0.1 - 2.0     | Torque limit       |
| `open_position`    | float | 0 - 360       | Open position      |
| `closed_position`  | float | 0 - 360       | Closed position    |
| `degrees_per_turn` | float | > 0           | Mechanical scaling |

**Response:**

```json
{
  "status": "ok",
  "fields_updated": 4
}
```

**Notes:**

- Only include fields you want to update
- Changes take effect immediately
- Does not require stopping the valve

---

#### GET /api/v1/status

Get real-time valve status and measurements.

**Request:**

```http
GET /api/v1/status HTTP/1.1
Host: 192.168.1.100:8080
X-API-Key: steve-valve-2025
```

**Response:**

```json
{
  "mode": "running",
  "pos_deg": 45.234567,
  "vel_rad_s": 0.123456,
  "torque_nm": 0.087654,
  "energy_j": 0.000123,
  "loop_hz": 1000,
  "pos_raw": 2048,
  "vel_raw": 123
}
```

**Response Fields:**

| Field       | Type    | Unit     | Description            |
| ----------- | ------- | -------- | ---------------------- |
| `mode`      | string  | -        | "idle" or "running"    |
| `pos_deg`   | float   | degrees  | Current position       |
| `vel_rad_s` | float   | rad/s    | Current velocity       |
| `torque_nm` | float   | N·m      | Applied torque         |
| `energy_j`  | float   | Joules   | Passivity energy tank  |
| `loop_hz`   | integer | Hz       | Control loop frequency |
| `pos_raw`   | integer | counts   | Raw encoder position   |
| `vel_raw`   | integer | counts/s | Raw encoder velocity   |

**Update Rate:**

- Poll up to 10 Hz for smooth UI updates
- Lower rates (1-5 Hz) sufficient for monitoring

---

### Control

#### POST /api/v1/control

Start or stop valve control, optionally load a preset.

**Request - Start:**

```http
POST /api/v1/control HTTP/1.1
Host: 192.168.1.100:8080
X-API-Key: steve-valve-2025
Content-Type: application/json

{
  "action": "start"
}
```

**Request - Stop:**

```json
{
  "action": "stop"
}
```

**Request - Start with Preset:**

```json
{
  "action": "start",
  "preset": 0
}
```

**Request - Apply Preset (no start/stop):**

```json
{
  "preset": 1
}
```

**Request Body:**

| Field    | Type    | Required | Description        |
| -------- | ------- | -------- | ------------------ |
| `action` | string  | No       | "start" or "stop"  |
| `preset` | integer | No       | Preset index (0-3) |

**Available Presets:**

| Index | Feel |
|:-----:|:-----|
| `0` | Light (butterfly/faucet) |
| `1` | Medium (ball valve) |
| `2` | Heavy (gate valve) |
| `3` | Industrial (globe/gas) |

**Response:**

```json
{
  "status": "ok",
  "mode": "running",
  "preset_applied": true
}
```

**Response Fields:**

| Field            | Type    | Description                       |
| ---------------- | ------- | --------------------------------- |
| `status`         | string  | "ok" or "error"                   |
| `mode`           | string  | Current mode: "running" or "idle" |
| `preset_applied` | boolean | Whether a preset was applied      |

**Workflow:**

1. Ensure ODrive is enabled (`POST /api/v1/odrive` with `action: enable`)
2. Optionally load configuration or preset
3. Start valve control with `action: start`
4. Monitor with `GET /api/v1/status`
5. Stop with `action: stop` when finished

---

### Presets

#### GET /api/v1/presets

Get all available presets and their parameters.

**Request:**

```http
GET /api/v1/presets HTTP/1.1
Host: 192.168.1.100:8080
X-API-Key: steve-valve-2025
```

**Response:**

```json
[
  {
    "index": 0,
    "name": "default",
    "viscous": 0.0500,
    "coulomb": 0.0100,
    "wall_stiffness": 1.00,
    "wall_damping": 0.1000,
    "travel": 90.0,
    "torque_limit": 0.50,
    "smoothing": 0.001000
  },
  {
    "index": 1,
    "name": "smooth",
    "viscous": 0.0200,
    "coulomb": 0.0050,
    "wall_stiffness": 0.50,
    "wall_damping": 0.0500,
    "travel": 90.0,
    "torque_limit": 0.50,
    "smoothing": 0.001000
  },
  {
    "index": 2,
    "name": "stiff",
    "viscous": 0.0800,
    "coulomb": 0.0200,
    "wall_stiffness": 3.00,
    "wall_damping": 0.2000,
    "travel": 90.0,
    "torque_limit": 0.50,
    "smoothing": 0.001000
  },
  {
    "index": 3,
    "name": "heavy",
    "viscous": 0.1500,
    "coulomb": 0.0300,
    "wall_stiffness": 1.50,
    "wall_damping": 0.3000,
    "travel": 90.0,
    "torque_limit": 0.50,
    "smoothing": 0.001000
  }
]
```

**Notes:**

- Returns array of all 4 preset slots
- Use `index` or `name` to reference presets
- Units same as `/api/v1/config`

---

#### POST /api/v1/presets

Save or update a preset configuration.

**Request - Save Custom Values:**

```http
POST /api/v1/presets HTTP/1.1
Host: 192.168.1.100:8080
X-API-Key: steve-valve-2025
Content-Type: application/json

{
  "index": 0,
  "name": "myconfig",
  "viscous": 0.06,
  "coulomb": 0.012,
  "wall_stiffness": 1.5,
  "wall_damping": 0.12,
  "travel": 90.0,
  "torque_limit": 0.5,
  "smoothing": 0.001
}
```

**Request - Save Current Configuration:**

```json
{
  "index": 0,
  "save_current": true
}
```

**Request Body:**

| Field            | Type    | Required         | Description                  |
| ---------------- | ------- | ---------------- | ---------------------------- |
| `index`          | integer | Yes              | Preset slot (0-3)            |
| `save_current`   | boolean | No               | Save active config to preset |
| `name`           | string  | If !save_current | Preset name (max 15 chars)   |
| `viscous`        | float   | If !save_current | Viscous damping              |
| `coulomb`        | float   | If !save_current | Coulomb friction             |
| `wall_stiffness` | float   | If !save_current | Wall stiffness               |
| `wall_damping`   | float   | If !save_current | Wall damping                 |
| `travel`         | float   | If !save_current | Travel range (degrees)       |
| `torque_limit`   | float   | If !save_current | Torque limit                 |
| `smoothing`      | float   | If !save_current | Smoothing epsilon            |

**Response:**

```json
{
  "status": "ok"
}
```

**Notes:**

- Presets stored in non-volatile memory
- Persist across power cycles
- 4 preset slots available (indices 0-3)
- `save_current: true` captures active valve configuration

---

### ODrive Motor Controller

#### GET /api/v1/odrive

Get ODrive motor controller status and telemetry.

**Request:**

```http
GET /api/v1/odrive HTTP/1.1
Host: 192.168.1.100:8080
X-API-Key: steve-valve-2025
```

**Response:**

```json
{
  "axis_error": 0,
  "axis_state": 8,
  "motor_flags": 0,
  "encoder_flags": 0,
  "controller_status": 1,
  "position": 1234.567890,
  "velocity": 0.123456,
  "bus_voltage": 24.12,
  "bus_current": 1.45,
  "fet_temp": 35.2,
  "motor_temp": 38.7
}
```

**Response Fields:**

| Field               | Type    | Unit    | Description                  |
| ------------------- | ------- | ------- | ---------------------------- |
| `axis_error`        | integer | -       | Error flags (0 = no errors)  |
| `axis_state`        | integer | -       | State code (8 = closed loop) |
| `motor_flags`       | integer | -       | Motor status flags           |
| `encoder_flags`     | integer | -       | Encoder status flags         |
| `controller_status` | integer | -       | Controller status            |
| `position`          | float   | turns   | Encoder position             |
| `velocity`          | float   | turns/s | Encoder velocity             |
| `bus_voltage`       | float   | V       | Power supply voltage         |
| `bus_current`       | float   | A       | Power supply current         |
| `fet_temp`          | float   | °C      | FET temperature              |
| `motor_temp`        | float   | °C      | Motor temperature            |

**Axis States:**

| Code | State |
|:----:|:------|
| `1` | IDLE |
| `8` | CLOSED_LOOP_CONTROL |
| Other | Calibration or error states |

---

#### POST /api/v1/odrive

Send commands to the ODrive motor controller.

**Request - Enable:**

```http
POST /api/v1/odrive HTTP/1.1
Host: 192.168.1.100:8080
X-API-Key: steve-valve-2025
Content-Type: application/json

{
  "action": "enable"
}
```

**Request - Set Torque:**

```json
{
  "action": "set_torque",
  "value": 0.15
}
```

**Request - Set Mode:**

```json
{
  "action": "set_mode",
  "value": 1
}
```

**Available Actions:**

| Action         | Value Required | Description                    |
| -------------- | -------------- | ------------------------------ |
| `ping`         | No             | Check ODrive connectivity      |
| `enable`       | No             | Enable closed-loop control     |
| `disable`      | No             | Disable (idle mode)            |
| `estop`        | No             | Emergency stop                 |
| `clear`        | No             | Clear errors                   |
| `calibrate`    | No             | Run calibration sequence       |
| `set_mode`     | Yes            | Set control mode (0-3)         |
| `set_torque`   | Yes            | Set torque command (N·m)       |
| `set_velocity` | Yes            | Set velocity command (turns/s) |
| `set_position` | Yes            | Set position command (turns)   |

**Control Modes:**

| Mode | Type |
|:----:|:-----|
| `0` | Voltage control |
| `1` | Torque control *(used by valve)* |
| `2` | Velocity control |
| `3` | Position control |

**Response:**

```json
{
  "status": "ok"
}
```

**Error Response:**

```json
{
  "status": "error",
  "error": "odrive_command_failed"
}
```

---

### CAN Bus

#### GET /api/v1/can

Get CAN bus status, encoder, and telemetry data.

**Request:**

```http
GET /api/v1/can HTTP/1.1
Host: 192.168.1.100:8080
X-API-Key: steve-valve-2025
```

**Response:**

```json
{
  "tx_count": 12345,
  "rx_count": 12340,
  "error_count": 0,
  "bus_state": 0,
  "position": 1234.567890,
  "velocity": 0.123456,
  "bus_voltage": 24.12,
  "bus_current": 1.45,
  "fet_temp": 35.2,
  "motor_temp": 38.7
}
```

**Response Fields:**

| Field         | Type    | Unit    | Description            |
| ------------- | ------- | ------- | ---------------------- |
| `tx_count`    | integer | -       | Messages transmitted   |
| `rx_count`    | integer | -       | Messages received      |
| `error_count` | integer | -       | CAN errors             |
| `bus_state`   | integer | -       | Bus state (0 = active) |
| `position`    | float   | turns   | Encoder position       |
| `velocity`    | float   | turns/s | Encoder velocity       |
| `bus_voltage` | float   | V       | Power voltage          |
| `bus_current` | float   | A       | Power current          |
| `fet_temp`    | float   | °C      | FET temperature        |
| `motor_temp`  | float   | °C      | Motor temperature      |

**Bus States:**

| Code | State |
|:----:|:------|
| `0` | Active (normal operation) |
| `1` | Warning |
| `2` | Passive |
| `3` | Bus-off (disconnected) |

---

### Performance Metrics

#### GET /api/v1/performance

Get detailed performance statistics and timing information.

**Request:**

```http
GET /api/v1/performance HTTP/1.1
Host: 192.168.1.100:8080
X-API-Key: steve-valve-2025
```

**Response:**

```json
{
  "pos_min_deg": -5.234,
  "pos_max_deg": 95.678,
  "pos_mean_deg": 45.123,
  "pos_rms_deg": 26.789,
  "vel_min_rad_s": -2.345,
  "vel_max_rad_s": 2.123,
  "vel_mean_rad_s": 0.012,
  "vel_rms_rad_s": 0.456,
  "tau_min_nm": -0.234,
  "tau_max_nm": 0.198,
  "tau_mean_nm": 0.012,
  "tau_rms_nm": 0.089,
  "pos_peak_to_peak_deg": 100.912,
  "vel_peak_to_peak_rad_s": 4.468,
  "zero_crossing_hz": 2,
  "loop_total_min_us": 123,
  "loop_total_max_us": 456,
  "loop_total_mean_us": 234.500000,
  "loop_physics_min_us": 45,
  "loop_physics_max_us": 78,
  "loop_physics_mean_us": 56.700000,
  "torque_clamps": 0,
  "rate_limits": 0,
  "stability_events": 0
}
```

**Response Fields - Motion Statistics:**

| Field                    | Type    | Unit    | Description           |
| ------------------------ | ------- | ------- | --------------------- |
| `pos_min_deg`            | float   | degrees | Minimum position      |
| `pos_max_deg`            | float   | degrees | Maximum position      |
| `pos_mean_deg`           | float   | degrees | Mean position         |
| `pos_rms_deg`            | float   | degrees | RMS position          |
| `vel_min_rad_s`          | float   | rad/s   | Minimum velocity      |
| `vel_max_rad_s`          | float   | rad/s   | Maximum velocity      |
| `vel_mean_rad_s`         | float   | rad/s   | Mean velocity         |
| `vel_rms_rad_s`          | float   | rad/s   | RMS velocity          |
| `tau_min_nm`             | float   | N·m     | Minimum torque        |
| `tau_max_nm`             | float   | N·m     | Maximum torque        |
| `tau_mean_nm`            | float   | N·m     | Mean torque           |
| `tau_rms_nm`             | float   | N·m     | RMS torque            |
| `pos_peak_to_peak_deg`   | float   | degrees | Position range        |
| `vel_peak_to_peak_rad_s` | float   | rad/s   | Velocity range        |
| `zero_crossing_hz`       | integer | Hz      | Direction change rate |

**Response Fields - Timing:**

| Field                  | Type    | Unit | Description            |
| ---------------------- | ------- | ---- | ---------------------- |
| `loop_total_min_us`    | integer | μs   | Minimum loop time      |
| `loop_total_max_us`    | integer | μs   | Maximum loop time      |
| `loop_total_mean_us`   | float   | μs   | Mean loop time         |
| `loop_physics_min_us`  | integer | μs   | Min physics calc time  |
| `loop_physics_max_us`  | integer | μs   | Max physics calc time  |
| `loop_physics_mean_us` | float   | μs   | Mean physics calc time |

**Response Fields - Safety:**

| Field              | Type    | Description             |
| ------------------ | ------- | ----------------------- |
| `torque_clamps`    | integer | Torque limit violations |
| `rate_limits`      | integer | Rate limit activations  |
| `stability_events` | integer | Stability interventions |

**Notes:**

- Statistics computed over recent time window
- Reset when valve control restarts
- Use for system characterization and validation

---

### Data Streaming

#### GET /api/v1/stream

Get status of the real-time data streaming server.

**Request:**

```http
GET /api/v1/stream HTTP/1.1
Host: 192.168.1.100:8080
X-API-Key: steve-valve-2025
```

**Response:**

```json
{
  "active": true,
  "connected_clients": 2,
  "default_interval_ms": 100,
  "messages_sent": 12345,
  "send_errors": 0,
  "invalid_samples": 0,
  "last_send_tick_ms": 123456
}
```

**Response Fields:**

| Field                 | Type    | Description                 |
| --------------------- | ------- | --------------------------- |
| `active`              | boolean | Streaming server running    |
| `connected_clients`   | integer | Number of connected clients |
| `default_interval_ms` | integer | Streaming interval (ms)     |
| `messages_sent`       | integer | Total messages sent         |
| `send_errors`         | integer | Failed send attempts        |
| `invalid_samples`     | integer | Invalid data samples        |
| `last_send_tick_ms`   | integer | Last send timestamp         |

---

#### POST /api/v1/stream

Start or stop the real-time data streaming server.

**Request - Start:**

```http
POST /api/v1/stream HTTP/1.1
Host: 192.168.1.100:8080
X-API-Key: steve-valve-2025
Content-Type: application/json

{
  "action": "start",
  "interval_ms": 50
}
```

**Request - Stop:**

```json
{
  "action": "stop"
}
```

**Request Body:**

| Field         | Type    | Required | Description                         |
| ------------- | ------- | -------- | ----------------------------------- |
| `action`      | string  | Yes      | "start" or "stop"                   |
| `interval_ms` | integer | No       | Streaming interval (default: 100ms) |

**Response:**

```json
{
  "status": "ok"
}
```

**Notes:**

- Streaming server listens on TCP port 8888
- Binary data format for efficiency
- Multiple clients supported
- Minimum practical interval: ~10ms
- See [REST API Examples](rest-api-examples.md) for client code

---

## Complete Endpoint Reference

### Summary Table

| Endpoint              | Method | Purpose              | Auth Required |
| --------------------- | ------ | -------------------- | ------------- |
| `/`                   | GET    | Web UI (HTML page)   | No            |
| `/api/v1/config`      | GET    | Get configuration    | Yes           |
| `/api/v1/config`      | POST   | Update configuration | Yes           |
| `/api/v1/status`      | GET    | Get real-time status | Yes           |
| `/api/v1/control`     | POST   | Start/stop/preset    | Yes           |
| `/api/v1/presets`     | GET    | List all presets     | Yes           |
| `/api/v1/presets`     | POST   | Save preset          | Yes           |
| `/api/v1/odrive`      | GET    | Get ODrive status    | Yes           |
| `/api/v1/odrive`      | POST   | Control ODrive       | Yes           |
| `/api/v1/can`         | GET    | Get CAN bus status   | Yes           |
| `/api/v1/performance` | GET    | Get performance data | Yes           |
| `/api/v1/stream`      | GET    | Get stream status    | Yes           |
| `/api/v1/stream`      | POST   | Control streaming    | Yes           |

### Rate Limits

There are no enforced rate limits, but recommended polling rates:

- **Status updates:** 1-10 Hz (100-1000ms)
- **Configuration changes:** As needed (not rapid)
- **Performance data:** 0.1-1 Hz (1-10s)
- **Streaming:** Use TCP stream server (port 8888) for high-rate data

### Concurrency

- Multiple clients supported
- HTTP server handles up to 8 concurrent connections
- Streaming server supports multiple clients
- No request queuing - requests processed immediately

---

## Best Practices

### For Robotics Integration

1. **Initialize in sequence:**

   ```
   POST /api/v1/odrive (action: enable)
   POST /api/v1/config (set parameters)
   POST /api/v1/control (action: start)
   ```
1. **Monitor with GET /api/v1/status** at 5-10 Hz
1. **Use streaming** (port 8888) for real-time feedback
1. **Graceful shutdown:**

   ```
   POST /api/v1/control (action: stop)
   POST /api/v1/odrive (action: disable)
   ```

### For Data Collection

1. **Start streaming server:**

   ```json
   POST /api/v1/stream
   {"action": "start", "interval_ms": 10}
   ```
1. **Connect TCP client to port 8888**
1. **Query performance data periodically:**

   ```
   GET /api/v1/performance (every 5-10 seconds)
   ```
1. **Stop and save:**

   ```json
   POST /api/v1/stream
   {"action": "stop"}
   ```

### For Parameter Tuning

1. **Load a baseline preset:**

   ```json
   POST /api/v1/control
   {"preset": 0}
   ```
1. **Adjust parameters incrementally:**

   ```json
   POST /api/v1/config
   {"viscous": 0.06}
   ```
1. **Monitor real-time response:**

   ```
   GET /api/v1/status
   ```
1. **Save good configurations:**

   ```json
   POST /api/v1/presets
   {"index": 0, "save_current": true}
   ```

### Error Handling

Always check HTTP status code and `status` field:

```python
response = requests.get(url, headers=headers)
if response.status_code != 200:
    print(f"HTTP Error: {response.status_code}")
    return

data = response.json()
if data.get("status") == "error":
    print(f"API Error: {data.get('error')}")
    return
```

### Network Configuration

1. **Find device IP:**

   - Connect to CLI over USB
   - Run `ethstatus` command
   - Note IP address
1. **Set static IP (optional):**

   - CLI: `setip 192.168.1.100 255.255.255.0 192.168.1.1`
   - Configuration persists across reboots
1. **Test connectivity:**

   ```bash
   curl -H "X-API-Key: steve-valve-2025" \
        http://192.168.1.100:8080/api/v1/status
   ```

---

## Security Considerations

### API Key

- Default key is `steve-valve-2025`
- Transmitted in cleartext over HTTP
- Change for production deployments
- Recompile firmware with new key

### Network Security

- No HTTPS support (embedded system limitation)
- Use on trusted networks only
- Consider VPN for remote access
- Firewall to restrict access if needed

### Physical Security

- Device has no authentication beyond API key
- Physical access allows firmware reprogramming
- USB CLI provides full control
- Secure physical installation environment

---

## Troubleshooting

### 401 Unauthorized

**Problem:** All requests return 401

**Solution:**

- Check `X-API-Key` header is included
- Verify key matches: `steve-valve-2025`
- Check for typos in header name

### Connection Refused

**Problem:** Cannot connect to port 8080

**Solution:**

- Verify device IP with CLI `ethstatus`
- Check HTTP server is started: CLI `http status`
- Start server: CLI `http start`
- Verify network connectivity: `ping <device-ip>`

### 503 Service Unavailable

**Problem:** Requests return 503 error

**Solution:**

- Valve system may not be initialized
- Check with CLI `valve_status`
- May need to enable ODrive first
- Check CLI `odrive_status` for issues

### Timeout or No Response

**Problem:** Requests hang or timeout

**Solution:**

- Check network connectivity
- Verify device is powered on
- Check Ethernet cable connection
- Restart HTTP server: CLI `http stop` then `http start`

<details>
<summary><strong>Invalid JSON Error</strong></summary>
<br>

**Problem:** 400 error with `json_parse_error`

**Solution:**

- Validate JSON syntax
- Ensure proper Content-Type header
- Check for trailing commas
- Use JSON validator

</details>


---

## See Also

| Resource | Link |
|:---------|:-----|
| REST API Examples | [API Examples](rest-api-examples.md) |
| Web Interface Guide | [Web GUI](../web/web-interface-guide.md) |
| Streaming Guide | [Streaming](../stream/streaming-guide.md) |
| Streaming Examples | [Streaming Examples](../stream/streaming-examples.md) |
| CLI Reference | [CLI Reference](../cli/CLI%20Reference.md) |
| Getting Started | [Getting Started](../cli/Getting%20Started.md) |

---

<div align="center">
   <sub>
      <a href="../../../README.md">Back to Main README</a> · <a href="rest-api-examples.md">API Examples →</a>
   </sub>
</div>