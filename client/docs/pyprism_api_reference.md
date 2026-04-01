# SteveClient API Reference

Complete API documentation for the synchronous PRISM client.

## Class: SteveClient

Main interface for controlling PRISM haptic valve hardware.

```python
from pysteve import SteveClient

client = SteveClient(
    device_ip="192.168.1.100",
    port=5000,
    timeout=5.0,
    auto_reconnect=True,
    retry_delay=1.0,
    max_retries=3
)
```

### Constructor Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `device_ip` | str | Required | IP address of PRISM device |
| `port` | int | 5000 | REST API port |
| `timeout` | float | 5.0 | Request timeout in seconds |
| `auto_reconnect` | bool | True | Enable automatic reconnection |
| `retry_delay` | float | 1.0 | Delay between retry attempts |
| `max_retries` | int | 3 | Maximum reconnection attempts |

### Connection Management

#### `connect() -> None`

Establish connection to PRISM device.

```python
client.connect()
```

**Raises**:
- `ConnectionError`: If connection fails
- `TimeoutError`: If connection times out

#### `disconnect() -> None`

Close connection to device.

```python
client.disconnect()
```

#### `is_connected() -> bool`

Check if connected to device.

```python
if client.is_connected():
    print("Connected")
```

**Returns**: `bool` - Connection status

#### `ping() -> float`

Test connection and measure latency.

```python
latency_ms = client.ping()
print(f"Latency: {latency_ms:.2f} ms")
```

**Returns**: `float` - Round-trip time in milliseconds

**Raises**: `ConnectionError` if device unreachable

### Motor Control

#### `enable_motor() -> None`

Enable the motor drive.

```python
client.enable_motor()
```

**Raises**: `SteveError` if motor cannot be enabled

#### `disable_motor() -> None`

Disable the motor drive.

```python
client.disable_motor()
```

#### `is_motor_enabled() -> bool`

Check if motor is enabled.

```python
if client.is_motor_enabled():
    client.start_valve()
```

**Returns**: `bool` - Motor enabled status

### Valve Control

#### `start_valve() -> None`

Start valve control loop.

```python
client.start_valve()
```

**Preconditions**: Motor must be enabled

**Raises**: `SteveError` if valve cannot start

#### `stop_valve() -> None`

Stop valve control loop.

```python
client.stop_valve()
```

#### `is_valve_running() -> bool`

Check if valve control is active.

```python
if client.is_valve_running():
    client.stop_valve()
```

**Returns**: `bool` - Valve running status

### Status and State

#### `get_status() -> Dict[str, Any]`

Get current valve state.

```python
status = client.get_status()
print(f"Position: {status['pos_deg']:.2f}°")
print(f"Velocity: {status['vel_rad_s']:.3f} rad/s")
print(f"Torque: {status['torque_nm']:.3f} Nm")
```

**Returns**: Dictionary with fields:
- `pos_deg` (float): Position in degrees
- `vel_rad_s` (float): Velocity in rad/s
- `torque_nm` (float): Torque in N·m
- `motor_enabled` (bool): Motor state
- `valve_running` (bool): Valve control state
- `timestamp` (float): Unix timestamp

#### `get_position() -> float`

Get current position in degrees.

```python
position = client.get_position()
```

**Returns**: `float` - Position in degrees

#### `get_velocity() -> float`

Get current velocity in rad/s.

```python
velocity = client.get_velocity()
```

**Returns**: `float` - Velocity in rad/s

#### `get_torque() -> float`

Get current torque in N·m.

```python
torque = client.get_torque()
```

**Returns**: `float` - Torque in N·m

### Configuration

#### `get_config() -> ValveConfig`

Get current valve configuration.

```python
config = client.get_config()
print(f"Viscous: {config.viscous}")
print(f"Coulomb: {config.coulomb}")
```

**Returns**: `ValveConfig` dataclass

#### `update_config(**kwargs) -> None`

Update valve parameters.

```python
client.update_config(
    viscous=0.08,
    coulomb=0.015,
    wall_stiffness=2.0,
    wall_damping=0.2
)
```

**Parameters**:
- `viscous` (float, optional): Viscous damping (0.01-0.5)
- `coulomb` (float, optional): Coulomb friction (0.005-0.05)
- `wall_stiffness` (float, optional): Wall stiffness (0.5-5.0)
- `wall_damping` (float, optional): Wall damping (0.05-0.5)
- `smoothing` (float, optional): Smoothing factor (0.0001-0.01)
- `torque_limit` (float, optional): Torque limit (0.1-2.0)
- `travel` (float, optional): Angular travel (degrees)

**Raises**: `ValueError` if parameters out of range

### Presets

#### `load_preset(preset: int) -> None`

Load predefined configuration preset by index.

```python
client.load_preset(0)  # 0=light, 1=medium, 2=heavy, 3=industrial
```

**Parameters**:
- `preset` (int): Preset index (0-3)

