# PySteve: Python Client for PRISM Haptic Valve System

[![Python Version](https://img.shields.io/badge/python-3.8%2B-blue.svg)](https://www.python.org/downloads/)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Code style: black](https://img.shields.io/badge/code%20style-black-000000.svg)](https://github.com/psf/black)

PySteve is a comprehensive Python client for the PRISM (Simulated Task Exploration | Valve Emulation) haptic valve simulation system. It enables robotics engineers and researchers to seamlessly integrate haptic valve simulations into their projects with support for MuJoCo, Gymnasium-Robotics, and NVIDIA Isaac Sim.

## Features

- **Synchronous and Asynchronous APIs** - Sync-first design with async support for multi-device control
- **Real-time Parameter Updates** - Adjust viscous damping, friction, and wall parameters during operation
- **High-speed Data Streaming** - TCP streaming at 10-100 Hz with thread-safe callbacks
- **MuJoCo Integration** - Virtual actuator sync with interpolation and hardware-in-the-loop support
- **Gymnasium Environments** - Ready-to-use RL environments with configurable observation/action spaces
- **Isaac Sim Integration** - USD-based valve integration with multi-device coordination
- **Robust Error Handling** - Auto-reconnection with exponential backoff and connection callbacks
- **Data Recording** - Export to CSV, HDF5, and ROS bag formats

## Quick Start

### Installation

**Core package:**
```bash
pip install pysteve
```

**With framework-specific extras:**
```bash
pip install pysteve[mujoco]      # MuJoCo integration
pip install pysteve[gymnasium]   # Gymnasium RL environments
pip install pysteve[async]       # Async client support
pip install pysteve[data]        # HDF5 and pandas support
pip install pysteve[viz]         # Real-time plotting
pip install pysteve[all]         # All extras
```

### Basic Usage

```python
from pysteve import SteveClient

# Connect to PRISM device
with SteveClient("192.168.1.100") as steve:
    # Enable motor and start valve
    steve.enable_motor()
    steve.start_valve()
    
    # Get real-time status
    status = steve.get_status()
    print(f"Position: {status['pos_deg']:.2f}°")
    print(f"Velocity: {status['vel_rad_s']:.2f} rad/s")
    print(f"Torque: {status['torque_nm']:.3f} Nm")
    
    # Update parameters in real-time
    steve.update_config(
        viscous=0.08,
        coulomb=0.015,
        wall_stiffness=2.0
    )
    
    # Load preset (0=light, 1=medium, 2=heavy, 3=industrial)
    steve.load_preset(0)
    
    # Stop valve
    steve.stop_valve()
```

### Streaming Data

```python
from pysteve import SteveClient, SteveStreamer

client = SteveClient("192.168.1.100")
streamer = SteveStreamer(client)

def on_data(sample):
    print(f"Position: {sample['position_deg']:.2f}°, "
          f"Torque: {sample['torque_nm']:.3f} Nm")

# Start streaming at 50 Hz
streamer.start_stream(interval_ms=20, callback=on_data)

# Let it run...
import time
time.sleep(10)

streamer.stop_stream()
```

### Real-time Parameter Tuning

```python
from pysteve import SteveClient, RealtimeTuner

client = SteveClient("192.168.1.100")
client.enable_motor()
client.start_valve()

tuner = RealtimeTuner(client)

# Adjust parameters while valve is running
tuner.set_viscous(0.06)
tuner.set_coulomb(0.012)
tuner.set_wall_stiffness(1.5)

# Update multiple parameters atomically
tuner.update_multiple(
    viscous=0.08,
    wall_damping=0.15,
    smoothing=0.002
)
```

### MuJoCo Integration

```python
import mujoco
from pysteve.integrations.mujoco import SteveValveActuator

# Load MuJoCo model
model = mujoco.MjModel.from_xml_path("robot_with_valve.xml")
data = mujoco.MjData(model)

# Create PRISM actuator with hardware sync
actuator = SteveValveActuator(
    steve_ip="192.168.1.100",
    mujoco_joint_name="valve_joint",
    sync_mode="hardware",  # 'simulation', 'hardware', or 'hybrid'
    target_hz=1000,  # Match PRISM's control loop
    latency_compensation_ms=50
)

# Connect and start
actuator.connect()
actuator.start()

# Simulation loop
while True:
    # Update actuator (interpolates PRISM state for MuJoCo timestep)
    actuator.update(model, data)
    
    # Step MuJoCo
    mujoco.mj_step(model, data)
```

### Gymnasium Environment

```python
from pysteve.integrations.gymnasium import SteveValveEnv
from stable_baselines3 import PPO

# Create environment with configurable termination
env = SteveValveEnv(
    steve_ip="192.168.1.100",
    max_steps=1000,
    terminate_on_torque_limit=False,
    terminate_on_position_limit=True,
    reward_function="trajectory_following"
)

# Train RL agent
model = PPO("MlpPolicy", env, verbose=1)
model.learn(total_timesteps=100000)

# Test trained agent
obs, info = env.reset()
for _ in range(1000):
    action, _ = model.predict(obs)
    obs, reward, terminated, truncated, info = env.step(action)
    if terminated or truncated:
        print(f"Episode ended: {info['termination_reason']}")
        obs, info = env.reset()
```

### Isaac Sim Integration

```python
from pysteve.integrations.isaac import IsaacSteveConnector
from omni.isaac.core import World

# Create Isaac Sim world
world = World()
stage = world.stage

# Add PRISM valve to scene
connector = IsaacSteveConnector(stage, device_ip="192.168.1.100")
valve_prim = connector.create_valve_articulation(
    path="/World/Valve",
    name="steve_valve_1"
)

# Configure haptic parameters (stored in USD with steve: namespace)
connector.set_haptic_parameters(
    valve_prim,
    viscous=0.05,
    coulomb=0.01,
    wall_stiffness=1.0,
    wall_damping=0.1,
    travel=90.0
)

# Sync to hardware
connector.sync_to_hardware()

# Run simulation with hardware sync
world.reset()
while True:
    connector.update_async()  # Updates hardware from USD
    world.step(render=True)
```

## Architecture

```
pysteve/
├── core/               # Core client and streaming
│   ├── client.py       # SteveClient (sync)
│   ├── async_client.py # SteveAsyncClient
│   ├── streaming.py    # TCP streaming with callbacks
│   ├── config.py       # Configuration dataclasses
│   └── exceptions.py   # Custom exceptions
├── control/            # Real-time control
│   ├── realtime_tuner.py    # Live parameter updates
│   ├── parameter_sweep.py   # Automated testing
│   └── data_recorder.py     # Data logging
├── integrations/       # Framework integrations
│   ├── mujoco/         # MuJoCo support
│   ├── gymnasium/      # Gymnasium environments
│   └── isaac/          # Isaac Sim integration
└── utils/              # Utilities
    ├── stream_buffer.py     # Buffering and export
    ├── plotting.py          # Real-time visualization
    ├── ros_bridge.py        # ROS2 bridge
    └── validation.py        # Validation helpers
```

## Documentation

### 📚 Getting Started

- [Installation Guide](docs/installation.md) - Complete installation instructions
- [Configuration Guide](docs/configuration.md) - Network setup and valve parameters
- [Quickstart Tutorial](docs/quickstart.md) - 5-minute getting started guide

### 🎓 Tutorials

- [MuJoCo Integration](docs/tutorials/mujoco-integration.md) - Hardware-in-the-loop simulation
- [Gymnasium RL Training](docs/tutorials/gymnasium-rl.md) - Reinforcement learning environments
- [Parameter Tuning](docs/tutorials/parameter-tuning.md) - Real-time haptic parameter adjustment
- [Data Recording](docs/tutorials/data-recording.md) - Capture and analyze valve data

### 📖 Complete Documentation

- [Documentation Index](docs/index.md) - Full documentation hub with all guides
- [Examples](examples/) - Complete working code examples

## Examples

See the [`examples/`](examples/) directory for complete working examples:

- `basic_connection.py` - Basic connection and control
- `realtime_parameter_tuning.py` - Real-time parameter adjustment
- `data_collection.py` - High-speed data recording
- `parameter_sweep.py` - Automated parameter sweeps
- `mujoco_valve_manipulation.py` - MuJoCo integration
- `gymnasium_rl_training.py` - RL training with Gymnasium
- `isaac_multi_valve_scene.py` - Isaac Sim multi-valve scene
- `ros_integration.py` - ROS2 bridge

## Requirements

- Python 3.8+
- PRISM firmware device on network
- Optional: MuJoCo 2.3+, Gymnasium 0.28+, Isaac Sim 2023.1+

## Compatibility Matrix

| PySteve | Python | MuJoCo | Gymnasium | Isaac Sim |
|---------|--------|--------|-----------|-----------|
| 0.1.x   | 3.8-3.11 | 2.3.x, 3.x | 0.28.x, 0.29.x | 2023.x, 2024.x |

## Contributing

Contributions are welcome! Please see [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

## License

MIT License - see [LICENSE](LICENSE) for details.

## Citation

If you use PySteve in your research, please cite:

```bibtex
@software{pysteve2025,
  title = {PySteve: Python Client for PRISM Haptic Valve System},
  author = {PRISM Team},
  year = {2025},
  url = {https://github.com/yourusername/steve_can}
}
```

## Support

- Documentation: https://pysteve.readthedocs.io
- Issues: https://github.com/yourusername/steve_can/issues
- Discussions: https://github.com/yourusername/steve_can/discussions

## Acknowledgments

PySteve is designed to integrate with:
- [MuJoCo](https://mujoco.org/) - Physics simulator
- [Gymnasium](https://gymnasium.farama.org/) - RL environment standard
- [NVIDIA Isaac Sim](https://developer.nvidia.com/isaac-sim) - Robot simulation platform
- [Stable-Baselines3](https://stable-baselines3.readthedocs.io/) - RL algorithms

