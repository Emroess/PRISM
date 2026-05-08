<div align="center">
   <img src="../assets/images/PRISMlogo.png" width="500" alt="PRISM Logo">

   <h3>Assembly Guide</h3>

   <a href="PRISM%20Bill%20of%20Materials.md">
      <img src="https://img.shields.io/badge/⬅_Bill_of_Materials-2ea44f?style=for-the-badge" alt="BOM">
   </a>
   &nbsp;
   <a href="odriveparam.md">
      <img src="https://img.shields.io/badge/Next:_ODrive_Config_→-0969da?style=for-the-badge" alt="Next: ODrive Config">
   </a>
</div>

# Build Guide

Welcome to the PRISM Build Guide. Here you will find all the resources required to source components, print and assemble your parts, and flash the necessary firmware to get your system ready.

## Table of Contents

- [Hardware Build Guide](#hardware-build-guide)
    - [1. 3D Print Components](#1-3d-print-components)
    - [2. Device Assembly](#2-device-assembly)
    - [3. Communication Wiring](#3-communication-wiring)

## Hardware Build Guide

### Materials

- 12 AWG wire, length TBD (TODO), Quantity: 7
- 22 or 24 AWG wire (for CAN bus), length TBD (TODO), Quantity: 3
- 20 to 23 AWG wire (for CAN transciever), length TBD (TODO), Quantity: 4
- Spade connectors, Quantity: 12
- Soldering iron + solder
- Wire strippers
- Wire crimpers

- Threaded heat inserts, Quantity: 5
- M.4 Screws, Quantity: 8
- M.3 Screws, Quantity: 2

**3D Print Components**
TODO: rewrite what is under 3D Print Components
Use the STLs located in the `docs/Rotation Library/Handles` directory (found in the original source, or linked in the Handle Library) for structural mounting.

- Motor mounting brackets
- STM32H7 enclosure
- Odrive S1 mounting plates


### 2. Power Supply Unit Assembly

#### Wiring

##### Wiring Diagram

TODO

##### Steps

1. Push the IEC Switch connector (PSU plug/switch), XT60 Bulkhead (Odrive output), and CAN 3-Pin Terminal Block, into the rear 3D printed plate, such that the IEC Switch connector is on the right side when the rear plate is facing you.
2. Cut the 12 AWG wires to the required lengths. TODO: include lengths
3. Strip off about 1 cm of insulation from both ends of every one of the 12 AWG wires.
4. Crimp the spade connectors to both ends of every one of the 12 AWG wires, except for two of them (which will have spade connectors on just one end).
5. Solder the ends of the two 12 AWG wires of length TODO onto the Odrive output terminals.
6. Solder the ends of the three 22 AWG wires (CAN bus) to the CAN 3-Pin Terminal Block.
    - Note: the other end of the CAN cables will be attached muc hlater near the end of the physical "box" assembly process.
6. Attach / tighten the spade connectors accordingly:
    - Inputs from AC are `ground` `neutral` `live` and should be connected to the IEC Switch connector in that order. TODO: confirm that is the actual order
    - The Odrive output is connected to the XT60 Bulkhead connector in the order `vpositive` `vnegative`. TODO: confirm that is the actual order
7. Adjust the output voltage on the PSU to `48V`; test with a volt-meter from the Odrive outputs to ensure that the voltage is correct.

Now set aside the PSU assembly for now. We will solder cables for the STM32 CAN Transciever.

1. Take your 20-23 AWG cables for the STM32 to CAN Transciever, and solder `RX` on the CAN Transcieiver to pin `pd0` on the STM32 and likewise `TX` to pin `pd1` on the rail.
2. Solder the CAN Transciever's `ground` and `VCC 3.3V` to the corresponding pins on the STM32 (which are also `ground` and `3.3V`).

Next up: Component Assembly

#### Component Assembly

##### Steps: PSU Rear (side without logo)

1. Press 4 threaded heat inserts into PSU rear sleeve
2. Slide PSU rear sleeve over PSU where the PSU goes along the bottom; align mounting holes to the left and right sides.
    - Insert four M.4 screws into the aligned PSU mounting holes
    - Note: thread the CAN terminal block wires through the PSU rear sleeve

##### Steps: PSU Front

1. Take RJ-45 terminal block, and attach with two M.3 screws into the round hole on the front face.
2. Slide STM nucleo board into the support guides (close to the top - should see divots on the left and right)
    - Note: RJ-45 should be facing toward the rear
3. Partially slide the front PSU sleeve over the PSU, such that you can then connect the CAN Transciever in the next step
4. Attach the CAN terminal block outputs wires (the wires that are currently sticking out of the PSU rear sleeve) into the CAN Transciever outputs with the following matching: `high` `low` `ground`
5. Attach the ethernet cable between the STM32 and the connection goin out of the PSU front
6. Slide the front PSU sleeve the rest of the way over the PSU and align the mounting holes of the front sleeve
    - Screw in four M.4 screws into the sides of the sleeve (into mount holes)
7. Fasten the rear plate with four M.4 screws into the PSU rear sleeve

### 3. Motor Unit Assembly


---

<div align="center">
   <sub>
      <a href="PRISM%20Bill%20of%20Materials.md">← Bill of Materials</a> · <a href="../../README.md">Back to Main README</a> · <a href="odriveparam.md">Next: ODrive Config →</a>
   </sub>
</div>