**Raises**: `SteveValidationError` if preset not in range 0-3

#### `save_preset(slot: int, config: ValveConfig) -> None`

Save configuration to preset slot.

```python
from pysteve.core.config import ValveConfig

config = ValveConfig(viscous=0.08, coulomb=0.015, ...)
client.save_preset(3, config)  # Slot 3 (custom)
```

**Parameters**:
- `slot` (int): Preset slot (0-9, where 3-9 are user-configurable)
- `config` (ValveConfig): Configuration to save

#### `list_presets() -> List[str]`

Get available preset names.

```python
presets = client.list_presets()
print(f"Available: {presets}")  # ['smooth', 'medium', 'tight']
```

**Returns**: `List[str]` - Preset names

### Streaming

#### `start_streaming(rate_hz: int = 100) -> None`

Start data streaming.

```python
client.start_streaming(rate_hz=50)
```

**Parameters**:
- `rate_hz` (int): Streaming rate in Hz (10-100)

**Note**: Access stream data via `client.streamer`

#### `stop_streaming() -> None`

Stop data streaming.

```python
client.stop_streaming()
```

### Context Manager

#### `__enter__() / __exit__()`

Use as context manager for automatic cleanup.

```python
with SteveClient("192.168.1.100") as client:
    client.enable_motor()
    client.start_valve()
    # ... use client ...
    # Automatic stop, disable, disconnect on exit
```

### Properties

#### `device_ip: str`

IP address of connected device.

```python
print(f"Connected to {client.device_ip}")
```

#### `port: int`

REST API port.

```python
print(f"Port: {client.port}")
```

#### `streamer: SteveStreamer`

Associated streaming client (created automatically).

```python
client.streamer.register_callback(my_callback)
```

### Callbacks

#### `register_connection_callback(callback: Callable[[bool], None]) -> None`

Register callback for connection events.

```python
def on_connection_change(connected: bool):
    if connected:
        print("Connected!")
    else:
        print("Disconnected!")

client.register_connection_callback(on_connection_change)
```

**Parameters**:
- `callback` (Callable): Function taking bool (connected state)

#### `unregister_connection_callback(callback: Callable) -> None`

Remove connection callback.

```python
client.unregister_connection_callback(on_connection_change)
```

## Example Usage

### Basic Control

```python
from pysteve import SteveClient

# Connect
client = SteveClient("192.168.1.100")
client.connect()

try:
    # Setup
    client.enable_motor()
    client.load_preset(1)  # Load medium preset
    client.start_valve()
    
    # Monitor
    import time
    for _ in range(10):
        status = client.get_status()
        print(f"Pos: {status['pos_deg']:.2f}°, "
              f"Torque: {status['torque_nm']:.3f} Nm")
        time.sleep(0.5)
    
finally:
    # Cleanup
    client.stop_valve()
    client.disable_motor()
    client.disconnect()
```

### With Context Manager

```python
with SteveClient("192.168.1.100") as client:
    client.enable_motor()
    client.start_valve()
    
    # Real-time parameter adjustment
    client.update_config(viscous=0.08)
    time.sleep(5)
    
    client.update_config(viscous=0.15)
    time.sleep(5)
```

### With Streaming

```python
def on_data(sample):
    print(f"Position: {sample['position_deg']:.2f}°")

with SteveClient("192.168.1.100") as client:
    client.enable_motor()
    client.start_valve()
    
    # Start streaming
    client.start_streaming(rate_hz=100)
    client.streamer.register_callback(on_data)
    
    # Let it stream
    time.sleep(10)
    
    # Stop streaming
    client.stop_streaming()
```

## Error Handling

```python
from pysteve.core.exceptions import (
    SteveError,
    ConnectionError,
    TimeoutError,
    ConfigurationError
)

try:
    client = SteveClient("192.168.1.100")
    client.connect()
    client.enable_motor()
    client.start_valve()
    
except ConnectionError as e:
    print(f"Connection failed: {e}")
except TimeoutError as e:
    print(f"Request timeout: {e}")
except ConfigurationError as e:
    print(f"Invalid configuration: {e}")
except SteveError as e:
    print(f"PRISM error: {e}")
```

## Thread Safety

`SteveClient` is **not thread-safe** by default. For concurrent access:

```python
import threading

client = SteveClient("192.168.1.100", threadsafe=True)

def worker():
    status = client.get_status()
    # Thread-safe access

threads = [threading.Thread(target=worker) for _ in range(5)]
for t in threads:
    t.start()
for t in threads:
    t.join()
```

See [Thread Safety Guide](../advanced/thread-safety.md) for details.

## See Also

- [SteveStreamer API](streaming.md) - Data streaming
- [SteveAsyncClient API](async-client.md) - Async client
- [ValveConfig](config.md) - Configuration dataclass
- [Error Handling Guide](../advanced/error-handling.md)

