[⬅ Back to Main README](../../README.md#build-one-for-your-lab)

# Build Guide

Welcome to the PRISM Build Guide. Here you will find all the resources required to source components, print and assemble your parts, and flash the necessary firmware to get your system ready.

## Table of Contents

- [Hardware Build Guide](#hardware-build-guide)
    - [1. 3D Print Components](#1-3d-print-components)
    - [2. Device Assembly](#2-device-assembly)
    - [3. Communication Wiring](#3-communication-wiring)

## Hardware Build Guide

### 1. 3D Print Components

Use the STLs located in the `docs/Rotation Library/Handles` directory (found in the original source, or linked in the Handle Library) for structural mounting.

- Motor mounting brackets
- STM32H7 enclosure
- Odrive S1 mounting plates

### 2. Device Assembly

1. Mount the **ODrive S1** and **Nucleo-H753ZI** securely using the printed enclosures and M3 hardware.
2. Wire the power supply to the ODrive S1. *Ensure it is switched off during wiring.*
3. Install the brushless motor into the mounting brackets, coupling its shaft to the primary 8mm REX drive shaft.
4. Wire the motor phases and encoder to the ODrive S1 per ODrive specifications.

### 3. Communication Wiring

1. Connect the CAN bus between the Nucleo-H753ZI (FDCAN pins) and the ODrive CAN terminals.
2. Ensure you have a 120Ω termination resistor at both ends of the CAN bus.
3. Use a standard Micro-USB cable to connect the **CN1 (USB ST-LINK)** port of the Nucleo board to your host computer for programming. This port is typically farthest from the Ethernet port.


### Next: [Firmware Installation](../User%20Guides/firmware/firmware-installation.md)