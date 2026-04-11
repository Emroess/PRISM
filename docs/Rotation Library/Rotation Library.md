[⬅ Back to Main README](../../README.md#build-one-for-your-lab)

# Rotation Library

## Table of Contents

- [Emulation Profiles (Haptic Presets)](#emulation-profiles-haptic-presets)
- [Physical Handle Library](#physical-handle-library)
    - [1. Hydrant Handwheel](#1-hydrant-handwheel)
    - [2. Quarter-Turn Handle](#2-quarter-turn-handle)
    - [3. Lever Style Door Handle](#3-lever-style-door-handle)
    - [4. Wrench Tightening](#4-wrench-tightening)
- [Contributing a New Handle](#contributing-a-new-handle)

The PRISM system relies on interchangeable physical handles combined with software "Presets" (of which can be changed if desired) to accurately emulate real-world environments. This Rotation Library documents the physically accurate rotational profiles and how to assemble the required handles.

## Emulation Profiles (Haptic Presets)

Profiles control the resistance, stiffness, and feel of the main drive shaft. These parameters correspond precisely to real forces and can be recalled instantaneously on the hardware.

| Preset      | Purpose                                                          | Primary Settings                                        |
| ----------- | ---------------------------------------------------------------- | ------------------------------------------------------- |
| **default** | Balanced configuration for general testing.                      | Damping: 0.05, Friction: 0.01, Wall K: 1.0, Wall C: 0.1 |
| **smooth**  | Low friction and damping for easy, uninhibited rotation.         | Damping: 0.02, Friction: 0.005                          |
| **stiff**   | High stiffness for precise positioning, mimics strict fasteners. | Damping: 0.1, Friction: 0.02                            |
| **heavy**   | High damping for sluggish, viscous feel (e.g. rusted valves).    | (Drivetrain torque must be calibrated)                  |

Use `valve_preset <name>` in the CLI to instantly switch emulation modes.

---

## Physical Handle Library

Attaching 3D-printed and COTS (Commercial Off-The-Shelf) parts allows the robotic policy to interact with real geometry. Handle metadata is stored in [`handles.json`](Handles/handles.json) for extending.

### 1. Hydrant Handwheel

*For 4-turn valve emulation - industrial handwheel design*

**Assembly & Components:**

- [Handwheel Handle](https://a.co/d/cDYmmxf)
- [goBILDA 1310 Series Hyper Hub (8mm REX Bore)](https://www.gobilda.com/1310-series-hyper-hub-8mm-rex-bore/)
- *3D Print File:* [`Hand Wheel Adapter Upper.stl`](Handles/HydrantHandwheel/cad/Hand%20Wheel%20Adapter%20Upper.stl)
- *3D Print File:* [`Handwheel Adapter Lower.stl`](Handles/HydrantHandwheel/cad/Handwheel%20Adapter%20Lower.stl)
- **Installation:** Install the lower adapter onto the main PRISM driveshaft using the GoBilda Hyper Hub and 4 M4 screws. Press-fit the handle onto the lower adapter. Place the upper adapter on top of the valve handle and install 3 M4 screws through the entire assembly using washers and locknuts.

### 2. Quarter-Turn Handle

*For quarter-turn valve emulation - 90° rotation design*

**Assembly & Components:**

- [Quarter-Turn Valve Handle](https://a.co/d/isgp3Wc)
- [goBILDA 1310 Series Hyper Hub (8mm REX Bore)](https://www.gobilda.com/1310-series-hyper-hub-8mm-rex-bore/)
- *3D Print File:* None
- **Installation:** Mount the valve handle using an M5 screw and washer, ensuring the tine on the end of the handle falls into the groove in the GoBilda hyper hub. Adjust the `valve_wall_k` preset to simulate physical hard stops at the 90° boundary.

### 3. Lever Style Door Handle

*Emulates standard self-centering/spring-loaded door handle behavior*

**Assembly & Components:**

- [Door Handle](https://a.co/d/iUJtJVq) 
- [goBILDA REX Shaft (8mm, 48mm length)](https://www.gobilda.com/2106-series-stainless-steel-rex-shaft-8mm-diameter-48mm-length/)
- [goBILDA Hyper Coupler (8mm REX)](https://www.gobilda.com/4007-series-hyper-coupler-8mm-rex-bore-to-8mm-rex-bore/)
- *3D Print File:* None
- **Installation:** Select the door handle half without the central shaft. Bend the sheet metal tines holding the spring mechanism "straight", pry off the brass-colored retainer, spring, and silver-colored component. Install the GoBilda REX shaft through the brass retainer with the C-clip on the side facing the handle. Reinstall the retainer and bend tabs back. Install the GoBilda coupler to clamp the REX shaft against the retainer.

### 4. Wrench Tightening

*Emulates tightening tasks using standard hand-held tools*

**Assembly & Components:**

- [8mm Wrench](https://a.co/d/g34gK7M)
- *3D Print File:* None (Optional Wrench Capture Washer)
- **Installation:** The "REX" drive shaft is an 8mm hex and can be turned directly with an 8mm wrench. You can configure "stiff" presets to match fastener torques.

---

## Contributing a New Handle

You can construct your own handle adapters and submit them. They require a JSON schema representation inside [`docs/CAD/Handles/handles.json`](Handles/handles.json) specifying images, purchased BOM objects, printed parts, and CAD/Sim files.

---