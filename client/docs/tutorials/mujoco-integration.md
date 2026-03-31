# MuJoCo Integration Tutorial

Learn how to integrate PRISM valves with MuJoCo physics simulation for hardware-in-the-loop testing.

## Prerequisites

- PySteve with MuJoCo extras: `pip install pysteve[mujoco]`
- MuJoCo 2.3.0 or higher
- PRISM device on network

## Overview

PySteve's MuJoCo integration provides:
- **Hardware-in-the-loop** simulation
- **State interpolation** for smooth sync
- **Latency compensation** for real-time control
- **Multi-device coordination** for complex scenes

## Sync Modes

### Simulation Mode

Simulation drives everything (no hardware):

```python
from pysteve.integrations.mujoco import SteveValveActuator
import mujoco as mj

# Load MuJoCo model
model = mj.MjModel.from_xml_path("robot.xml")
data = mj.MjData(model)

# Create actuator in simulation mode
actuator = SteveValveActuator(
    client=None,  # No hardware connection
    mj_model=model,
    mj_data=data,
    joint_name="valve_joint",
    sync_mode="simulation"
)

# Pure simulation loop
for _ in range(1000):
    mj.mj_step(model, data)
```

### Hardware Mode

Hardware drives simulation (hardware-in-the-loop):

```python
from pysteve import SteveClient
from pysteve.integrations.mujoco import SteveValveActuator

# Connect to hardware
client = SteveClient("192.168.1.100")
client.connect()
client.enable_motor()
client.start_valve()

# Create actuator in hardware mode
actuator = SteveValveActuator(
    client=client,
    mj_model=model,
    mj_data=data,
    joint_name="valve_joint",
    sync_mode="hardware",  # Hardware drives simulation
    latency_ms=50  # Compensate for network latency
)

actuator.connect()

# Hardware-driven simulation
for _ in range(1000):
    # Update from hardware (sets joint position/velocity)
    actuator.update()
    
    # Step simulation
    mj.mj_step(model, data)

actuator.disconnect()
```

### Hybrid Mode

Bidirectional sync (simulation affects hardware):

```python
actuator = SteveValveActuator(
    client=client,
    mj_model=model,
    mj_data=data,
    joint_name="valve_joint",
    sync_mode="hybrid",  # Bidirectional
    latency_ms=30
)

actuator.connect()

# Hybrid simulation
for _ in range(1000):
    # Apply control in simulation
    data.ctrl[0] = 0.1  # Torque command
    
    # Bidirectional update
    actuator.update()
    
    # Step simulation
    mj.mj_step(model, data)
```

## Building MJCF Scenes

### Basic Valve Model

```xml
<mujoco model="valve_scene">
    <option timestep="0.002" gravity="0 0 -9.81"/>
    
    <asset>
        <texture name="grid" type="2d" builtin="checker" width="512" height="512"/>
        <material name="grid" texture="grid" texrepeat="1 1"/>
    </asset>
    
    <worldbody>
        <light pos="0 0 3" dir="0 0 -1"/>
        <geom name="floor" type="plane" size="2 2 .1" material="grid"/>
        
        <!-- Valve assembly -->
        <body name="valve_base" pos="0 0 0.5">
            <geom name="base" type="cylinder" size="0.05 0.05" rgba="0.3 0.3 0.3 1"/>
            
            <body name="valve_handle" pos="0 0 0.1">
                <joint name="valve_joint" type="hinge" axis="0 0 1" 
                       limited="true" range="0 1.571"/>
                <geom name="handle" type="cylinder" size="0.02 0.08" 
                      rgba="0.8 0.2 0.2 1"/>
                <geom name="grip" type="box" size="0.1 0.02 0.02" 
                      pos="0 0 0.1" rgba="0.2 0.2 0.8 1"/>
            </body>
        </body>
    </worldbody>
    
    <actuator>
        <motor name="valve_motor" joint="valve_joint" gear="1" 
               ctrllimited="true" ctrlrange="-0.5 0.5"/>
    </actuator>
</mujoco>
```

### Using MJCF Builder

```python
from pysteve.integrations.mujoco import MJCFBuilder

builder = MJCFBuilder()

# Add valve to existing model
builder.add_valve_joint(
    body_name="valve_handle",
    joint_name="valve_joint",
    axis=(0, 0, 1),
    range_deg=(0, 90)
)

builder.add_valve_actuator(
    joint_name="valve_joint",
    gear=1.0,
    torque_range=(-0.5, 0.5)
)

# Save modified MJCF
builder.save("robot_with_valve.xml")
```

## Complete Example: Robot Manipulation

