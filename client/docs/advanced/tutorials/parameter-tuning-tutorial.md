# Parameter Tuning Tutorial

Learn how to adjust PRISM valve parameters in real-time for optimal haptic feel.

## Prerequisites

- PySteve installed: `pip install pysteve`
- PRISM device on network
- Basic understanding of haptic parameters

## Parameters Overview

### Viscous Damping (`viscous`)

**Range**: 0.01 - 0.5 N·m·s/rad

Opposes velocity. Higher values make the valve feel "thicker" or "heavier" to turn.

```python
from pysteve import SteveClient
from pysteve.control import RealtimeTuner

client = SteveClient("192.168.1.100")
client.connect()
client.enable_motor()
client.start_valve()

tuner = RealtimeTuner(client)

# Low viscosity (easy to turn, feels "light")

## Table of Contents

- [Prerequisites](#prerequisites)
- [Parameters Overview](#parameters-overview)
  - [Viscous Damping (`viscous`)](#viscous-damping-viscous)
  - [Coulomb Friction (`coulomb`)](#coulomb-friction-coulomb)
  - [Wall Stiffness (`wall_stiffness`)](#wall-stiffness-wall_stiffness)
  - [Wall Damping (`wall_damping`)](#wall-damping-wall_damping)
  - [Smoothing (`smoothing`)](#smoothing-smoothing)
  - [Torque Limit (`torque_limit`)](#torque-limit-torque_limit)
- [Real-Time Tuning Workflow](#real-time-tuning-workflow)
  - [Interactive Tuning Session](#interactive-tuning-session)
  - [Batch Parameter Updates](#batch-parameter-updates)
  - [Parameter Sweeps](#parameter-sweeps)
- [Finding Optimal Parameters](#finding-optimal-parameters)
  - [Goal: Smooth Operation](#goal-smooth-operation)
  - [Goal: Realistic Mechanical Feel](#goal-realistic-mechanical-feel)
  - [Goal: Precise Control](#goal-precise-control)
  - [Goal: Training/Resistance](#goal-trainingresistance)
- [Data-Driven Tuning](#data-driven-tuning)
  - [Objective: Minimize Velocity Variance](#objective-minimize-velocity-variance)
  - [Objective: Maximize Energy Efficiency](#objective-maximize-energy-efficiency)
- [Saving and Loading Configurations](#saving-and-loading-configurations)
  - [Save to Preset](#save-to-preset)
  - [Export to File](#export-to-file)
- [Best Practices](#best-practices)
  - [1. Always Start with a Preset](#1-always-start-with-a-preset)
  - [2. Change One Parameter at a Time](#2-change-one-parameter-at-a-time)
  - [3. Record Your Changes](#3-record-your-changes)
  - [4. Validate with Users](#4-validate-with-users)
- [Troubleshooting](#troubleshooting)
  - [Valve Feels Unstable](#valve-feels-unstable)
  - [Valve Too Sensitive](#valve-too-sensitive)
  - [Walls Feel Bouncy](#walls-feel-bouncy)
- [Next Steps](#next-steps)


tuner.set_viscous(0.05)

# Medium viscosity (balanced)
tuner.set_viscous(0.1)

# High viscosity (hard to turn, feels "thick")
tuner.set_viscous(0.3)
```

**Use cases**:
- Low: Delicate operations, fine control
- Medium: General purpose manipulation
- High: Heavy machinery simulation, damped feel

### Coulomb Friction (`coulomb`)

**Range**: 0.005 - 0.05 N·m

Constant friction torque independent of velocity. Creates "sticky" or "notchy" feel.

```python
# Low friction (smooth, no stiction)
tuner.set_coulomb(0.005)

# Medium friction (slight resistance)
tuner.set_coulomb(0.015)

# High friction (sticky, requires force to start)
tuner.set_coulomb(0.03)
```

**Use cases**:
- Low: Smooth operation, minimal resistance
- Medium: Realistic mechanical feel
- High: Old/worn valve simulation, deliberate control

### Wall Stiffness (`wall_stiffness`)

**Range**: 0.5 - 5.0 N·m/turn

Spring stiffness at endpoints. Higher values create "harder" stops.

```python
# Soft walls (gentle stops)
tuner.set_wall_stiffness(0.8)

# Medium walls (firm stops)
tuner.set_wall_stiffness=2.0)

# Hard walls (rigid stops)
tuner.set_wall_stiffness(4.0)
```

**Use cases**:
- Soft: Compliant interfaces, gentle feedback
- Medium: Standard mechanical stops
- Hard: Safety-critical hard limits

### Wall Damping (`wall_damping`)

