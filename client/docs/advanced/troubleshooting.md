# Troubleshooting Guide

Common issues and solutions when using PySteve.

## Connection Issues

### Cannot Connect to Device

**Symptom**: `ConnectionError: Could not connect to 192.168.1.100:5000`

**Possible causes**:

1. **Device not powered on**
   ```bash
   # Check if device is reachable
   ping 192.168.1.100
   ```

2. **Wrong IP address**
   ```python
   # Verify IP in device documentation or:
   from pysteve.utils import discover_devices
   devices = discover_devices()
   print(devices)
   ```

3. **Firewall blocking connection**
   ```bash
   # Linux: Check firewall
   sudo iptables -L
   
   # Allow port 5000 and 5001
   sudo iptables -A INPUT -p tcp --dport 5000 -j ACCEPT
   sudo iptables -A INPUT -p tcp --dport 5001 -j ACCEPT
   ```

4. **Network configuration**
   ```bash
   # Ensure same subnet
   ip addr show
   # Device: 192.168.1.100
   # Computer should be: 192.168.1.x
   ```

### Connection Drops Randomly

**Symptom**: Connection works, then fails after some time

**Solutions**:

1. **Enable auto-reconnect**
   ```python
   client = SteveClient(
       "192.168.1.100",
       auto_reconnect=True,
       max_retries=5,
       retry_delay=2.0
   )
   ```

2. **Check network stability**
   ```bash
   # Monitor connection
   ping -i 0.2 192.168.1.100
   ```

3. **Increase timeout**
   ```python
   client = SteveClient("192.168.1.100", timeout=10.0)
   ```

### Timeout Errors

**Symptom**: `TimeoutError: Request timeout after 5.0 seconds`

**Solutions**:

1. **Increase timeout**
   ```python
   client = SteveClient("192.168.1.100", timeout=10.0)
   ```

2. **Check network latency**
   ```python
   latency_ms = client.ping()
   print(f"Latency: {latency_ms:.2f} ms")
   ```

3. **Reduce request frequency**
   ```python
   # Instead of:
   for _ in range(100):
       status = client.get_status()  # Too frequent!
   
   # Use streaming:
   client.start_streaming(rate_hz=50)
   ```

## Motor and Valve Issues

### Motor Won't Enable

**Symptom**: `SteveError: Motor enable failed`

**Solutions**:

1. **Check device status**
   ```python
   status = client.get_status()
   print(status)
   # Look for error flags
   ```

2. **Reset device** (if supported)
   ```python
   client.reset()
   time.sleep(2)
   client.enable_motor()
   ```

3. **Check safety interlocks**
   - Verify emergency stop is not engaged
   - Check power supply voltage
   - Inspect physical connections

### Valve Won't Start

**Symptom**: `SteveError: Valve start failed`

**Cause**: Motor not enabled

**Solution**:
```python
# Enable motor first
client.enable_motor()
time.sleep(0.5)  # Wait for motor to stabilize

# Then start valve
client.start_valve()
```

### Valve Feels Unstable

**Symptom**: Oscillations, vibrations, or jittery motion

**Solutions**:

1. **Increase damping**
   ```python
   client.update_config(
       viscous=0.15,     # Higher damping
       smoothing=0.002   # More smoothing
   )
   ```

2. **Reduce torque limit**
   ```python
   client.update_config(torque_limit=0.3)
   ```

3. **Check control frequency**
   ```python
   # Ensure device running at correct frequency
   # (usually 1000 Hz, check firmware docs)
   ```

### Valve Doesn't Respond

**Symptom**: Parameters update but feel doesn't change

**Cause**: Valve not running

**Solution**:
```python
# Check if valve is actually running
if not client.is_valve_running():
    client.start_valve()

# Then update parameters
client.update_config(viscous=0.08)
```

## Streaming Issues

### No Streaming Data

**Symptom**: Callback never called

**Solutions**:

1. **Start streaming explicitly**
   ```python
   client.start_streaming()  # Don't forget this!
   client.streamer.register_callback(my_callback)
   ```

2. **Check callback registration**
   ```python
   def my_callback(sample):
       print(sample)
   
   # Register AFTER starting streaming
   client.start_streaming()
   client.streamer.register_callback(my_callback)
   ```

3. **Verify callback signature**
   ```python
   # CORRECT
   def callback(sample):
       print(sample)
   
   # WRONG
   def callback():  # Missing sample parameter!
       print("data")
   ```