```python
import mujoco as mj
import mujoco.viewer as viewer
from pysteve import SteveClient
from pysteve.integrations.mujoco import SteveValveActuator
import numpy as np

def main():
    # Load MuJoCo model with robot and valve
    model = mj.MjModel.from_xml_path("robot_valve_scene.xml")
    data = mj.MjData(model)
    
    # Connect to PRISM
    client = SteveClient("192.168.1.100")
    client.connect()
    client.load_preset(0)  # Load light preset
    client.enable_motor()
    client.start_valve()
    
    # Create valve actuator
    valve_actuator = SteveValveActuator(
        client=client,
        mj_model=model,
        mj_data=data,
        joint_name="valve_joint",
        sync_mode="hardware",
        latency_ms=50
    )
    valve_actuator.connect()
    
    # Create viewer
    with viewer.launch_passive(model, data) as v:
        # Simulation loop
        step = 0
        while v.is_running() and step < 10000:
            # Update valve from hardware
            valve_actuator.update()
            
            # Robot control (reach for valve)
            if step < 500:
                # Move robot to valve
                data.ctrl[0:6] = compute_robot_ik(data, target_pos)
            else:
                # Manipulate valve
                data.ctrl[6] = 0.2  # Grasp
                data.ctrl[0:6] = compute_valve_twist(data)
            
            # Step simulation
            mj.mj_step(model, data)
            
            # Update viewer
            v.sync()
            
            step += 1
    
    # Cleanup
    valve_actuator.disconnect()
    client.stop_valve()
    client.disconnect()

if __name__ == "__main__":
    main()
```

## State Interpolation

PRISM streams at ~100 Hz, MuJoCo runs at 500 Hz. Interpolation smooths the sync:

```python
actuator = SteveValveActuator(
    client=client,
    mj_model=model,
    mj_data=data,
    joint_name="valve_joint",
    sync_mode="hardware",
    latency_ms=50,
    interpolation_buffer_size=100  # Last 100ms of data
)

# Actuator automatically interpolates between samples
for _ in range(1000):
    actuator.update()  # Gets interpolated state
    mj.mj_step(model, data)
```

## Multi-Device Coordination

Control multiple PRISM valves in one scene:

```python
from pysteve.integrations.mujoco import HardwareSyncController

# Create controller
controller = HardwareSyncController(model, data)

# Add devices
controller.add_device(
    device_id="valve_1",
    device_ip="192.168.1.100",
    joint_name="valve_joint_1",
    sync_mode="hardware"
)

controller.add_device(
    device_id="valve_2",
    device_ip="192.168.1.101",
    joint_name="valve_joint_2",
    sync_mode="hardware"
)

# Connect all
controller.connect_all()
controller.start_all()

# Synchronized simulation
for _ in range(1000):
    # Update all devices
    controller.update_all()
    
    # Step simulation
    mj.mj_step(model, data)
    
    # Get synchronized states
    states = controller.get_all_hardware_states()
    print(f"Valve 1: {states['valve_1']['position_deg']:.2f}°")
    print(f"Valve 2: {states['valve_2']['position_deg']:.2f}°")

# Cleanup
controller.disconnect_all()
```

## Performance Tuning

### Latency Compensation

Measure and compensate for network latency:

```python
import time

# Measure round-trip time
start = time.perf_counter()
status = client.get_status()
latency_ms = (time.perf_counter() - start) * 1000

print(f"Measured latency: {latency_ms:.1f} ms")

# Apply compensation
actuator = SteveValveActuator(
    client=client,
    mj_model=model,
    mj_data=data,
    joint_name="valve_joint",
    sync_mode="hardware",
    latency_ms=latency_ms
)
```

### Synchronization Frequency

Match MuJoCo and PRISM frequencies:

```python
# MuJoCo at 500 Hz (0.002s timestep)
model.opt.timestep = 0.002

# PRISM streaming at 100 Hz
streamer = client.streamer
streamer.start_stream(interval_ms=10)

# Actuator interpolates between samples
```

## Visualization

### Real-time Plotting

```python
from pysteve.utils import RealtimePlotter

plotter = RealtimePlotter(
    fields=["position_deg", "torque_nm"],
    max_points=500
)

# Register with actuator
actuator.register_callback(plotter.update)

# Show plot in separate thread
import threading
plot_thread = threading.Thread(target=plotter.show)
plot_thread.start()

# Run simulation
for _ in range(1000):
    actuator.update()
    mj.mj_step(model, data)
```

### Data Recording

```python
from pysteve.control import DataRecorder

recorder = DataRecorder(client, client.streamer)
recorder.start_recording()

# Run simulation
for _ in range(1000):
    actuator.update()
    mj.mj_step(model, data)

recorder.stop_recording()
recorder.export_csv("mujoco_run.csv")
```

## Troubleshooting

### Jittery Motion

**Problem**: Valve motion appears jittery in simulation

**Solution**: Enable interpolation and increase buffer:
```python
actuator = SteveValveActuator(
    ...,
    interpolation_buffer_size=200,  # Increase buffer
    latency_ms=50  # Add latency compensation
)
```

### Desynchronization

**Problem**: Simulation and hardware drift apart

**Solution**: Use hardware sync mode and verify latency:
```python
actuator = SteveValveActuator(
    ...,
    sync_mode="hardware",  # Let hardware drive
    latency_ms=50
)
```

### Performance Issues

**Problem**: Simulation runs slowly

**Solution**: 
- Reduce streaming rate: `interval_ms=20` (50 Hz)
- Disable threadsafe if single-threaded: `threadsafe=False`
- Use simulation mode for testing

## Next Steps

- [Gymnasium RL Tutorial](gymnasium-rl.md) - Train RL agents
- [Multi-Device Guide](../advanced/multi-device.md) - Advanced coordination
- [Performance Optimization](../advanced/performance.md) - Latency tuning