**Range**: 0.05 - 0.5 N·m·s/turn

Damping at wall contacts. Prevents bouncing at endpoints.

```python
# Low damping (can bounce)
tuner.set_wall_damping(0.05)

# Medium damping (slight bounce)
tuner.set_wall_damping(0.2)

# High damping (no bounce, dead stop)
tuner.set_wall_damping(0.4)
```

### Smoothing (`smoothing`)

**Range**: 0.0001 - 0.01

Regularization for numerical stability. Rarely needs adjustment.

```python
# Standard value
tuner.set_smoothing(0.001)
```

### Torque Limit (`torque_limit`)

**Range**: 0.1 - 2.0 N·m

Maximum torque output. Safety limit.

```python
# Low limit (gentle)
tuner.set_torque_limit(0.3)

# Medium limit
tuner.set_torque_limit(0.5)

# High limit (strong feedback)
tuner.set_torque_limit(1.0)
```

## Real-Time Tuning Workflow

### Interactive Tuning Session

```python
from pysteve import SteveClient
from pysteve.control import RealtimeTuner
from pysteve.utils import RealtimePlotter
import time

# Connect and start
client = SteveClient("192.168.1.100")
client.connect()
client.load_preset(1)  # Load medium preset
client.enable_motor()
client.start_valve()

# Create tuner
tuner = RealtimeTuner(client)

# Setup real-time plotting
plotter = RealtimePlotter(fields=["position_deg", "torque_nm"])
client.streamer.register_callback(plotter.update)

import threading
plot_thread = threading.Thread(target=plotter.show, kwargs={"block": False})
plot_thread.daemon = True
plot_thread.start()

print("=== Interactive Tuning Session ===")
print("Manually turn the valve and observe the response.")
print()

# Test viscous damping
print("Testing viscous damping...")
for viscous in [0.05, 0.1, 0.15, 0.2]:
    print(f"  viscous = {viscous:.2f}")
    tuner.set_viscous(viscous)
    time.sleep(5)  # Try turning the valve

# Test coulomb friction
print("\nTesting coulomb friction...")
for coulomb in [0.005, 0.01, 0.02, 0.03]:
    print(f"  coulomb = {coulomb:.3f}")
    tuner.set_coulomb(coulomb)
    time.sleep(5)

# Test wall stiffness
print("\nTesting wall stiffness...")
for stiff in [1.0, 2.0, 3.0, 4.0]:
    print(f"  wall_stiffness = {stiff:.1f}")
    tuner.set_wall_stiffness(stiff)
    time.sleep(5)  # Try hitting the walls

print("\nTuning session complete!")
```

### Batch Parameter Updates

Update multiple parameters atomically:

```python
# Update all at once (atomic operation)
tuner.update_multiple(
    viscous=0.08,
    coulomb=0.012,
    wall_stiffness=2.0,
    wall_damping=0.2
)
```

### Parameter Sweeps

Systematically test parameter ranges:

```python
from pysteve.control import ParameterSweep

sweep = ParameterSweep(client)

# Sweep viscous damping
results = sweep.sweep_range(
    parameter="viscous",
    start=0.05,
    end=0.3,
    steps=10,
    duration_per_step=5.0
)

# Analyze results
for result in results:
    print(f"viscous={result['value']:.2f}: "
          f"mean_vel={result['metrics']['vel_mean']:.3f}, "
          f"std_vel={result['metrics']['vel_std']:.3f}")

# Export to CSV
sweep.export_results("viscous_sweep.csv", results)
```

## Finding Optimal Parameters

### Goal: Smooth Operation

For smooth, easy-to-turn valve:

```python
tuner.update_multiple(
    viscous=0.05,      # Low damping
    coulomb=0.005,     # Minimal friction
    wall_stiffness=1.0,  # Soft walls
    wall_damping=0.15
)
```

### Goal: Realistic Mechanical Feel

For realistic industrial valve:

```python
tuner.update_multiple(
    viscous=0.12,      # Medium damping
    coulomb=0.018,     # Noticeable friction
    wall_stiffness=2.5,  # Firm walls
    wall_damping=0.25
)
```

### Goal: Precise Control

For fine positioning tasks:

```python
tuner.update_multiple(
    viscous=0.15,      # Higher damping for stability
    coulomb=0.008,     # Low friction for smoothness
    wall_stiffness=3.0,  # Firm stops
    wall_damping=0.3     # Dead stops
)
```

### Goal: Training/Resistance

For exercise or training applications:

```python
tuner.update_multiple(
    viscous=0.25,      # High resistance
    coulomb=0.025,     # Significant friction
    wall_stiffness=2.0,
    wall_damping=0.2
)
```