### Streaming Lag/Delay

**Symptom**: Data arrives late

**Solutions**:

1. **Reduce streaming rate**
   ```python
   # Try lower rate
   client.start_streaming(rate_hz=50)  # Instead of 100
   ```

2. **Optimize callback**
   ```python
   # Slow callback
   def slow_callback(sample):
       time.sleep(0.1)  # BAD!
       process(sample)
   
   # Fast callback with queue
   import queue
   q = queue.Queue()
   
   def fast_callback(sample):
       q.put_nowait(sample)  # Fast
   
   def worker():
       while True:
           sample = q.get()
           process(sample)  # Slow processing in separate thread
   
   threading.Thread(target=worker, daemon=True).start()
   ```

3. **Check network**
   ```python
   latency = client.ping()
   if latency > 10:
       print(f"High latency: {latency:.2f} ms")
   ```

### Missing Samples

**Symptom**: Gaps in streaming data

**Solutions**:

1. **Increase buffer size**
   ```python
   from pysteve.utils import StreamBuffer
   
   buffer = StreamBuffer(max_size=10000)  # Larger buffer
   client.streamer.register_callback(buffer.append)
   ```

2. **Check network bandwidth**
   ```bash
   # Monitor network usage
   iftop -i eth0
   ```

3. **Reduce other network traffic**

## Configuration Issues

### Invalid Parameter Values

**Symptom**: `ValueError: Parameter out of range`

**Solution**: Check parameter limits
```python
# Valid ranges:
client.update_config(
    viscous=0.1,        # 0.01 - 0.5
    coulomb=0.015,      # 0.005 - 0.05
    wall_stiffness=2.0, # 0.5 - 5.0
    wall_damping=0.2,   # 0.05 - 0.5
    smoothing=0.001,    # 0.0001 - 0.01
    torque_limit=0.5    # 0.1 - 2.0
)
```

### Preset Not Found

**Symptom**: `SteveValidationError: preset must be an integer from 0 to 3`

**Solution**: Use valid preset indices (0-3)
```python
# Valid presets
client.load_preset(0)  # Light (butterfly/faucet)
client.load_preset(1)  # Medium (ball valve)
client.load_preset(2)  # Heavy (gate valve)
client.load_preset(3)  # Industrial (globe/gas)

# Check available presets
presets = client.get_presets()
for p in presets:
    print(f"{p.index}: {p.name}")
```

### Configuration Not Applied

**Symptom**: Update called but parameters don't change

**Cause**: Valve not running

**Solution**:
```python
# Start valve first
client.start_valve()
time.sleep(0.5)  # Let it stabilize

# Then update
client.update_config(viscous=0.08)
```

## Integration Issues

### MuJoCo Sync Issues

**Symptom**: Simulation and hardware out of sync

**Solutions**:

1. **Check sync mode**
   ```python
   actuator = SteveValveActuator(
       steve_ip="192.168.1.100",
       sync_mode="hardware",  # Correct mode?
       target_hz=1000
   )
   ```

2. **Increase latency compensation**
   ```python
   actuator = SteveValveActuator(
       steve_ip="192.168.1.100",
       latency_compensation_ms=100  # Higher value
   )
   ```

3. **Use interpolation**
   ```python
   actuator.enable_interpolation(window_size=10)
   ```

### Gymnasium Environment Issues

**Symptom**: Training not converging

**Solutions**:

1. **Check reward function**
   ```python
   env = SteveValveEnv(
       steve_ip="192.168.1.100",
       reward_function="smooth_operation"  # Try different functions
   )
   ```

2. **Adjust termination conditions**
   ```python
   env = SteveValveEnv(
       steve_ip="192.168.1.100",
       terminate_on_torque_limit=False,  # More lenient
       max_steps=2000  # Longer episodes
   )
   ```

3. **Normalize observations**
   ```python
   from stable_baselines3.common.vec_env import VecNormalize
   
   env = VecNormalize(env, norm_obs=True, norm_reward=True)
   ```

### Isaac Sim Connection Issues

**Symptom**: Connector can't sync with hardware

**Solutions**:

1. **Check USD attributes**
   ```python
   # Verify steve: namespace attributes exist
   valve_prim = stage.GetPrimAtPath("/World/Valve")
   print(valve_prim.GetAttribute("steve:viscous").Get())
   ```

2. **Ensure sync is enabled**
   ```python
   connector.sync_to_hardware()  # Don't forget to call this!
   ```

