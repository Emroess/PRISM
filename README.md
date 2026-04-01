<div align="center">

<h1>PRISM: Programmable Rotary Impedance Suite for Manipulation</h1>

<div style="display: flex; gap: 1rem; justify-content: center; align-items: center;" >
<img src="docs/CAD/images/PRISM LOGO Square.jpg" style="width: 40%;" alt="PRISM Logo"/>
</div>

<h2>Introduction</h2>

An open-source, low-cost task emulator engineered to replicate the dynamics of real-world valves, handles, knobs, and fasteners. 

PRISM can be used for robotic policy training, as a benchmark tool, or to emulate virtually any real-world rotational behavior with live-swapping of physical handle geometries and configurable haptics.

</div>

<br>

<div align="center">

### [▶ Explore The Build Guide](docs/Build.md)
*Bill of Materials (BOM), Hardware Build Instructions, Firmware Flashing, and Boot Diagnostics.*

### [▶ Explore The User Guide](docs/User.md)
*Device Connection, Command-Line Operations, Software APIs (Python, MuJoCo, Isaac Sim), and Integration.*

### [▶ Explore The Rotation Library](docs/Rotation_Library.md)
*Physically Accurate Haptic Profiles, Valve Geometries, and the Handle Hardware Library.*

</div>

---

## Key Capabilities

### Multi-Task Benchmark - Realistic rotational task simulation

<table style="width:100%"><tr>
<td align="center" style="width:25%">
<img src="docs/CAD/images/PXL_20260112_203228289.PORTRAIT.jpg" style="width:100%" alt="Multi-turn valves"><br>
<strong>Multi-turn valves</strong><br>
</td>
<td align="center" style="width:25%">
<img src="docs/CAD/images/quarter_turn.jpeg" style="width:100%" alt="Quarter-turn Valves"><br>
<strong>Quarter-turn Valves</strong><br>
</td>
<td align="center" style="width:25%">
<img src="docs/CAD/images/door_handle_iso.jpg" style="width:100%" alt="Door Handles"><br>
<strong>Door Handles</strong><br>
</td>
<td align="center" style="width:25%">
<img src="docs/CAD/images/wrench_free.jpg" style="width:100%" alt="Fastener Tighting"><br>
<strong>Fastener Tighting</strong><br>
</td>
</tr></table>

PRISM simulates common rotational tasks with configurable physical behavior and reproducible randomness. 

- **Configurable dynamics**: Damping, friction, stiffness, and endstops.
- **Controlled randomness**: REPEATABLY emulate real-world rotational physics such as sticking or rusty rotation targets.
- **Real-time telemetry**: Position, torque/force and other signals for success detection.
- **Velocity Maximums**: Prevents damage to robotic manipulators or the PRISM unit.
- **Self-resetting tasks and presets**: Load physical scenarios instantly via the embedded network.

---

## Quick Start Overview 

*Refer to the [User Guide](docs/User.md) for full details.*

**Firmware Flashing:**
```bash
cd firmware && make && make flash
# Connect serial: screen /dev/ttyACM0 115200
# Try: odrive_enable, valve_start, valve_preset smooth
```

**Python Client Connection:**
```python
pip install pysteve
from pysteve import SteveClient

steve = SteveClient("192.168.1.100")
steve.valve_start(preset="smooth")
```

---

<p align="center">
  <em>PRISM: Benchmarking realistic rotational task manipulation for robotics research.</em>
</p>
