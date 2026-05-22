
## Table of Contents

- [📁 Directory Structure](#directory-structure)
  - [Handles](#handles)
    - [Handle Options](#handle-options)
      - [🔴 **Hydrant Style "Handwheel" Valve Handle**](#hydrant-style-handwheel-valve-handle)
      - [🟡 **Handle Style "Quarter-Turn" Valve Handle**](#handle-style-quarter-turn-valve-handle)
      - [🟢 **Lever Style "Door Handle"**](#lever-style-door-handle)
      - [🔵 **Wrench Tightening**](#wrench-tightening)
  - [PRISM Device](#prism-device)
- [🔗 Related Resources](#related-resources)


﻿# CAD Files

This directory contains all CAD files and 3D models for the **PRISM** (Simulated Task Exploration | Valve Emulation) project.

## 📁 Directory Structure

### [Handles](./Handles)
CAD files and designs for the handle components that attach to the motor shaft for valve emulation. See [Handles/handles.json](./Handles/handles.json) for a structured catalog of all available handles.

#### Handle Options

---

##### 🔴 **Hydrant Style "Handwheel" Valve Handle**
*For 4-turn valve emulation - industrial handwheel design*

![Hydrant Handwheel](./images/handwheel_installed.jpeg)


**Purchase Components:**
- [Handwheel Handle](https://a.co/d/cDYmmxf) - Main handle
- [goBILDA 1310 Series Hyper Hub (8mm REX Bore)](https://www.gobilda.com/1310-series-hyper-hub-8mm-rex-bore/) - Mounting hub
    
**3D Print Files:**
- [Hand Wheel Adapter Upper.stl](./Handles/HydrantHandwheel/cad/Hand%20Wheel%20Adapter%20Upper.stl)
- [Handwheel Adapter Lower.stl](./Handles/HydrantHandwheel/cad/Handwheel%20Adapter%20Lower.stl)

**Assembly Instructions:**
- *Install the LOWER ADAPTER onto the main PRISM driveshaft using the GoBilda Hyper Hub and 4 M4 screws*

- *Press-fit the handle onto the LOWER ADAPTER*
![Hydrant Handwheel](./images/handwheel.jpg)
- *Place the UPPER ADAPTER on top of the valve handle and install 3X M4 screws through the entire assembly using washers and locknuts*

---
    
##### 🟡 **Handle Style "Quarter-Turn" Valve Handle**
*For quarter-turn valve emulation - 90° rotation design*

![Quarter-turn Handle](./images/quarter_turn.jpeg)

**Purchase Components:**
- [Quarter-Turn Valve Handle](https://a.co/d/isgp3Wc) - Main handle
- [goBILDA 1310 Series Hyper Hub (8mm REX Bore)](https://www.gobilda.com/1310-series-hyper-hub-8mm-rex-bore/) - Mounting hub

**3D Print Files:**
- *None Required*

**Assembly Instructions:**
- *Mount the valve handle using an M5 screw and washer as shown, making sure that the tine on the end of the handle falls into groove in the GoBilda hyper hub*

---
  
##### 🟢 **Lever Style "Door Handle"**
*Emulates standard self-centering/spring-loaded door handle behavior*

<!-- ![Lever Handle](./images/lever-handle.jpg) -->

**Purchase Components:**
- [Door Handle](https://a.co/d/iUJtJVq) - Lever handle
- [goBILDA REX Shaft (8mm, 48mm length)](https://www.gobilda.com/2106-series-stainless-steel-rex-shaft-8mm-diameter-48mm-length/) - Connection shaft
- [goBILDA Hyper Coupler (8mm REX to 8mm REX)](https://www.gobilda.com/4007-series-hyper-coupler-8mm-rex-bore-to-8mm-rex-bore/) - Shaft coupler

**3D Print Files:**
- *None Required*

**Assembly Instructions:**
- *This door handle comes in two halves, select the half that DOES NOT have a central shaft, the other half will not be used*

- *Using pliers bend the sheet metal tines holding the spring mechanism "straight", pry the brass-colored sheet metal spring retainer, spring and silver-colored sheet metal component off*

- *Install the GoBilda Rex shaft through the brass-colored spring retainer with the C-clip on the side facing the door handle.*

- *Reinstall the brass-colored spring retainer and bend the tabs back*

- *Install the Gobilda coupler so that it clamps the Rex shaft tightly against the brass-colored retainer"

- *"*Install the other side of the coupler onto the main PRISM driveshaft*


---

##### 🔵 **Wrench Tightening**
*Emulates tightening tasks such as bolts being tightened using standard hand-held tools*

![Wrench Tightening](./images/8mm_wrench.jpg)

**Purchase Components:**
- [8mm Wrench](https://a.co/d/g34gK7M) - Wrench tool

**3D Print Files:**
- *None Required*
- *TODO: Optional Wrench Capture Washer*

**Assembly Instructions:**
- *The PRISM "REX" drive shaft is an 8mm hex and can be turned with either an 8mm socket and ratchet or a standard 8mm wrench*

- *If desired the Hyper Hub can be raised or lowered to "capture" the wrench.*

---

### [PRISM Device](./PRISM%20Device)
Main device CAD files and assemblies for the complete PRISM system.

**Contents:**
- Motor mounting brackets
- STM32H7 enclosure
- Odrive S1 mounting plates
- Cable management solutions
- *Additional files to be added*

---


## 🔗 Related Resources

- [Main Repository](https://github.com/Emroess/PRISM)
- [Project Documentation](../README.md)
- [Build Guide](../docs/) *(in progress)*
- [Firmware Documentation](../firmware/)



