# Quickstart Guide

Get started with PySteve in 5 minutes.

## Installation

Install PySteve with pip:

```bash
pip install pysteve
```

For specific integrations:
```bash
pip install pysteve[mujoco]      # MuJoCo support
pip install pysteve[gymnasium]   # Gymnasium RL
pip install pysteve[async]       # Async client
pip install pysteve[all]         # Everything
```

See the [full installation guide](installation.md) for detailed instructions.

## Connect to Device

Find your PRISM device on the network and connect:

```python
from pysteve import SteveClient

# Connect to device (replace with your IP)
client = SteveClient("192.168.1.100")
client.connect()

# Check connection
print(f"Connected to {client.device_ip}")
```

## Enable Motor

Before using the valve, enable the motor:

```python
# Enable motor
client.enable_motor()

# Verify motor is enabled
status = client.get_status()
print(f"Motor enabled: {status['motor_enabled']}")
```

## Start the Valve

Start valve control:

```python
# Start valve with default configuration
client.start_valve()

# Or load a preset first (0=light, 1=medium, 2=heavy, 3=industrial)
client.load_preset(0)
client.start_valve()
```

## Read Status

Get real-time valve state:

```python
status = client.get_status()

print(f"Position: {status['pos_deg']:.2f}°")
print(f"Velocity: {status['vel_rad_s']:.3f} rad/s")
print(f"Torque: {status['torque_nm']:.3f} Nm")
print(f"Valve running: {status['valve_running']}")
```

## Update Parameters

Adjust haptic feel in real-time:

```python
# Update individual parameters
client.update_config(viscous=0.08)
client.update_config(coulomb=0.015)

# Or update multiple at once
client.update_config(
    viscous=0.08,
    coulomb=0.015,
    wall_stiffness=2.0,
    wall_damping=0.2
)
```

## Stream Data

Stream real-time data at high speed:

```python
def on_data(sample):
    print(f"Position: {sample['position_deg']:.2f}°")

# Start streaming at 50 Hz
client.start_streaming(rate_hz=50)
client.streamer.register_callback(on_data)

# Let it run...
import time
time.sleep(10)

# Stop streaming
client.stop_streaming()
```

## Stop and Cleanup

Always stop the valve and disable motor when done:

```python
# Stop valve
client.stop_valve()

# Disable motor
client.disable_motor()

# Disconnect
client.disconnect()
```

Or use context manager for automatic cleanup:

```python
with SteveClient("192.168.1.100") as client:
    client.enable_motor()
    client.start_valve()
    
    # ... your code ...
    
    # Automatic cleanup on exit
```

## Next Steps

- **Learn more**: [Configuration Guide](configuration.md)
- **Deep dive**: [Tutorials](tutorials/)
- **See examples**: [Examples Directory](../examples/)
- **Full reference**: [API Documentation](api/)

## Quick Reference

### Common Presets

```python
client.load_preset(0)  # Light - Low damping, easy to turn
client.load_preset(1)  # Medium - Balanced parameters
client.load_preset(2)  # Heavy - High damping, stiff feel
client.load_preset(3)  # Industrial - Maximum resistance
```

### Parameter Ranges

- `viscous`: 0.01 - 0.5 N·m·s/rad (damping)
- `coulomb`: 0.005 - 0.05 N·m (friction)
- `wall_stiffness`: 0.5 - 5.0 N·m/turn
- `wall_damping`: 0.05 - 0.5 N·m·s/turn
- `torque_limit`: 0.1 - 2.0 N·m

### Common Operations

```python
# Get configuration
config = client.get_config()

# Check if motor is enabled
status = client.get_status()
if status['motor_enabled']:
    client.start_valve()

# Emergency stop
client.stop_valve()
client.disable_motor()
```

## Troubleshooting

**Can't connect?**
- Verify device IP: `ping 192.168.1.100`
- Check firewall settings
- Ensure device is powered on

**Motor won't enable?**
- Check status for error messages
- Verify safety interlocks
- Try disconnecting and reconnecting

**No streaming data?**
- Call `client.start_streaming()` explicitly
- Check callback is registered
- Verify network bandwidth

For more help, see the [full documentation](index.md).

