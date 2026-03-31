# PRISM TCP Streaming Guide

## Overview

PRISM provides real-time TCP streaming of valve simulation data for robotics applications, data logging, and visualization. The streaming server delivers JSON-formatted data packets containing encoder position, velocity, torque, and diagnostic information at configurable intervals.

## Key Features

- **Real-time data streaming** over TCP (port 8888)
- **Configurable streaming intervals** (specify interval in milliseconds)
- **Multiple concurrent connections** (up to 6 clients simultaneously)
- **JSON format** for easy parsing in any language
- **Low latency** streaming directly from control loop
- **Comprehensive data** including position, velocity, torque, timing, and diagnostics

## Data Stream Format

Each data packet is a single-line JSON object containing:

### Position Data
- `position_turns` - Encoder position in turns (floating point)
- `position_deg` - Encoder position in degrees (floating point)

### Velocity Data
- `omega_rad_s` - Angular velocity in rad/s (floating point)

### Torque Data
- `torque_nm` - Commanded torque in Newton-meters (floating point)
- `filt_torque_nm` - Filtered torque in Newton-meters (floating point)

### Timing Data
- `timestamp_ms` - System timestamp in milliseconds
- `t_us` - Accumulated control loop time in microseconds (monotonic)
- `loop_time_us` - Last control loop iteration time in microseconds
- `seq` - Sample sequence number (increments each control cycle)

### Diagnostic Data
- `status` - Valve state: "RUNNING", "IDLE", or "ERROR"
- `passivity_mj` - Passivity energy in millijoules
- `quiet` - Quiet mode active (boolean)
- `err` - Last error code
- `hb_age` - Heartbeat age in milliseconds
- `data_valid` - Data validity flag (boolean)

### Example Data Packet

```json
{"timestamp_ms":12345678,"t_us":987654321,"loop_time_us":125,"seq":7890,"position_turns":2.456,"position_deg":883.2,"torque_nm":0.125,"filt_torque_nm":0.118,"status":"RUNNING","omega_rad_s":1.234,"passivity_mj":5,"quiet":false,"err":0,"hb_age":10,"data_valid":true}
```

## Setting Up Streaming

You can start and stop streaming using either the CLI or REST API.

### Using the CLI

Connect to PRISM via USB ST-LINK and use the `eth_stream` command:

#### Start Streaming at Default Rate (100 Hz)

```
eth_stream start
```

#### Start Streaming at 50 Hz (20 ms interval)

```
eth_stream start 20
```

#### Start Streaming at 10 Hz (100 ms interval)

```
eth_stream start 100
```

#### Stop Streaming

```
eth_stream stop
```

**CLI Syntax:**
```
eth_stream start [interval_ms]
eth_stream stop
```

- `interval_ms` - Optional streaming interval in milliseconds
- Default interval: 100 ms if not specified

### Using the REST API

You can control streaming programmatically via HTTP requests to `/api/v1/stream`.

#### Start Streaming at 50 Hz

**Request:**
```http
POST /api/v1/stream HTTP/1.1
Host: 192.168.1.100:8080
X-API-Key: steve-valve-2025
Content-Type: application/json

{
  "action": "start",
  "interval_ms": 20
}
```

**Response:**
```json
{
  "status": "success",
  "message": "stream_started"
}
```

#### Stop Streaming

**Request:**
```http
POST /api/v1/stream HTTP/1.1
Host: 192.168.1.100:8080
X-API-Key: steve-valve-2025
Content-Type: application/json

{
  "action": "stop"
}
```

**Response:**
```json
{
  "status": "success",
  "message": "stream_stopped"
}
```

