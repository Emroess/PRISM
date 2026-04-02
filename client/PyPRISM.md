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

## Quick Links

<div align="center">

### [▶ Start Here (Getting Started)](docs/getting_started.md)

### [▶ API Reference](docs/pyprism_api_reference.md)

### [▶ Advanced](docs/advanced)

*Go to the [client/docs/advanced/integrations](docs/advanced/integrations) folder to find usage documentation for integrating [mujoco](docs/integrations/mujoco.md), [ros](docs/ros.md), [isaacsim](docs/isaacsim.md), and [gymnasium](docs/gymnasium_rl.md)*

</div>


## PySTEVE Folder Architecture

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

## Python Examples

See the [examples/](examples/) directory for complete working examples:

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

| PySteve | Python   | MuJoCo     | Gymnasium      | Isaac Sim      |
| ------- | -------- | ---------- | -------------- | -------------- |
| 0.1.x   | 3.8-3.11 | 2.3.x, 3.x | 0.28.x, 0.29.x | 2023.x, 2024.x |

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