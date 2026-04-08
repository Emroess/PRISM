<div align="center">
  <img src="docs/assets/images/PRISMlogo.png" width="600" alt="PRISM Logo">
</div>

# PRISM
Train robotic policies on physical rotational tasks — fully automated, self-resetting, zero human intervention.

[![License](https://img.shields.io/badge/License-MIT-blue.svg)](https://github.com/Emroess/PRISM)
[![C/STM32](https://img.shields.io/badge/C%2FSTM32-00599C?logo=c%2B%2B&logoColor=white)](https://github.com/Emroess/PRISM)
[![Python](https://img.shields.io/badge/Python-3776AB?logo=python&logoColor=white)](https://github.com/Emroess/PRISM)

---

## Table of Contents

- [About & Why](#about--why)
- [Build One](#build-one-or-two-for-your-lab)
- [User Guides](#user-guides)
- [Rotation Library](#rotation-library)
- [Features](#features)
- [License](#license)

## About & Why

PRISM (Programmable Rotary Impedance Suite for Manipulation) is a flexible, hardware-based haptic control system designed to help practitioners train policies on real-world rotational tasks. Unlike purely virtual simulations, PRISM can replicate the tactile force feedback of valves, handles, knobs, and fasteners. It delivers realistic physical-world force feedback to the policy through a modular, motor-driven interface.

The core value of PRISM lies in helping the practitioner **automate robotic policy training**. It streams high-frequency (1 kHz) real-time TCP telemetry, including precise position, velocity, and torque feedback. This enables robotic policies to autonomously monitor task progress in real time. Practitioner-written scripts can then determine task success or failure using physical metrics — such as theta_on, theta_off, torque, or velocity thresholds — streamed over Ethernet.

PRISM’s fully programmatic API allows the motor to automatically reset to its starting state. Practitioners can also instantly re-randomize physical parameters — such as damping, friction, and wall stiffness — for the next episode. This creates a seamless, continuous training loop, enabling robots to physically train on **self-resetting tasks** with zero human intervention.

### Prerequisites

- A PC running Linux, Windows, or MacOS
- A 180 x 180 x 180 mm or larger 3D printer
- BOM
- Python 3.10+
- GNU ARM Embedded Toolchain & Make
- IssacSim, MuJoCo, ect (For RL training)
- A cool robot to train (optional)

## Build One For Your Lab
All CAD files, assembly guides, and firmware are open-sourced. So start building and make it your own. 

1. **Order the Bill of Materials ([BOM](docs/Build%20Guide/BOM/BOM.md)):** (Add BOM Total Cost Here)
2. **Print the STLs:** 3D print the [structural mounts](docs/Build%20Guide/CAD%20&%20Assembly) and desired [interface handles](docs/Rotation%20Library/Rotation%20Library.md).
3. **Assembly:** Follow the [assembly guide](docs/Build%20Guide/Assembly%20Guide/Assembly%20Guide.md) to put the pieces together.
4. **Install the Firmware:** Install the PRISM [firmware](docs/User%20Guides/firmware/firmware-installation.md) onto the STM32 microcontroller.
5. **First Use:** Follow the [getting started guide](docs/User%20Guides/getting-started/getting-started.md) to verify the haptic response and learn how to use the CLI.

### Installation

```bash
git clone https://github.com/Emroess/PRISM.git
cd PRISM/firmware
make && make flash
```

## User Guides

Control PRISM via the Command Line, REST API, Web Interface, or Python Client. See the [**Usage Docs**](docs/User%20Guides/)) for full references.

To start using the Python client:

```bash
pip install -e client/
```

```python
from pysteve import SteveClient

steve = SteveClient("192.168.1.100")
steve.valve_start(preset="smooth")
```

## Rotation Library

PRISM's modular handle system allows for custom haptic emulations. See the [**Rotation Library Docs**](docs/Rotation%20Library/Rotation%20Library.md) for 3D printable designs and parameter configurations.

Attachments:

- Hydrant Handwheel (4-turn)
- Quarter-turn Valve (90°)
- Door Handle (+/- 45°)
- Wrench Tightening
- More to come!

## Features

- **Real-time force feedback** at 1 kHz+
- **Programmable characteristics** (damping, friction, stiffness, endstops)
- **Controlled randomness** for repeatable task variations
- **Multi-interface support**: CLI, Browser GUI, REST API, Data Streaming
- **Simulation Integration (Under Development)**: Experimental support for reinforcement learning environments (Isaac Sim, MuJoCo).

## License

MIT License