## Data-Driven Tuning

### Objective: Minimize Velocity Variance

```python
from pysteve.control import ParameterSweep
import numpy as np

sweep = ParameterSweep(client)

# Grid search
results = sweep.sweep_grid(
    parameters={
        "viscous": np.linspace(0.05, 0.2, 5),
        "coulomb": np.linspace(0.005, 0.02, 4)
    },
    duration_per_step=3.0
)

# Find configuration with lowest velocity variance
best = min(results, key=lambda r: r['metrics']['vel_std'])

print(f"Best configuration:")
print(f"  viscous: {best['viscous']:.3f}")
print(f"  coulomb: {best['coulomb']:.3f}")
print(f"  velocity std: {best['metrics']['vel_std']:.3f}")

# Apply best configuration
tuner.update_multiple(**best['config'])
```

### Objective: Maximize Energy Efficiency

```python
# Minimize average power
best = min(results, key=lambda r: abs(r['metrics']['power_mean']))

print(f"Most efficient configuration:")
print(f"  viscous: {best['viscous']:.3f}")
print(f"  coulomb: {best['coulomb']:.3f}")
print(f"  power: {best['metrics']['power_mean']:.3f} W")
```

## Saving and Loading Configurations

### Save to Preset

```python
from pysteve.core.config import ValveConfig

# Create custom configuration
config = ValveConfig(
    viscous=0.08,
    coulomb=0.012,
    wall_stiffness=2.0,
    wall_damping=0.2,
    smoothing=0.001,
    torque_limit=0.5,
    travel=90
)

# Save to preset slot 3 (custom preset)
client.save_preset(3, config)

# Later, load it back
client.load_preset(3)
```

### Export to File

```python
import json

# Get current configuration
config = client.get_config()

# Save to JSON
with open("my_valve_config.json", "w") as f:
    json.dump(config.to_dict(), f, indent=2)

# Load from JSON
with open("my_valve_config.json") as f:
    config_dict = json.load(f)

config = ValveConfig.from_dict(config_dict)
client.update_config(**config.to_dict())
```

## Best Practices

### 1. Always Start with a Preset

```python
# Start with closest preset (0=light, 1=medium, 2=heavy, 3=industrial)
client.load_preset(1)

# Then tune incrementally
tuner.set_viscous(0.12)
```

### 2. Change One Parameter at a Time

```python
# Good: Isolate effects
tuner.set_viscous(0.1)
time.sleep(5)  # Test
tuner.set_coulomb(0.015)
time.sleep(5)  # Test

# Avoid: Changing multiple parameters
tuner.update_multiple(viscous=0.1, coulomb=0.015, wall_stiffness=3.0)
# Hard to identify which parameter has what effect
```

### 3. Record Your Changes

```python
from pysteve.control import DataRecorder

recorder = DataRecorder(client, client.streamer)

# Record baseline (0=light, 1=medium, 2=heavy, 3=industrial)
client.load_preset(1)
recorder.start_recording()
time.sleep(5)
recorder.stop_recording()
recorder.export_csv("baseline.csv")

# Record after tuning
tuner.set_viscous(0.08)
recorder.start_recording()
time.sleep(5)
recorder.stop_recording()
recorder.export_csv("tuned.csv")
```

### 4. Validate with Users

```python
# A/B testing
configs = {
    "A": {"viscous": 0.08, "coulomb": 0.01},
    "B": {"viscous": 0.12, "coulomb": 0.015}
}

for name, config in configs.items():
    print(f"\nTesting configuration {name}")
    tuner.update_multiple(**config)
    input("Try it out, then press Enter...")
    
    rating = int(input("Rate 1-5: "))
    print(f"Config {name}: {rating}/5")
```

## Troubleshooting

### Valve Feels Unstable

**Problem**: Oscillations or vibrations

**Solution**: Increase viscous damping and smoothing
```python
tuner.update_multiple(
    viscous=0.15,      # Higher damping
    smoothing=0.002    # More smoothing
)
```

### Valve Too Sensitive

**Problem**: Small inputs cause large movements

**Solution**: Increase damping and friction
```python
tuner.update_multiple(
    viscous=0.2,
    coulomb=0.02
)
```

### Walls Feel Bouncy

**Problem**: Valve bounces at endpoints

**Solution**: Increase wall damping
```python
tuner.set_wall_damping(0.4)
```

## Next Steps

- [Data Recording Tutorial](data-recording.md) - Record and analyze tuning results
- [Parameter Sweeps Tutorial](parameter-sweeps.md) - Automated testing
- [API Reference: DataRecorder](../../pyprism_api_reference.md) - Full API documentation
