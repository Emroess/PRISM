# Isaac Sim Integration Tutorial

Learn how to integrate PRISM haptic valves into NVIDIA Isaac Sim for robot simulation.

## Prerequisites

- PySteve installed with Isaac extras: `pip install pysteve[isaac]`
- NVIDIA Isaac Sim 2023.1+ installed
- PRISM device on network
- Basic USD (Universal Scene Description) knowledge helpful

## Overview

The Isaac Sim integration provides:
- **USD-based valve articulations** - Standard Isaac Sim robot components
- **Bidirectional sync** - Hardware ↔ simulation synchronization
- **Multi-valve coordination** - Manage multiple valves in one scene
- **Haptic parameter storage** - Store parameters in USD with `steve:` namespace
- **Async updates** - Non-blocking hardware communication

## Quick Start

### Basic Valve Setup

```python
from omni.isaac.core import World
from pysteve.integrations.isaac import IsaacSteveConnector

# Create Isaac Sim world
world = World()
stage = world.stage

# Connect to PRISM device
connector = IsaacSteveConnector(stage, device_ip="192.168.1.100")

# Add valve to scene
valve_prim = connector.create_valve_articulation(
    path="/World/SteveValve",
    name="valve_1"
)

# Set haptic parameters
connector.set_haptic_parameters(
    valve_prim,
    viscous=0.08,
    coulomb=0.015,
    wall_stiffness=2.0,
    wall_damping=0.2,
    travel=90.0
)

# Start hardware sync
connector.sync_to_hardware()

# Run simulation
world.reset()
for _ in range(1000):
    connector.update_async()  # Update from hardware
    world.step(render=True)
```

## USD Scene Building

### Creating Valve Articulation

The valve is represented as a revolute joint articulation:

```python
from pysteve.integrations.isaac import IsaacSteveConnector
from omni.isaac.core import World

world = World()
stage = world.stage
connector = IsaacSteveConnector(stage, device_ip="192.168.1.100")

# Create valve with physical properties
valve_prim = connector.create_valve_articulation(
    path="/World/Valve",
    name="steve_valve",
    position=(0.0, 0.0, 1.0),  # World position (x, y, z)
    orientation=(0.0, 0.0, 0.0, 1.0),  # Quaternion (x, y, z, w)
    scale=0.1,  # Valve size (meters)
    visual_color=(0.2, 0.5, 0.8)  # RGB color
)

# The articulation includes:
# - Base link (fixed)
# - Valve wheel (revolute joint)
# - Visual geometry (cylinder)
# - Collision geometry
```

### Accessing USD Attributes

Valve parameters are stored in USD with custom `steve:` namespace:

```python
from pxr import UsdGeom, Usd

# Get valve prim
valve_prim = stage.GetPrimAtPath("/World/Valve")

# Read haptic parameters
viscous_attr = valve_prim.GetAttribute("steve:viscous")
viscous = viscous_attr.Get()

coulomb_attr = valve_prim.GetAttribute("steve:coulomb")
coulomb = coulomb_attr.Get()

# Modify parameters
viscous_attr.Set(0.12)
coulomb_attr.Set(0.018)

# Get joint reference
joint_prim = stage.GetPrimAtPath("/World/Valve/ValveJoint")
joint = UsdPhysics.RevoluteJoint(joint_prim)
```

### Manual USD Scene Creation

For advanced users, create valve manually in USD:

