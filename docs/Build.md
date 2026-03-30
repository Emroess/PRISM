# PRISM Build Guide

This guide covers everything you need to build the PRISM (Programmable Rotary Impedance Suite for Manipulation) system. It details the Bill of Materials (BOM), cost estimates, hardware assembly, initial firmware flashing, and diagnostics.

## Bill of Materials (BOM) & Cost

To build a complete PRISM unit, you will need the following core components:

### Core Hardware
| Item | Description | Estimated Cost |
|------|-------------|----------------|
| **STM32 Nucleo-H753ZI** | Development board and main microcontroller. | ~$30.00 |
| **ODrive S1** | Motor controller for the brushless motor. | ~$150.00 |
| **Brushless Motor** | Compatible high-torque brushless motor for haptic feedback. | ~$50.00-$100.00 |
| **Power Supply** | 12-24V power supply capable of handling peak motor draw. | ~$20.00-$40.00 |
| **Miscellaneous Hardware** | Wires, M3 screws for mounting, CAN bus termination resistors (120Ω), connectors. | ~$20.00 |

### Emulation Handles (Select one or more)
*Refer to the [Rotation Library](Rotation_Library.md) for full details on each handle.*

- **Hydrant Handwheel:** [Handwheel](https://a.co/d/cDYmmxf) + [goBILDA 1310 Series Hyper Hub (8mm REX)](https://www.gobilda.com/1310-series-hyper-hub-8mm-rex-bore/) (~$20.00)
- **Quarter-Turn Handle:** [Valve Handle](https://a.co/d/isgp3Wc) + [goBILDA 1310 Series Hyper Hub](https://www.gobilda.com/1310-series-hyper-hub-8mm-rex-bore/) (~$15.00)
- **Door Handle:** [Lever Handle](https://a.co/d/iUJtJVq) + [goBILDA REX Shaft 48mm](https://www.gobilda.com/2106-series-stainless-steel-rex-shaft-8mm-diameter-48mm-length/) + [goBILDA Hyper Coupler](https://www.gobilda.com/4007-series-hyper-coupler-8mm-rex-bore-to-8mm-rex-bore/) (~$25.00)
- **Wrench Tightening:** [8mm Wrench](https://a.co/d/g34gK7M) (~$10.00)

**Total Estimated Cost:** ~$300.00 - $400.00 (depending on handle selection and existing equipment)

---

## Hardware Build Guide

### 1. 3D Print Components
Use the STLs located in the `docs/CAD/` directory (found in the original source, or linked in the Handle Library) for structural mounting.
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

---

## Initial Firmware Flash

Before the system can run, you must compile and flash the embedded firmware to the STM32 microcontroller. 

### Software Requirements
You will need a Linux, macOS, or Windows host equipped with:
- **ARM GCC Toolchain:** (`gcc-arm-none-eabi`, `binutils-arm-none-eabi`)
- **OpenOCD:** For flashing via ST-LINK.
- **Micro-USB Cable:** Connect to CN1 port on the Nucleo board.

### Build & Flash Process
1. Navigate to the firmware directory in your terminal:
   ```bash
   cd firmware/
   ```
2. Build the firmware using Make:
   ```bash
   make
   ```
   *This compiles the C source code into an ELF and binary file, utilizing the lwIP TCP/IP stack and STM32Cube HAL.*
3. Flash the firmware onto the Nucleo board:
   ```bash
   make flash
   ```
   *This runs OpenOCD, programs the board, and resets the target target MCU.*

**Note:** Flashing while a CAN transceiver is active or connected to a powered ODrive might occasionally cause OpenOCD timeouts. Power off the ODrive logic if this happens repeatedly.

---

## System Diagnosis & Troubleshooting

After assembly and flashing, verify the system is behaving correctly. 

### Basic System Check
1. Connect via Serial Terminal (115200 baud) to the Nucleo board (`/dev/ttyACM0` on Linux, or COM port on Windows). For more details / other connection options, see here: [Opening A Serial Terminal](getting-started/getting-started.md#opening-a-serial-terminal)
2. Press `Enter`. You should see the `STEVE>` prompt.
3. Test the ODrive connection:
   ```
   STEVE> odrive_ping
   ```
   *Expected:* `ODrive heartbeat received`
4. If the heartbeat fails, check the CAN bus wiring (CAN_H, CAN_L, GND), termination resistors, and verify the ODrive is powered on.

### Debugging Common Issues
- **Motor Oscillates or Vibrates:** 
  - Reduce damping: `valve_damping 0.03`
  - Lower torque limit: `valve_torquelimit 0.3`
  - Verify encoder is working: `can_encoder`
- **Valve Control Won't Start (`valve_start` fails):**
  - Ensure ODrive is enabled first (`odrive_enable`).
  - Clear any ODrive hard faults (`odrive_clear`, `fault_last`).
- **Cannot Access Ethernet/Web Server:**
  - Verify network cable is connected.
  - Run `ethstatus` to check the IP address.
  - Start HTTP server: `http start`.

For more in-depth operation and software control, continue to the [User Guide](User.md).
