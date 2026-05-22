<div align="center">
   <img src="../assets/images/PRISMlogo.png" width="500" alt="PRISM Logo">

   <h3>Rotation Library</h3>

   <p>Interchangeable handles and programmable haptic presets<br>
   for emulating real-world rotational tasks.</p>

   <a href="../../README.md">
      <img src="https://img.shields.io/badge/⬅_Back_to_README-e34c26?style=for-the-badge" alt="Back to README">
   </a>
   &nbsp;
   <a href="../Build%20Guide/PRISM%20Bill%20of%20Materials.md">
      <img src="https://img.shields.io/badge/Bill_of_Materials-2ea44f?style=for-the-badge" alt="Bill of Materials">
   </a>
</div>

---

> [!NOTE]
> The PRISM system relies on interchangeable physical handles combined with software "Presets" to accurately emulate real-world rotational environments. Swap handles in seconds — switch presets instantly via the user interfaces.

---

## Emulation Profiles (Haptic Presets)

Profiles control the resistance, stiffness, and feel of the main drive shaft. These parameters correspond precisely to real forces and can be recalled instantaneously on the hardware.

| Preset | Purpose | Travel | Primary Settings |
|:-------|:--------|:------:|:-----------------|
| **90 valve** | Quarter-turn valve with hard stops | 90° | Viscous: 0.01 · Coulomb: 0.8 · Wall K: 100 · Wall C: 0 · Smoothing: 10 · Limit: 8 Nm |
| **h-wrench** | Rigid fastener tightening | 180° | Viscous: 0.1 · Coulomb: 0.2 · Wall K: 150 · Wall C: 0 · Smoothing: 100 · Limit: 10 Nm |
| **door handle** | Spring-loaded lever handle | 45° | Viscous: 0.01 · Coulomb: 0.01 · Wall K: 4.0 · Wall C: 0.005 · Smoothing: 1 · Limit: 10 Nm |
| **turnwheel** | Heavy industrial handwheel | 360° | Viscous: 0.1 · Coulomb: 0.01 · Wall K: 100.0 · Wall C: 0.1 · Smoothing: 100 · Limit: 8 Nm |
| **custom** | User-defined configuration | Variable | Variable (User Defined) |

> [!TIP]
> Use `valve_preset <name>` in the CLI to instantly switch emulation modes.

---

## Physical Handle Library

Attaching 3D-printed and COTS (Commercial Off-The-Shelf) parts allows the robotic policy to interact with real geometry. Handle metadata is stored in [`handles.json`](Handles/handles.json) for programmatic extension.

---

### 1. Hydrant Handwheel

*For 4-turn valve emulation — industrial handwheel design*

<details>
<summary>Components & Assembly Instructions</summary>
<br>

| Qty | Component | Link |
|:---:|:----------|:-----|
| 1 | Handwheel Handle | [Amazon](https://a.co/d/cDYmmxf) |
| 1 | goBILDA 1310 Series Hyper Hub (8 mm REX Bore) | [GoBilda](https://www.gobilda.com/1310-series-hyper-hub-8mm-rex-bore/) |
| 1 | Hand Wheel Adapter Upper *(3D Print)* | [`Hand Wheel Adapter Upper.stl`](Handles/HydrantHandwheel/cad/Hand%20Wheel%20Adapter%20Upper.stl) |
| 1 | Handwheel Adapter Lower *(3D Print)* | [`Handwheel Adapter Lower.stl`](Handles/HydrantHandwheel/cad/Handwheel%20Adapter%20Lower.stl) |

**Installation:**  
Install the lower adapter onto the main PRISM driveshaft using the GoBilda Hyper Hub and 4 M4 screws. Press-fit the handle onto the lower adapter. Place the upper adapter on top of the valve handle and install 3 M4 screws through the entire assembly using washers and locknuts.

</details>

---

### 2. Quarter-Turn Handle

*For quarter-turn valve emulation — 90° rotation design*

<details>
<summary>Components & Assembly Instructions</summary>
<br>

| Qty | Component | Link |
|:---:|:----------|:-----|
| 1 | Quarter-Turn Valve Handle | [Amazon](https://a.co/d/isgp3Wc) |
| 1 | goBILDA 1310 Series Hyper Hub (8 mm REX Bore) | [GoBilda](https://www.gobilda.com/1310-series-hyper-hub-8mm-rex-bore/) |

*No 3D-printed parts required.*

**Installation:**  
Mount the valve handle using an M5 screw and washer, ensuring the tine on the end of the handle falls into the groove in the GoBilda Hyper Hub. Adjust the `valve_wall_k` preset to simulate physical hard stops at the 90° boundary.

</details>

---

### 3. Lever Style Door Handle

*Emulates standard self-centering / spring-loaded door handle behavior*

<details>
<summary>Components & Assembly Instructions</summary>
<br>

| Qty | Component | Link |
|:---:|:----------|:-----|
| 1 | Door Handle | [Amazon](https://a.co/d/iUJtJVq) |
| 1 | goBILDA REX Shaft (8 mm, 48 mm length) | [GoBilda](https://www.gobilda.com/2106-series-stainless-steel-rex-shaft-8mm-diameter-48mm-length/) |
| 1 | goBILDA Hyper Coupler (8 mm REX) | [GoBilda](https://www.gobilda.com/4007-series-hyper-coupler-8mm-rex-bore-to-8mm-rex-bore/) |

*No 3D-printed parts required.*

**Installation:**  
Select the door handle half without the central shaft. Bend the sheet metal tines holding the spring mechanism "straight", pry off the brass-colored retainer, spring, and silver-colored component. Install the GoBilda REX shaft through the brass retainer with the C-clip on the side facing the handle. Reinstall the retainer and bend tabs back. Install the GoBilda coupler to clamp the REX shaft against the retainer.

</details>

---

### 4. Wrench Tightening

*Emulates tightening tasks using standard hand-held tools*

<details>
<summary>Components & Assembly Instructions</summary>
<br>

| Qty | Component | Link |
|:---:|:----------|:-----|
| 1 | 8 mm Wrench | [Amazon](https://a.co/d/g34gK7M) |

*No 3D-printed parts required. Optional wrench capture washer.*

**Installation:**  
The "REX" drive shaft is an 8 mm hex and can be turned directly with an 8 mm wrench. You can configure "stiff" presets to match fastener torques.

</details>

---

## Contributing a New Handle

You can construct your own handle adapters and submit them. Each new handle requires a JSON schema entry in [`handles.json`](Handles/handles.json) specifying images, purchased BOM objects, printed parts, and CAD/Sim files.

> [!TIP]
> Check the existing entries in `handles.json` for the expected schema format before submitting.

---

<div align="center">
   <sub>
      <a href="../../README.md">⬅ Back to Main README</a> · <a href="../Build%20Guide/PRISM%20Bill%20of%20Materials.md">← Bill of Materials</a>
   </sub>
</div>