```python
from pxr import Usd, UsdGeom, UsdPhysics, Gf

stage = world.stage

# Create Xform (transform) for valve
valve_xform = UsdGeom.Xform.Define(stage, "/World/ManualValve")
valve_xform.AddTranslateOp().Set(Gf.Vec3d(0, 0, 1))

# Create base (fixed)
base = UsdGeom.Cylinder.Define(stage, "/World/ManualValve/Base")
base.GetRadiusAttr().Set(0.05)
base.GetHeightAttr().Set(0.02)
UsdPhysics.RigidBodyAPI.Apply(base.GetPrim())

# Create valve wheel (movable)
wheel = UsdGeom.Cylinder.Define(stage, "/World/ManualValve/Wheel")
wheel.GetRadiusAttr().Set(0.08)
wheel.GetHeightAttr().Set(0.01)
wheel.AddTranslateOp().Set(Gf.Vec3d(0, 0, 0.015))
UsdPhysics.RigidBodyAPI.Apply(wheel.GetPrim())

# Create revolute joint
joint = UsdPhysics.RevoluteJoint.Define(stage, "/World/ManualValve/Joint")
joint.CreateBody0Rel().SetTargets([base.GetPath()])
joint.CreateBody1Rel().SetTargets([wheel.GetPath()])
joint.CreateAxisAttr().Set("Z")

# Add PRISM custom attributes
wheel_prim = wheel.GetPrim()
wheel_prim.CreateAttribute("steve:viscous", Sdf.ValueTypeNames.Float).Set(0.08)
wheel_prim.CreateAttribute("steve:coulomb", Sdf.ValueTypeNames.Float).Set(0.015)
wheel_prim.CreateAttribute("steve:wall_stiffness", Sdf.ValueTypeNames.Float).Set(2.0)
wheel_prim.CreateAttribute("steve:wall_damping", Sdf.ValueTypeNames.Float).Set(0.2)
wheel_prim.CreateAttribute("steve:travel", Sdf.ValueTypeNames.Float).Set(90.0)
```

## Synchronization Modes

### Hardware-to-Simulation Sync

Read from hardware, drive simulation:

```python
connector = IsaacSteveConnector(stage, device_ip="192.168.1.100")
connector.create_valve_articulation("/World/Valve", "valve_1")

# Enable hardware → simulation sync
connector.sync_to_hardware()

# In simulation loop
world.reset()
for _ in range(1000):
    # Read hardware, update USD
    connector.update_async()
    
    # Step simulation
    world.step(render=True)
```

The valve in Isaac Sim will mirror the physical PRISM device.

### Simulation-to-Hardware Sync

Drive hardware from simulation (teleoperation mode):

```python
connector = IsaacSteveConnector(stage, device_ip="192.168.1.100")
valve_prim = connector.create_valve_articulation("/World/Valve", "valve_1")

# Enable simulation → hardware sync
connector.sync_from_simulation(valve_prim)

# In simulation loop
for _ in range(1000):
    # Apply forces/torques in simulation
    # (e.g., robot manipulating valve)
    
    # Step simulation
    world.step(render=True)
    
    # Push simulation state to hardware
    connector.update_from_simulation()
```

### Bidirectional Sync

Both directions (hybrid mode):

```python
# Setup bidirectional sync
connector.sync_bidirectional(valve_prim)

for _ in range(1000):
    # Read hardware position
    connector.update_from_hardware()
    
    # Simulation computes forces
    world.step(render=True)
    
    # Send computed torques to hardware
    connector.update_torques_to_hardware()
```

## Complete Robot Manipulation Example

Robot opening a valve in simulation with hardware feedback:

