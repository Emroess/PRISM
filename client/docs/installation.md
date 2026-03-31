# Installation Guide

This guide covers installing PySteve and its dependencies for different use cases.

## Prerequisites

- Python 3.8 or higher
- pip package manager
- PRISM device on your network
- (Optional) MuJoCo, Gymnasium, or Isaac Sim for framework integrations

## Basic Installation

### Core Package

Install the core PySteve package with basic functionality:

```bash
pip install pysteve
```

This includes:
- REST API client (`SteveClient`)
- TCP streaming (`SteveStreamer`)
- Configuration management
- Basic utilities

### Development Installation

For development or to run examples from source:

```bash
# Clone repository
git clone https://github.com/yourusername/steve_can.git
cd steve_can/client

# Install in editable mode
pip install -e .
```

## Optional Dependencies

PySteve uses extras groups for optional dependencies. Install only what you need:

### Async Support

For asynchronous multi-device control:

```bash
pip install pysteve[async]
```

Includes:
- `aiohttp` - Async HTTP client
- `SteveAsyncClient` functionality

### Data Analysis

For data recording and analysis:

```bash
pip install pysteve[data]
```

Includes:
- `h5py` - HDF5 file format support
- `pandas` - DataFrame support for analysis
- Export to HDF5, CSV, and more

### Visualization

For real-time and static plotting:

```bash
pip install pysteve[viz]
```

Includes:
- `matplotlib` - Plotting library
- `RealtimePlotter` and `StaticPlotter`
- Live data visualization

### MuJoCo Integration

For physics simulation with MuJoCo:

```bash
pip install pysteve[mujoco]
```

Includes:
- `mujoco` - MuJoCo physics engine
- `SteveValveActuator` for hardware-in-the-loop
- MJCF scene building utilities

**Note**: Requires MuJoCo 2.3.0 or higher.

### Gymnasium (RL) Integration

For reinforcement learning environments:

```bash
pip install pysteve[gymnasium]
```

Includes:
- `gymnasium` - RL environment standard
- `numpy` - Numerical computing
- Pre-built RL environments and wrappers

Compatible with Stable-Baselines3, Ray RLlib, etc.

### Isaac Sim Integration

For NVIDIA Isaac Sim integration:

```bash
pip install pysteve[isaac]
```

**Note**: Requires NVIDIA Isaac Sim 2023.1+ to be installed separately.
Install Isaac Sim from: https://developer.nvidia.com/isaac-sim

### ROS2 Bridge

For ROS2 integration:

```bash
pip install pysteve[ros]
```

**Note**: Requires ROS2 (Humble, Iron, or Rolling) to be installed and sourced.

Includes:
- `rclpy` - ROS2 Python client
- ROS2 node for valve control
- Publishers and subscribers for standard topics

### All Extras

Install everything:

```bash
pip install pysteve[all]
```

This installs all optional dependencies for full functionality.

## Platform-Specific Notes

### Linux

No special requirements. Standard installation works on all distributions.

```bash
# Ubuntu/Debian
sudo apt update
pip install pysteve[all]

# Fedora/RHEL
sudo dnf update
pip install pysteve[all]
```

### macOS

Standard installation via pip:

```bash
pip install pysteve[all]
```

For MuJoCo visualization on macOS:
```bash
brew install glfw
```

### Windows

Standard installation via pip:

```bash
pip install pysteve[all]
```

**Note**: MuJoCo on Windows may require Visual C++ redistributables.

## Verifying Installation

Test your installation:

```python
import pysteve
print(f"PySteve version: {pysteve.__version__}")

# Test basic import
from pysteve import SteveClient
print("✓ Core client available")

# Test optional imports
try:
    from pysteve.integrations.mujoco import SteveValveActuator
    print("✓ MuJoCo integration available")
except ImportError:
    print("✗ MuJoCo integration not installed")

try:
    from pysteve.integrations.gymnasium import SteveValveEnv
    print("✓ Gymnasium integration available")
except ImportError:
    print("✗ Gymnasium integration not installed")

try:
    from pysteve.core.async_client import SteveAsyncClient
    print("✓ Async client available")
except ImportError:
    print("✗ Async client not installed")
```

## Network Configuration

Ensure your PRISM device is accessible on the network:

### Find Device IP

Check your PRISM device display or use network scanning:

```bash
# Linux/macOS
arp -a | grep -i "odrive"

# Or use nmap
nmap -sn 192.168.1.0/24
```

### Test Connection

```python
from pysteve import SteveClient

# Replace with your device IP
client = SteveClient("192.168.1.100")

try:
    client.connect()
    print("✓ Connection successful")
    status = client.get_status()
    print(f"  Valve position: {status['pos_deg']:.2f}°")
except Exception as e:
    print(f"✗ Connection failed: {e}")
finally:
    client.disconnect()
```

## Firewall Configuration

PySteve uses these ports:
- **TCP 8080**: REST API
- **TCP 8888**: Data streaming

Ensure firewall allows these connections:

```bash
# Linux (ufw)
sudo ufw allow 8080/tcp
sudo ufw allow 8888/tcp

# Linux (firewalld)
sudo firewall-cmd --add-port=8080/tcp --permanent
sudo firewall-cmd --add-port=8888/tcp --permanent
sudo firewall-cmd --reload
```

## Troubleshooting

### Import Errors

**Problem**: `ModuleNotFoundError: No module named 'pysteve'`

**Solution**: Ensure pip installation completed successfully:
```bash
pip install --upgrade pysteve
```

### Connection Refused

**Problem**: `SteveConnectionError: Connection refused`

**Solution**: 
1. Verify device IP address
2. Check device is powered on
3. Ensure network connectivity: `ping 192.168.1.100`
4. Check firewall settings

### Missing Optional Dependencies

**Problem**: `ImportError: MuJoCo required. Install with: pip install pysteve[mujoco]`

**Solution**: Install the appropriate extras group:
```bash
pip install pysteve[mujoco]
```

## Next Steps

- [Quick Start Tutorial](tutorials/quickstart.md)
- [Configuration Guide](configuration.md)
- [API Reference](api/client.md)

