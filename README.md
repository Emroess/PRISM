<div align="center">
  <img src="docs/assets/images/PRISMlogo.png" width="600" alt="PRISM Logo">
</div>

# PRISM

An open-source, programmable rotary impedance suite for emulating realistic task haptics.

[![License](https://img.shields.io/badge/License-MIT-blue.svg)](https://github.com/Emroess/PRISM)
[![C/STM32](https://img.shields.io/badge/C%2FSTM32-00599C?logo=c%2B%2B&logoColor=white)](https://github.com/Emroess/PRISM)
[![Python](https://img.shields.io/badge/Python-3776AB?logo=python&logoColor=white)](https://github.com/Emroess/PRISM)

---

## Table of Contents
- [Getting Started](#getting-started)
- [Usage](#usage)
- [Emulation Library](#emulation-library)
- [Features](#features)
- [Contributing](#contributing)
- [License](#license)

## About / Overview
PRISM (Programmable Rotary Impedance Suite for Manipulation) is an advanced hardware-based haptic control system engineered to replicate real-world valves, handles, knobs, and fasteners. Unlike purely virtual simulations, PRISM provides physical-world force feedback at 1kHz+ through a modular, motor-driven interface.

The core value of PRISM is **fully automated, unattended robotic policy training**. It streams high-frequency, real-time TCP telemetry including precise position, velocity, and torque feedback. This allows robotic policies to autonomously track task status. Software can determine task completion using physical metrics like `theta_on`, `theta_off`, torque, or velocity thresholds. 

Additionally, PRISM's fully programmatic API allows the motor to **reset to its start state**. It can also instantly re-randomize physical parameters (damping, friction, wall stiffness) for the next episode. This creates a continuous training loop. Robots can physically train on self-resetting tasks without any human intervention.

## build it yourself
All CAD files, assembly guides, and firmware are entirely open-source. Anyone can assemble a working PRISM device without any prior experience.

- [bill of materials](docs/Build%20Guide/Build%20Guide.md)
- [CAD files & assembly guide](docs/Rotation%20Library/Rotation%20Library.md)
- [software stack (GitHub)](https://github.com/Emroess/PRISM)

To start using the PRISM tool, follow these sequential steps:
1. **Order the BOM:** Purchase the required off-the-shelf components.
2. **Print the STLs:** 3D print the structural mounts and interface handles from the CAD files.
3. **Assemble the Hardware:** Follow the mechanical assembly guide to put the pieces together.
4. **Flash the Firmware:** Load the PRISM software onto the STM32 microcontroller.
5. **Test the System:** Connect via USB and verify the haptic response.

For detailed instructions on flashing the firmware and testing the assembled system, refer to the [**getting started docs**](docs/User%20Guides/getting-started.md).

### Prerequisites
- [STM32 Nucleo-H753ZI](https://www.st.com/en/evaluation-tools/nucleo-h753zi.html)
- [ODrive S1 Motor Controller](https://odriverobotics.com/)
- Python 3.10+
- GNU ARM Embedded Toolchain & Make

### Installation
```bash
git clone https://github.com/Emroess/PRISM.git
cd PRISM/firmware
make && make flash
```

## Usage
Control PRISM via the Command Line, REST API, Web Interface, or Python Client. See the [**Usage Docs**](docs/User%20Guides/usage.md) for full references.

To start using the Python client:
```bash
pip install -e client/
```
```python
from pysteve import SteveClient

steve = SteveClient("192.168.1.100")
steve.valve_start(preset="smooth")
```

## Emulation Library
PRISM's modular handle system allows for custom haptic emulations. See the [**Rotation Library Docs**](docs/Rotation%20Library/Rotation%20Library.md) for 3D printable designs and parameter configurations.

Included Handles:
- Hydrant Handwheel (4-turn)
- Quarter-turn Valve (90°)
- Door Handle (+/- 45°)
- Wrench Tightening

## Features
- **Real-time force feedback** at 1 kHz+
- **Programmable characteristics** (damping, friction, stiffness, endstops)
- **Controlled randomness** for repeatable task variations
- **Multi-interface support**: CLI, Browser GUI, REST API, Data Streaming
- **Simulation Integration (Under Development)**: Experimental support for reinforcement learning environments (Isaac Sim, MuJoCo).

## Contributing
We welcome contributions! Please see [Next Steps](docs/next-steps.md) for testing opportunities, community feedback, and future enhancements.

## License
MIT License


