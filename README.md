<div align="center">
   <img src="docs/assets/images/PRISMlogo.png" width="700" alt="PRISM Logo">

[![License](https://img.shields.io/badge/License-MIT-blue.svg)](https://github.com/Emroess/PRISM)
[![C/STM32](https://img.shields.io/badge/C%2FSTM32-00599C?logo=c%2B%2B&logoColor=white)](https://github.com/Emroess/PRISM)
[![Python](https://img.shields.io/badge/Python-3776AB?logo=python&logoColor=white)](https://github.com/Emroess/PRISM)
[![GitHub Stars](https://img.shields.io/github/stars/Emroess/PRISM?style=social)](https://github.com/Emroess/PRISM)
</div>

<div align="center">
<h3>Train robot policies on real-world rotational tasks — with real force feedback.</h3>
</div>

## What is PRISM?

PRISM (Programmable Rotary Impedance Suite for Manipulation) is a flexible, hardware-based haptic control system designed to help practitioners train policies on real-world rotational tasks. Unlike purely virtual simulations, PRISM can replicate the tactile force feedback of valves, handles, knobs, and fasteners. It delivers realistic haptic feedback to the policy through a modular, motor-driven interface.


## Explore PRISM
<div align="center">  
<table>
  <tr>
    <td align="center" width="33%">
      <a href="docs/Build%20Guide/">
        <img src="https://img.shields.io/badge/-Build_Guide-2ea44f?style=for-the-badge" alt="Build Guide">
      </a>
      <br><sub>BOM, assembly, firmware</sub>
    </td>
    <td align="center" width="33%">
      <a href="docs/User%20Guides/">
        <img src="https://img.shields.io/badge/-User_Guides-0969da?style=for-the-badge" alt="User Guides">
      </a>
      <br><sub>CLI, REST API, Web GUI, Streaming</sub>
    </td>
    <td align="center" width="33%">
      <a href="docs/Rotation%20Library/">
        <img src="https://img.shields.io/badge/-Rotation_Library-8250df?style=for-the-badge" alt="Rotation Library">
      </a>
      <br><sub>Printable handles and haptic profiles</sub>
    </td>
  </tr>
  <tr>
    <td align="center" width="33%">
      <a href="firmware/">
        <img src="https://img.shields.io/badge/-Firmware-e8590c?style=for-the-badge" alt="Firmware">
      </a>
      <br><sub>STM32H7 open-source firmware</sub>
    </td>
    <td align="center" width="33%">
      <a href="client/">
        <img src="https://img.shields.io/badge/-Python_Client-ffd43b?style=for-the-badge" alt="Python Client">
      </a>
      <br><sub>PyPRISM API and examples</sub>
    </td>
    <td align="center" width="33%">
      <a href="#quick-start">
        <img src="https://img.shields.io/badge/-Quick_Start-e34c26?style=for-the-badge" alt="Quick Start">
      </a>
      <br><sub>Get building in 5 steps</sub>
    </td>
  </tr>
</table>
</div>


## Key Capabilities
| Feature | Description |
|---|---|
| **1 kHz Task Telemetry** | Real-time TCP streaming of θ, τ, and velocity for policy training |
| **Self-Resetting Tasks** | Automated return-to-start for continuous training loops |
| **Programmable Haptics** | Tune damping, friction, stiffness, and torque per-episode |
| **Multi-Interface** | CLI, Web GUI, REST API, and data streaming — all on-device |
| **Sim Integration** | Experimental Isaac Sim & MuJoCo support *(in development)* |
| **Modular Handles** | Swap 3D-printed attachments to emulate any rotational task |

<details>
<summary>Real-time Task Telemetry </summary>
<br>
PRISM streams (1 kHz) real-time TCP telemetry about the task over ethernet. This enables robotic policies to autonomously monitor task progress in real time. Practitioner-written scripts can also determine task success or failure by reading theta_on, theta_off, torque, or velocity thresholds.
</details>

<details>
<summary>Task Self-Resetting</summary>
<br>
PRISM’s fully programmable API allows the rotational task to automatically reset to its starting state or other programmed positions. Practitioners can use this feature to fast track expert demonstrations or make a continuous policy training loop with PRISM API control scripts.
</details>

<details>
<summary>Programmable Characteristics</summary>
<br>
Practitioners define a software “profile” by configuring physically meaningful parameters such as viscous damping, Coulomb friction coefficient, smoothing parameter ϵ, virtual wall stiffness, torque limits, and others. These parameters define how each rotational task will "feel" and lets PRISM emulate the spring-back of a lever door handle to the hard to turn quarter-turn valve found in naval ships. 
  
  > Practitioners can also instantly randomize these parameters for the next episode to train ML polices to adapt to task changes. 
</details>

<details>
<summary>Multi-interface Support</summary>
<br>
PRISM has a CLI, Web-GUI, REST API, Data Streaming interfaces. Each interface lets the practitioner configure or use certain features. 
  
  > Every user-interface is locally hosted by the main micro-controller. 
</details>

<details>
<summary>Simulation Integration (Under Development)</summary>
<br>
Experimental support for reinforcement learning environments (Isaac Sim, MuJoCo) and sim2real transfer.
</details>

## Specifications

| Spec | Value |
|---|---|
| Controller | STM32H7 microcontroller |
| Motor Driver | ODrive S1 |
| Telemetry Rate | 1 kHz over TCP/Ethernet |
| Interfaces | CLI, Web GUI, REST API, Data Stream |
| Build Cost | ~$400 – $500 |
| Print Volume | ≥ 180 × 180 × 180 mm |
| Software | Python 3.10+, GNU ARM Toolchain |
| License | MIT |

## Architecture

```mermaid
graph LR
    A[Robot Arm] -->|Grasps| B[PRISM Handle]
    B -->|Shaft Encoder + Motor| C[STM32H7]
    C -->|1 kHz TCP| D[Host PC]
    D -->|PyPRISM API| E[Policy Training]
    D -->|REST / CLI / Web| C
    C -->|Haptic Feedback| B
```

---

## Quick Start

> **Total build time:** ~2-5 hours  |  **Cost:** ~$400-500  |  **Difficulty:** Intermediate

| Step | Action | Link |
|:---:|---|---|
| **1** | **Order** the Bill of Materials | [BOM](docs/Build%20Guide/PRISM%20Bill%20of%20Materials.md) |
| **2** | **Print** structural mounts and handles | [Handles](docs/Rotation%20Library/Rotation%20Library.md) |
| **3** | **Assemble** the mechanical and electrical system | [Assembly Guide](docs/Build%20Guide/Assembly%20Guide.md) |
| **4** | **Flash** firmware onto the STM32 | [Firmware Guide](docs/User%20Guides/firmware/firmware-installation.md) |
| **5** | **Verify** haptic response via the onboard CLI | [CLI Guide](docs/User%20Guides/cli/connecting-to-onboard-cli.md) |

---

## Rotation Library

PRISM's modular handle system allows for custom haptic emulations. Each attachment mounts to the same motor shaft interface, letting practitioners swap between task types in seconds.

See the [**Rotation Library**](docs/Rotation%20Library/Rotation%20Library.md) for 3D-printable designs and parameter configurations.

| Handle | Range of Motion |
|---|---|
| Hydrant Handwheel | 4-turn |
| Quarter-turn Valve | 90 degrees |
| Door Handle | +/- 45 degrees |
| Wrench Tightening | Continuous |
| More to come | -- |

---

## Contributing

PRISM is open source and contributions are welcome.

- [Report a Bug]()
- [Request a Feature]()
- [Submit a Pull Request]()

---

## Citation

If you use PRISM in your research, please cite:

```bibtex
@misc{prism2026,
  title={PRISM: Programmable Rotary Impedance Suite for Manipulation},
  author={},
  year={2026},
  url={https://github.com/Emroess/PRISM}
}
```

---

## License

MIT License