#### Get Streaming Status

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
  "default_interval_ms": 20,
  "messages_sent": 45678,
  "send_errors": 0,
  "invalid_samples": 0,
  "last_send_tick_ms": 12345678
}
```

## Connecting to the Stream

Once streaming is started, connect to PRISM's IP address on port 8888 using a TCP client.

### Connection Details

- **Protocol:** TCP
- **Port:** 8888
- **Format:** Newline-delimited JSON (one object per line)
- **Encoding:** UTF-8

### Connection Workflow

1. Start streaming (via CLI or REST API)
2. Connect TCP client to `<device-ip>:8888`
3. Receive hello message confirming connection
4. Receive continuous stream of JSON data packets
5. Parse JSON and extract desired fields
6. Close connection when done

### Hello Message

Upon connection, the server sends a hello message:

```json
{"stream":"ready","interval_ms":20,"version":"1.0"}
```

This confirms the connection and reports the current streaming interval.

## Common Streaming Rates

| Frequency | Interval (ms) | Use Case |
|-----------|---------------|----------|
| 100 Hz | 10 | High-speed control, research |
| 50 Hz | 20 | Real-time robotics control |
| 20 Hz | 50 | Visualization, monitoring |
| 10 Hz | 100 | Data logging, low bandwidth |
| 5 Hz | 200 | Status monitoring |
| 1 Hz | 1000 | Slow polling |

**Recommended:** 20 ms interval provides excellent balance between data rate and network overhead for most robotics applications.

## Implementation Examples

See [Streaming Examples](streaming-examples.md) for complete code examples in:

- Python (socket and threading)
- C++ (Boost.Asio)
- JavaScript/Node.js
- ROS 2 (subscriber node)

## Troubleshooting

### No Data Received

**Problem:** TCP connection succeeds but no data arrives.

**Solutions:**
1. Verify streaming is started: `GET /api/v1/stream` should show `"active": true`
2. Check that valve is running: `valve_status` in CLI or `GET /api/v1/status`
3. Verify network connectivity: `ping <device-ip>`
4. Check firewall settings on client machine

### Connection Refused

**Problem:** Cannot connect to port 8888.

**Solutions:**
1. Ensure streaming is started via `eth_stream start` or REST API
2. Verify correct IP address (use `ip_info` in CLI)
3. Check network cable and switch connectivity
4. Ensure client is on same network/subnet

### Data Rate Too Slow or Fast

**Problem:** Data arrives at wrong rate.

**Solutions:**
1. Adjust interval with `eth_stream start <interval_ms>`
2. Or via REST API: `POST /api/v1/stream` with `"interval_ms": 20`
3. Verify current rate: `GET /api/v1/stream` shows `default_interval_ms`

### Invalid or NaN Data

**Problem:** JSON contains invalid values or `"data_valid": false`.

**Solutions:**
1. Check that ODrive is enabled and calibrated
2. Verify valve is in RUNNING state
3. Check encoder connection to ODrive
4. Review error code in `err` field
5. Check `GET /api/v1/status` for system errors

### Connection Drops

**Problem:** TCP connection disconnects unexpectedly.

**Solutions:**
1. Implement automatic reconnection in client code
2. Check network stability and cable quality
3. Verify no IP address conflicts on network
4. Monitor `send_errors` in streaming status
5. Reduce streaming rate if network is congested

### Buffer Overflow or Latency

**Problem:** Data buffering causes increasing latency.

**Solutions:**
1. Process data immediately upon receipt (don't buffer)
2. Reduce streaming rate to match processing capability
3. Use separate thread for network I/O
4. Implement flow control in application
5. Consider using UDP for lowest latency (future feature)

## Performance Considerations

### Network Bandwidth

Each JSON packet is approximately 250-300 bytes. Bandwidth requirements:

- 100 Hz: ~30 KB/s per client
- 50 Hz: ~15 KB/s per client
- 20 Hz: ~6 KB/s per client
- 10 Hz: ~3 KB/s per client

Ethernet bandwidth is typically 100 Mbps, so network congestion is unlikely with reasonable client counts.

### CPU Load

Streaming adds minimal CPU overhead:
- ~0.1% CPU per client at 100 Hz
- Negligible impact on control loop performance

### Multiple Clients

The server supports up to 6 simultaneous TCP connections. Each client can have a different streaming interval (configured per-connection based on the default rate set when starting the stream).

## Data Logging Best Practices

### High-Frequency Logging

For data capture at high rates (50-100 Hz):

1. Use binary format for storage (convert from JSON)
2. Buffer writes to disk (write in chunks)
3. Use dedicated logging thread
4. Pre-allocate file space
5. Consider SSD for write performance

### Long-Duration Logging

For extended logging sessions:

1. Use lower streaming rate (10-20 Hz)
2. Implement log rotation (time-based or size-based)
3. Compress older log files
4. Monitor disk space
5. Include timestamp for synchronization

### Synchronized Multi-Device Logging

When logging from multiple PRISM devices:

1. Use NTP for time synchronization
2. Include `t_us` (monotonic time) for relative timing
3. Use `seq` field to detect dropped packets
4. Record network latency for time correction
5. Align data using common trigger events

## Advanced Features

### Selective Data Streaming

Currently, all fields are streamed in every packet. To reduce bandwidth for specific applications, you can:

1. Filter fields in client code (ignore unused fields during parsing)
2. Use slower streaming rate and interpolate if needed
3. Contact firmware team for custom streaming formats

### Data Validation

Always check the `data_valid` field before using data:

```python
data = json.loads(line)
if data.get('data_valid', False):
    position = data['position_deg']
    velocity = data['omega_rad_s']
    torque = data['torque_nm']
    # Process valid data
else:
    # Handle invalid data (log warning, use last valid value, etc.)
    pass
```

### Timestamp Synchronization

PRISM provides two time references:

1. **`timestamp_ms`** - System time in milliseconds (may wrap around)
2. **`t_us`** - Monotonic accumulated time in microseconds (never wraps)

For precise timing analysis, use `t_us` which is directly from the control loop.

## Integration with Other Tools

### MATLAB/Simulink

Use MATLAB's TCP/IP interface to stream data:

```matlab
t = tcpclient('192.168.1.100', 8888);
while t.BytesAvailable > 0
    line = readline(t);
    data = jsondecode(line);
    plot(data.position_deg, data.torque_nm);
end
```

### LabVIEW

Use TCP Read/Write VIs to stream data into LabVIEW for real-time visualization and control.

### ROS/ROS 2

See [Streaming Examples](streaming-examples.md) for complete ROS 2 node implementation.

## Security Considerations

The TCP streaming server has no authentication mechanism. Access control should be implemented at the network level:

1. Use firewalls to restrict access to port 8888
2. Deploy PRISM on isolated network or VLAN
3. Use VPN for remote access
4. Monitor connected clients via `GET /api/v1/stream`

## See Also

- [Streaming Examples](streaming-examples.md) - Complete code examples
- [Web Interface Guide](../html/web-interface-guide.md) - Browser-based control panel
- [REST API Reference](../rest/rest-api.md) - API endpoint documentation
- [CLI Reference](../cli/cli-reference.md) - Command-line interface
- [Getting Started Guide](../getting-started/getting-started.md) - Initial setup

## Support

For questions or issues with streaming:

1. Check streaming status: `GET /api/v1/stream`
2. Review firmware logs via CLI
3. Verify network configuration
4. Contact PRISM firmware team with specific error details