3. **Update every frame**
   ```python
   for _ in range(1000):
       connector.update_async()  # Must call each frame
       world.step(render=True)
   ```

## Performance Issues

### Slow API Calls

**Symptom**: `get_status()` takes >100ms

**Solutions**:

1. **Use streaming instead**
   ```python
   # Instead of polling:
   while True:
       status = client.get_status()  # Slow!
       time.sleep(0.01)
   
   # Use streaming:
   def callback(sample):
       print(sample)
   
   client.start_streaming(rate_hz=100)
   client.streamer.register_callback(callback)
   ```

2. **Check network latency**
   ```python
   latency = client.ping()
   print(f"Latency: {latency:.2f} ms")
   ```

3. **Use async client for concurrency**
   ```python
   import asyncio
   from pysteve import SteveAsyncClient
   
   async with SteveAsyncClient("192.168.1.100") as client:
       # Concurrent requests
       results = await asyncio.gather(
           client.get_status(),
           client.get_config(),
           client.get_position()
       )
   ```

### High CPU Usage

**Symptom**: Python process using >50% CPU

**Solutions**:

1. **Reduce polling frequency**
   ```python
   # Instead of:
   while True:
       status = client.get_status()  # Busy loop!
   
   # Add sleep:
   while True:
       status = client.get_status()
       time.sleep(0.01)  # 100Hz
   ```

2. **Optimize callbacks**
   ```python
   def optimized_callback(sample):
       # Process only what you need
       position = sample['position_deg']
       # Skip expensive operations
   ```

3. **Use DataRecorder instead of manual recording**
   ```python
   # Instead of:
   data = []
   def callback(sample):
       data.append(sample)  # Memory grows!
   
   # Use:
   from pysteve.control import DataRecorder
   recorder = DataRecorder(client, client.streamer)
   recorder.start_recording()
   ```

## Data Recording Issues

### No Data Recorded

**Symptom**: `len(recorder.data) == 0`

**Solutions**:

1. **Start streaming**
   ```python
   client.start_streaming()  # Must start streaming!
   recorder.start_recording()
   time.sleep(5)
   recorder.stop_recording()
   ```

2. **Check recording state**
   ```python
   print(f"Recording: {recorder.is_recording}")
   ```

3. **Verify callback registered**
   ```python
   # DataRecorder auto-registers, but check:
   recorder = DataRecorder(client, client.streamer)
   # It registers callback internally
   ```

### Export Fails

**Symptom**: File write error

**Solutions**:

1. **Check permissions**
   ```bash
   # Verify write permission
   touch test.csv
   rm test.csv
   ```

2. **Check disk space**
   ```bash
   df -h
   ```

3. **Use absolute path**
   ```python
   import os
   path = os.path.expanduser("~/data/recording.csv")
   recorder.export_csv(path)
   ```

## Python/Environment Issues

### Import Errors

**Symptom**: `ModuleNotFoundError: No module named 'pysteve'`

**Solution**:
```bash
# Verify installation
pip list | grep pysteve

# Reinstall if needed
pip install --upgrade pysteve
```

### Version Compatibility

**Symptom**: Unexpected API behavior

**Solution**:
```python
import pysteve
print(f"PySteve version: {pysteve.__version__}")

# Check compatibility
# PySteve 0.1.x requires Python 3.8+
import sys
print(f"Python: {sys.version}")
```

### Dependency Issues

**Symptom**: Optional features not working

**Solution**:
```bash
# Install specific extras
pip install pysteve[mujoco]
pip install pysteve[gymnasium]
pip install pysteve[async]
pip install pysteve[data]
pip install pysteve[viz]

# Or install all
pip install pysteve[all]
```

## Getting Help

If none of these solutions work:

1. **Check logs** (if enabled)
   ```python
   import logging
   logging.basicConfig(level=logging.DEBUG)
   ```

2. **Enable verbose mode**
   ```python
   client = SteveClient("192.168.1.100", verbose=True)
   ```

3. **Minimal reproducible example**
   ```python
   from pysteve import SteveClient
   
   client = SteveClient("192.168.1.100")
   client.connect()
   print(client.get_status())
   ```

4. **Report issue**
   - GitHub Issues: https://github.com/yourusername/steve_can/issues
   - Include: PySteve version, Python version, error traceback, minimal code

5. **Community support**
   - Discussions: https://github.com/yourusername/steve_can/discussions
   - Documentation: https://pysteve.readthedocs.io

