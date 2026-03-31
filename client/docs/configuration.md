# Configuration Guide

This guide covers configuring PySteve for your network and application requirements.

## Network Configuration

### Device Discovery

Find your PRISM device on the network:

```python
from pysteve.utils import discover_devices

# Scan local network (192.168.1.0/24)
devices = discover_devices(subnet="192.168.1.0/24", timeout=5.0)

for device in devices:
    print(f"Found device at {device['ip']}")
    print(f"  MAC: {device['mac']}")
    print(f"  Hostname: {device['hostname']}")
```

### Static IP Configuration

Configure your PRISM device with a static IP address via the web interface or CLI.

**Recommended static IP ranges:**
- Development: `192.168.1.100-199`
- Production: Configure based on your network policy

### API Key Configuration

Set the API key for authentication:

```python
from pysteve import SteveClient

# Default API key
client = SteveClient("192.168.1.100", api_key="steve-valve-2025")

# Custom API key
client = SteveClient("192.168.1.100", api_key="your-custom-key")
```

**Security Note**: Change the default API key in production environments.

## Valve Parameters

### Parameter Ranges

Valid ranges for each parameter:

| Parameter | Range | Unit | Description |
|-----------|-------|------|-------------|
| `viscous` | 0.01 - 0.5 | N·m·s/rad | Viscous damping coefficient |
| `coulomb` | 0.005 - 0.05 | N·m | Coulomb friction torque |
| `wall_stiffness` | 0.5 - 5.0 | N·m/turn | Wall spring stiffness |
| `wall_damping` | 0.05 - 0.5 | N·m·s/turn | Wall damping coefficient |
| `smoothing` | 0.0001 - 0.01 | - | Smoothing epsilon |
| `torque_limit` | 0.1 - 2.0 | N·m | Maximum torque |
| `travel` | 1 - 360 | degrees | Total valve travel |
| `closed_position` | 0 - 360 | degrees | Closed endpoint |
| `open_position` | 0 - 360 | degrees | Open endpoint |

### Configuration Presets

PySteve includes 4 preset configurations:

#### Preset 0: "smooth"
```python
ValveConfig(
    viscous=0.05,
    coulomb=0.005,
    wall_stiffness=1.0,
    wall_damping=0.1,
    smoothing=0.001,
    torque_limit=0.5,
    travel=90
)
```
**Use case**: Smooth, low-friction operation

#### Preset 1: "medium"
```python
ValveConfig(
    viscous=0.1,
    coulomb=0.01,
    wall_stiffness=2.0,
    wall_damping=0.2,
    smoothing=0.001,
    torque_limit=0.5,
    travel=90
)
```
**Use case**: Balanced feel, general purpose

#### Preset 2: "tight"
```python
ValveConfig(
    viscous=0.2,
    coulomb=0.02,
    wall_stiffness=3.0,
    wall_damping=0.3,
    smoothing=0.001,
    torque_limit=0.5,
    travel=90
)
```
**Use case**: High damping, tight control

#### Preset 3: "custom"
```python
# User-defined configuration
# Saved via save_preset(3, config)
```

### Loading Presets

```python
from pysteve import SteveClient

client = SteveClient("192.168.1.100")
client.connect()

# Load preset by index (0=light, 1=medium, 2=heavy, 3=industrial)
client.load_preset(0)
client.load_preset(1)
```

### Custom Configuration

Create custom configurations with validation:

```python
from pysteve.core.config import ValveConfig

# Create configuration
config = ValveConfig(
    viscous=0.08,
    coulomb=0.012,
    wall_stiffness=1.5,
    wall_damping=0.15,
    smoothing=0.001,
    torque_limit=0.5,
    travel=90,
    closed_position=0,
    open_position=90
)

# Apply to device
client.update_config(**config.to_dict())

# Or save as preset
client.save_preset(3, config)
```

## Streaming Configuration

### Stream Rate

Configure streaming rate based on your needs:

| Rate (Hz) | Interval (ms) | Use Case |
|-----------|---------------|----------|
| 10 | 100 | Low-bandwidth monitoring |
| 20 | 50 | Standard data logging |
| 50 | 20 | Real-time control |
| 100 | 10 | High-speed data acquisition |

```python
from pysteve import SteveStreamer

streamer = SteveStreamer(client)

# Start at 50 Hz
streamer.start_stream(interval_ms=20)

# Or 100 Hz for high-speed
streamer.start_stream(interval_ms=10)
```

**Performance Note**: Higher rates increase CPU usage and network bandwidth.

### Buffer Size

Configure circular buffer size:

```python
from pysteve import SteveStreamer

# Default: 1000 samples
streamer = SteveStreamer(client, buffer_size=1000)

# Larger buffer for longer recording
streamer = SteveStreamer(client, buffer_size=10000)
```

**Memory usage**: ~1 KB per sample

### Thread Safety

Configure callback thread safety:

```python
# Thread-safe callbacks (default)
streamer = SteveStreamer(client, threadsafe=True)

# Disable for single-threaded performance
streamer = SteveStreamer(client, threadsafe=False)
```

## Connection Configuration

### Auto-Reconnect

Configure automatic reconnection on connection loss:

```python
client = SteveClient(
    "192.168.1.100",
    auto_reconnect=True,      # Enable auto-reconnect
    max_retries=10,            # Maximum retry attempts
    retry_delay_s=1.0,         # Initial retry delay
    backoff_factor=2.0,        # Exponential backoff multiplier
    max_retry_delay_s=30.0     # Maximum delay between retries
)
```

**Reconnection schedule**: 1s, 2s, 4s, 8s, 16s, 30s, 30s, ...

### Connection Callbacks

Register callbacks for connection events:

```python
def on_disconnect():
    print("Lost connection to device!")

def on_reconnect():
    print("Reconnected successfully!")

client = SteveClient("192.168.1.100")
client.on_disconnect = on_disconnect
client.on_reconnect = on_reconnect
```

### Timeout Configuration

```python
client = SteveClient(
    "192.168.1.100",
    timeout_s=5.0,  # Request timeout
    connect_timeout_s=10.0  # Connection timeout
)
```

## Environment Variables

Configure via environment variables:

```bash
# Device IP
export PRISM_IP=192.168.1.100

# API key
export PRISM_API_KEY=your-api-key

# Streaming rate
export PRISM_STREAM_HZ=50

# Auto-reconnect
export PRISM_AUTO_RECONNECT=true
```

Load in Python:

```python
import os
from pysteve import SteveClient

client = SteveClient(
    os.getenv("PRISM_IP", "192.168.1.100"),
    api_key=os.getenv("PRISM_API_KEY", "steve-valve-2025"),
    auto_reconnect=os.getenv("PRISM_AUTO_RECONNECT", "true").lower() == "true"
)
```

## Configuration Files

### YAML Configuration

Create `steve_config.yaml`:

```yaml
device:
  ip: 192.168.1.100
  api_key: steve-valve-2025
  timeout_s: 5.0

connection:
  auto_reconnect: true
  max_retries: 10
  retry_delay_s: 1.0

streaming:
  rate_hz: 50
  buffer_size: 1000
  threadsafe: true

valve:
  preset: smooth
  custom_config:
    viscous: 0.08
    coulomb: 0.012
    wall_stiffness: 1.5
```

Load configuration:

```python
import yaml
from pysteve import SteveClient

with open("steve_config.yaml") as f:
    config = yaml.safe_load(f)

client = SteveClient(
    config["device"]["ip"],
    api_key=config["device"]["api_key"],
    auto_reconnect=config["connection"]["auto_reconnect"],
    timeout_s=config["device"]["timeout_s"]
)
```

### JSON Configuration

```json
{
  "device": {
    "ip": "192.168.1.100",
    "api_key": "steve-valve-2025"
  },
  "valve": {
    "viscous": 0.08,
    "coulomb": 0.012,
    "wall_stiffness": 1.5
  }
}
```

## Multi-Device Configuration

Configure multiple devices:

```python
devices = [
    {
        "id": "valve_1",
        "ip": "192.168.1.100",
        "role": "primary"
    },
    {
        "id": "valve_2",
        "ip": "192.168.1.101",
        "role": "secondary"
    }
]

clients = {}
for device in devices:
    clients[device["id"]] = SteveClient(device["ip"])
    clients[device["id"]].connect()
```

## Performance Tuning

### Latency Optimization

```python
# Reduce latency
client = SteveClient(
    "192.168.1.100",
    timeout_s=1.0,  # Lower timeout
    keepalive=True  # Enable TCP keepalive
)

streamer = SteveStreamer(
    client,
    buffer_size=100,  # Smaller buffer
    threadsafe=False  # Disable locks
)
```

### Throughput Optimization

```python
# Maximize throughput
streamer = SteveStreamer(
    client,
    buffer_size=10000,  # Large buffer
    threadsafe=True     # Safe for processing
)
```

## Logging Configuration

Enable debug logging:

```python
import logging

logging.basicConfig(level=logging.DEBUG)
logger = logging.getLogger("pysteve")
logger.setLevel(logging.DEBUG)
```

## Next Steps

- [Quick Start Tutorial](tutorials/quickstart.md)
- [API Reference](api/client.md)
- [Performance Optimization](advanced/performance.md)