```python
from omni.isaac.core import World
from omni.isaac.core.robots import Robot
from omni.isaac.core.utils.stage import add_reference_to_stage
from pysteve.integrations.isaac import IsaacSteveConnector
import numpy as np

# Create world
world = World()
stage = world.stage

# Add robot (e.g., Franka Panda)
add_reference_to_stage(
    usd_path="/path/to/franka.usd",
    prim_path="/World/Franka"
)
robot = Robot(prim_path="/World/Franka")
world.scene.add(robot)

# Add PRISM valve
connector = IsaacSteveConnector(stage, device_ip="192.168.1.100")
valve_prim = connector.create_valve_articulation(
    path="/World/Valve",
    name="valve_1",
    position=(0.5, 0.0, 0.5)  # In front of robot
)

connector.set_haptic_parameters(
    valve_prim,
    viscous=0.1,
    coulomb=0.015,
    wall_stiffness=2.0,
    wall_damping=0.2,
    travel=90.0
)

# Start hardware sync
connector.sync_to_hardware()

# Reset world
world.reset()

# Get end-effector controller
from omni.isaac.core.controllers import BaseController

class ValveGraspController(BaseController):
    def __init__(self, robot):
        super().__init__("valve_grasp")
        self.robot = robot
        self.phase = "approach"
        self.steps = 0
    
    def forward(self, valve_position):
        self.steps += 1
        
        if self.phase == "approach":
            # Move to valve
            target = valve_position + np.array([0, 0, 0.15])
            if np.linalg.norm(self.robot.get_world_pose()[0] - target) < 0.01:
                self.phase = "grasp"
            return {"target_position": target}
        
        elif self.phase == "grasp":
            # Close gripper
            if self.steps > 50:
                self.phase = "rotate"
            return {"gripper": 0.0}  # Close
        
        elif self.phase == "rotate":
            # Rotate valve
            angle = min(90, (self.steps - 100) * 0.5)
            return {"valve_rotation": angle}
        
        return {}

controller = ValveGraspController(robot)

# Simulation loop
for i in range(2000):
    # Update hardware state
    connector.update_async()
    
    # Control robot
    valve_pos = connector.get_valve_position(valve_prim)
    actions = controller.forward(valve_pos)
    
    # Apply actions
    if "target_position" in actions:
        robot.set_world_pose(position=actions["target_position"])
    
    # Step simulation
    world.step(render=True)
    
    # Log data
    if i % 100 == 0:
        steve_angle = connector.get_valve_angle(valve_prim)
        print(f"Step {i}: Valve angle = {steve_angle:.1f}°")
```

## Multi-Valve Coordination

Manage multiple PRISM devices in one scene:

```python
from pysteve.integrations.isaac import MultiSteveManager

# Create manager
manager = MultiSteveManager(stage)

# Add multiple valves
valve1 = manager.add_valve(
    device_ip="192.168.1.100",
    path="/World/Valve1",
    name="valve_1",
    position=(0.5, 0.0, 0.5)
)

valve2 = manager.add_valve(
    device_ip="192.168.1.101",
    path="/World/Valve2",
    name="valve_2",
    position=(0.5, 0.3, 0.5)
)

valve3 = manager.add_valve(
    device_ip="192.168.1.102",
    path="/World/Valve3",
    name="valve_3",
    position=(0.5, -0.3, 0.5)
)

# Configure all valves
manager.set_all_parameters(
    viscous=0.08,
    coulomb=0.015,
    wall_stiffness=2.0
)

# Or configure individually
manager.set_parameters(
    "valve_1",
    viscous=0.05  # Easier to turn
)

# Start sync for all
manager.sync_all_to_hardware()

# Update all in parallel
world.reset()
for _ in range(1000):
    manager.update_all_async()
    world.step(render=True)

# Get data from specific valve
angle1 = manager.get_valve_angle("valve_1")
torque2 = manager.get_valve_torque("valve_2")
```

## Data Recording in Isaac Sim

Record valve data during simulation:

```python
from pysteve.integrations.isaac import IsaacSteveConnector
from pysteve.control import DataRecorder

connector = IsaacSteveConnector(stage, device_ip="192.168.1.100")
valve_prim = connector.create_valve_articulation("/World/Valve", "valve_1")
connector.sync_to_hardware()

# Create recorder
recorder = DataRecorder(
    connector.steve_client,
    connector.steve_client.streamer
)

# Start recording
recorder.start_recording()

# Run simulation
world.reset()
for i in range(1000):
    connector.update_async()
    world.step(render=True)

# Stop and export
recorder.stop_recording()
recorder.export_csv("isaac_sim_recording.csv")

print(f"Recorded {len(recorder.data)} samples")
```

## Performance Optimization

### Async Updates

For multiple valves, update asynchronously:

```python
import asyncio

async def update_valves():
    tasks = [
        connector1.update_async(),
        connector2.update_async(),
        connector3.update_async()
    ]
    await asyncio.gather(*tasks)

# In simulation loop
for _ in range(1000):
    asyncio.run(update_valves())
    world.step(render=True)
```

### Update Rate Control

Match Isaac Sim timestep:

```python
# Isaac Sim at 60 Hz, PRISM at 100 Hz
# Update every 1-2 sim steps

connector.sync_to_hardware()

step_count = 0
for _ in range(1000):
    if step_count % 2 == 0:  # Update every 2 steps
        connector.update_async()
    
    world.step(render=True)
    step_count += 1
```

### Interpolation

Smooth hardware updates:

```python
from pysteve.integrations.isaac import InterpolatedConnector

connector = InterpolatedConnector(
    stage,
    device_ip="192.168.1.100",
    interpolation_window=10  # Interpolate over 10 samples
)

# Updates are automatically interpolated
for _ in range(1000):
    connector.update_interpolated()
    world.step(render=True)
```

## Camera and Sensors

Add cameras to observe valve manipulation:

```python
from omni.isaac.core.utils.viewports import set_camera_view
from omni.isaac.sensor import Camera

# Create camera
camera = Camera(
    prim_path="/World/Camera",
    position=np.array([0.8, 0.3, 0.7]),
    frequency=30
)

# Point at valve
valve_pos = connector.get_valve_position(valve_prim)
camera.set_world_pose(position=[0.8, 0.3, 0.7])
camera.set_focal_length(24.0)

# Initialize
camera.initialize()

# Capture images
world.reset()
for i in range(100):
    connector.update_async()
    world.step(render=True)
    
    # Get camera data
    if i % 10 == 0:
        rgba = camera.get_rgba()
        depth = camera.get_depth()
        
        # Save or process images
        camera.save_image(f"frame_{i:04d}.png")
```

## Troubleshooting

### Valve Not Moving in Simulation

**Problem**: Hardware updates not reflected in USD

**Solution**: Check sync mode and update calls
```python
# Ensure sync is enabled
connector.sync_to_hardware()

# Ensure update is called every frame
for _ in range(100):
    connector.update_async()  # Must call this
    world.step(render=True)
```

### Connection Timeout

**Problem**: Can't connect to PRISM device

**Solution**: Check network and device status
```python
# Test connection first
from pysteve import SteveClient

client = SteveClient("192.168.1.100")
try:
    client.connect()
    print("Connection OK")
except Exception as e:
    print(f"Connection failed: {e}")
```

### Performance Issues

**Problem**: Simulation running slowly with PRISM sync

**Solution**: Reduce update frequency
```python
# Update every N steps
update_interval = 5
for i in range(1000):
    if i % update_interval == 0:
        connector.update_async()
    world.step(render=True)
```

### Joint Limits Not Working

**Problem**: Valve rotates beyond travel limits

**Solution**: Set USD joint limits
```python
from pxr import UsdPhysics

joint_prim = stage.GetPrimAtPath("/World/Valve/ValveJoint")
joint = UsdPhysics.RevoluteJoint(joint_prim)

# Set limits (in degrees)
joint.CreateLowerLimitAttr().Set(-45.0)
joint.CreateUpperLimitAttr().Set(45.0)
```

## Next Steps

- [MuJoCo Integration](mujoco-integration.md) - Compare with MuJoCo approach
- [Multi-Device Coordination](../advanced/multi-device.md) - Advanced multi-valve control
- [ROS Integration](ros-integration.md) - Connect Isaac Sim to ROS
- [Data Recording Tutorial](data-recording.md) - Analyze simulation data

## Resources

- [Isaac Sim Documentation](https://docs.omniverse.nvidia.com/app_isaacsim/app_isaacsim.html)
- [USD Documentation](https://graphics.pixar.com/usd/docs/index.html)
- [PySteve API Reference](../api/integrations.md#isaac-sim)

