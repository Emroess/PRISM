# Getting Started

Welcome to PRISM! This section guides you through building the physical device, flashing the firmware, and establishing your first connection.

## 1. Hardware Assembly & CAD
Before running the software, ensure your PRISM device is fully assembled.
- **[Hardware & CAD Instructions](CAD/README.md)**: Find the bill of materials, 3D printing files, and mechanical assembly steps.
- **[ODrive Configuration](odrive/odriveparam.md)**: Steps to correctly configure the ODrive motor controller.

## 2. Firmware Installation
PRISM runs on an STM32H7 microcontroller. Install the bare-metal C firmware to enable network, CAN bus, and haptic capabilities.
- **[Firmware Installation Guide](firmware/firmware-installation.md)**: Setup ARM GCC, OpenOCD, and flash the Nucleo board.

## 3. Initial Setup & First Run
Once the hardware is built and flashed, connect and test your device.
- **[Quick Setup & Connecting](getting-started/getting-started.md)**: Learn how to connect to the serial terminal, enable the motor, and verify operation.
