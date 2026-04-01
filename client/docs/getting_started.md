# Getting Started

## Quickstart Guide

Get started with PySteve in 5 minutes.

### Step 0: Installation

Install PySteve with pip: (works on all operating systems)

```bash
pip install pysteve
```

Alternatively, you can build from source:

```bash
# Clone repository
git clone https://github.com/Emroess/PRISM
cd PRISM/client

# Install in editable mode
pip install -e .
```

### Step 0: Find Your PyPRISM IP

*Ensure your PRISM device is accessible on the network*

Check your PRISM device display or use network scanning:

```bash
# Linux/macOS
arp -a | grep -i "odrive"

# Or use nmap
nmap -sn 192.168.1.0/24
```

### Step 0: Double Check Firewall Configuration

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

### Step 1: Import and Connect

```python
from pysteve import SteveClient

# Create client with device IP
client = SteveClient("192.168.1.100")

# Connect to device
client.connect()
print("✓ Connected to PRISM device")
```

### Step 2: Enable Motor

Before starting the valve, enable the ODrive motor:

```python
# Enable motor controller
client.enable_motor()
print("✓ Motor enabled")

# Wait for motor to stabilize
import time
time.sleep(0.5)
```

### Step 3: Start Valve Simulation

```python
# Start valve with current configuration
client.start_valve()
print("✓ Valve simulation started")
```

### Step 4: Read Status

Get real-time valve state:

```python
# Get current status
status = client.get_status()

print(f"Position: {status['pos_deg']:.2f}°")
print(f"Velocity: {status['vel_rad_s']:.2f} rad/s")
print(f"Torque: {status['torque_nm']:.3f} N·m")
print(f"Mode: {status['mode']}")
```

### Step 5: Load Preset

Try different haptic feels:

```python
# Load light preset (index 0)
client.load_preset(0)
print("✓ Loaded 'light' preset (index 0)")

time.sleep(2)

# Try heavy preset (index 2)
client.load_preset(2)
print("✓ Loaded 'heavy' preset (index 2)")
```

### Step 6: Update Parameters

Adjust parameters in real-time:

```python
# Increase viscous damping
client.update_config(viscous=0.15)
print("✓ Updated viscous damping to 0.15")

time.sleep(1)

# Adjust multiple parameters
client.update_config(
    viscous=0.08,
    coulomb=0.012,
    wall_stiffness=2.0
)
print("✓ Updated multiple parameters")
```

### Step 7: Stream Data

Get high-speed data stream:

```python
from pysteve import SteveStreamer

# Create streamer
streamer = SteveStreamer(client)

# Define callback
def on_data(sample):
    print(f"Pos: {sample['position_deg']:6.2f}° | "
          f"Vel: {sample['omega_rad_s']:6.3f} rad/s | "
          f"Torque: {sample['torque_nm']:6.3f} N·m")

# Register callback
streamer.register_callback(on_data)

# Start streaming at 50 Hz
streamer.start_stream(interval_ms=20)
print("✓ Streaming started")

# Let it run for 5 seconds
time.sleep(5)

# Stop streaming
streamer.stop_stream()
print("✓ Streaming stopped")
```

### Step 8: Stop and Cleanup

Always stop the valve and disconnect:

```python
# Stop valve simulation
client.stop_valve()
print("✓ Valve stopped")

# Disable motor
client.disable_motor()
print("✓ Motor disabled")

# Disconnect
client.disconnect()
print("✓ Disconnected")
```

## Complete Example

```python
from pysteve import SteveClient, SteveStreamer
import time

def main():
    # Connect
    client = SteveClient("192.168.1.100")
    client.connect()
    
    # Enable and start
    client.enable_motor()
    time.sleep(0.5)
    client.start_valve()
    
    # Read status
    status = client.get_status()
    print(f"Position: {status['pos_deg']:.2f}°")
    
    # Load preset (0=light, 1=medium, 2=heavy, 3=industrial)
    client.load_preset(0)
    time.sleep(2)
    
    # Update parameters
    client.update_config(viscous=0.1, coulomb=0.01)
    
    # Stream data
    streamer = SteveStreamer(client)
    
    def on_data(sample):
        print(f"Position: {sample['position_deg']:.2f}°")
    
    streamer.register_callback(on_data)
    streamer.start_stream(interval_ms=20)
    
    time.sleep(5)
    
    # Cleanup
    streamer.stop_stream()
    client.stop_valve()
    client.disconnect()
    
    print("Done!")

if __name__ == "__main__":
    main()
```

## Using Context Manager

Simplify cleanup with context manager:

```python
from pysteve import SteveClient

with SteveClient("192.168.1.100") as client:
    # Enable and start
    client.enable_motor()
    client.start_valve()
    
    # Your code here
    status = client.get_status()
    print(f"Position: {status['pos_deg']:.2f}°")
    
    # Automatic cleanup on exit
```

## Error Handling

Add error handling for robustness:

```python
from pysteve import SteveClient
from pysteve.core.exceptions import SteveConnectionError, SteveAPIError

try:
    with SteveClient("192.168.1.100") as client:
        client.enable_motor()
        client.start_valve()
        
        # Your code here
        
except SteveConnectionError as e:
    print(f"Connection error: {e}")
except SteveAPIError as e:
    print(f"API error: {e}")
except Exception as e:
    print(f"Unexpected error: {e}")
```

## Common Issues

### Connection Refused

**Problem**: `SteveConnectionError: Connection refused`

**Solution**:

- Verify device IP: `ping 192.168.1.100`
- Check device is powered on
- Ensure firewall allows ports 8080, 8888

### Motor Not Enabled

**Problem**: Valve doesn't respond

**Solution**:

- Always call `enable_motor()` before `start_valve()`
- Wait 0.5s after enabling motor

### Streaming No Data

**Problem**: Callback not receiving data

**Solution**:

- Ensure valve is started: `client.start_valve()`
- Check streaming is active: `streamer.start_stream()`
- Verify callback is registered

*For further troubleshooting, please see [client/docs/advanced/troubleshooting.md](advanced/troubleshooting.md)*

## Next Steps

Work through the advanced options in [client/docs/advanced](advanced):

- Explore options related to thread safety: [client/docs/advanced/thread-safety.md](advanced/thread-safety.md)
- Explore various integrations in [client/docs/advanced/integrations](advanced/integrations)
- Explore some tutorials in [client/docs/advanced/tutorials](advanced/tutorials)



## Additional Resources

- [Advanced Configuration](advanced/configuration.md)
- [Python Examples](../examples)
- [PyPRISM API Reference](pyprism_api_reference.md)