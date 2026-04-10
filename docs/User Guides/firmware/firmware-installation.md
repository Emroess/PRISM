[⬅ Back to Main README](../../../README.md#Build%20One%20For%20Your%20Lab)

# Firmware Installation Guide

This guide walks you through installing the PRISM firmware onto a Nucleo-H753ZI development board using the onboard ST-LINK debugger/programmer.

## Table of Contents

- [Prerequisites](#prerequisites)
- [Hardware Setup](#hardware-setup)
- [Software Requirements](#software-requirements)
- [Building the Firmware](#building-the-firmware)
- [Flashing the Firmware](#flashing-the-firmware)
- [Verification](#verification)
- [Troubleshooting](#troubleshooting)
- [Advanced Options](#advanced-options)

---

## Prerequisites

### Required Hardware

- **Nucleo-H753ZI** development board
- **USB Type-A to Micro-USB cable** (for ST-LINK connection)
- **Host computer** running Linux, macOS, or Windows
- *Optional*: ODrive motor controller and brushless motor for full system testing

### Knowledge Requirements

Basic familiarity with:

- Command-line terminal usage
- Embedded systems concepts
- Make build system

---

## Hardware Setup

### 1. Inspect the Board

The Nucleo-H753ZI has two USB connectors:

- **CN1 (USB ST-LINK)**: Micro-USB connector at the top of the board - **Use this one for programming**
- **CN13 (USB USER)**: Micro-USB connector close to RJ45 network port- This is for the target MCU, not for programming

### 2. Connect the Board

1. Connect the **Micro-USB cable** to the **CN1 (ST-LINK)** connector
2. Connect the other end to your host computer
3. The board should power on - you should see the following LEDs:

   - **LD1 (COM)**: Red/Green - Indicates ST-LINK communication
   - **LD2 (Power)**: Green - Board is powered

  <!-- **LD3 (PWR)**: Red - 3.3V rail is active
-->

### 3. Verify Connection (Linux)

After connecting, verify the ST-LINK is detected:

```bash
lsusb | grep STMicroelectronics
```

Expected output:

```
Bus 001 Device 005: ID 0483:374b STMicroelectronics ST-LINK/V2-1
```

Check if the device appears in `/dev`:

```bash
ls -la /dev/ttyACM*
```

Expected output:

```
crw-rw---- 1 root dialout 166, 0 Nov 21 10:30 /dev/ttyACM0
```

**Note**: You may need to add your user to the `dialout` group to access the serial port:

```bash
sudo usermod -aG dialout $USER
```

Then log out and log back in for the changes to take effect.

---

## Software Requirements

### 1. ARM GCC Toolchain

The firmware is built using the ARM GCC compiler. Install it using your package manager:

**Ubuntu/Debian:**

```bash
sudo apt-get update
sudo apt-get install gcc-arm-none-eabi binutils-arm-none-eabi
```

**Fedora/RHEL:**

```bash
sudo dnf install arm-none-eabi-gcc-cs arm-none-eabi-binutils
```

**macOS (Homebrew):**

```bash
brew install --cask gcc-arm-embedded
```

Verify installation:

```bash
arm-none-eabi-gcc --version
```

### 2. OpenOCD

OpenOCD (Open On-Chip Debugger) is used to program and debug the microcontroller via ST-LINK.

**Ubuntu/Debian:**

```bash
sudo apt-get install openocd
```

**Fedora/RHEL:**

```bash
sudo dnf install openocd
```

**macOS (Homebrew):**

```bash
brew install openocd
```

Verify installation:

```bash
openocd --version
```

Expected output should show version 0.11.0 or newer.

### 3. Make

The build system uses GNU Make:

**Ubuntu/Debian:**

```bash
sudo apt-get install build-essential
```

**Fedora/RHEL:**

```bash
sudo dnf groupinstall "Development Tools"
```

**macOS:**
Make is included with Xcode Command Line Tools:

```bash
xcode-select --install
```

### 4. Optional: Serial Monitor

To interact with the firmware via UART, install a serial terminal:

**Option A: screen** (usually pre-installed)

```bash
screen /dev/ttyACM0 115200
```

**Option B: picocom** 

```bash
picocom -b 115200 /dev/ttyACM0 
```

**Option C: minicom**

```bash
sudo apt-get install minicom
minicom -D /dev/ttyACM0 -b 115200
```

**Option D: PuTTY** (GUI option)

```bash
sudo apt-get install putty
```

---

## Building the Firmware

### 1. Navigate to Firmware Directory

```bash
cd /path/to/PRISM/firmware
```

### 2. Clean Previous Builds (Optional)

If you've built before, clean the build directory:

```bash
make clean
```

This removes all compiled object files and binaries from the `build/` directory.

### 3. Build the Firmware

Build with default optimization (-O2):

```bash
make
```

Or build with debug symbols (no optimization):

```bash
make CFLAGS_OPT=-O0
```

### 4. Build Output

The build process will:

1. Compile all C source files to object files (`.o`)
2. Generate dependency files (`.d`)
3. Link everything into an ELF executable
4. Generate additional formats (HEX, BIN)
5. Display memory usage

Expected output:

```
Compiling: src/main.c
Compiling: src/bsp/board.c
...
Linking: build/firmware.elf
Creating hex file: build/firmware.hex
Creating binary file: build/firmware.bin

Memory Usage:
   text    data     bss     dec     hex filename
 145892    1560   72840  220292   35c84 build/firmware.elf

Build complete!
```

### 5. Verify Build Artifacts

Check that the firmware binaries were created:

```bash
ls -lh build/firmware.*
```

You should see:

- `firmware.elf` - Executable with debug symbols
- `firmware.hex` - Intel HEX format
- `firmware.bin` - Raw binary format
- `firmware.map` - Memory map file

---

## Flashing the Firmware

> **_NOTE:_** Flashing the firmware to the Nucleo-STM32H7ZI while a CAN transceiver is connected might result in a OpenOCD core timeout error.

The simplest way to flash the firmware is using the provided Makefile target:

```bash
make flash
```

This command:

1. Builds the firmware (if not already built)
2. Launches OpenOCD with ST-LINK interface configuration
3. Programs the firmware to flash memory
4. Verifies the programmed data
5. Resets the target MCU
6. Exits

**Expected output:**

```
openocd -f interface/stlink.cfg -f target/stm32h7x.cfg \
    -c "program build/firmware.elf verify reset exit"
Open On-Chip Debugger 0.11.0
Licensed under GNU GPL v2
...
Info : device id = 0x450
Info : flash size = 2048 kbytes
Warn : Adding extra erase range, 0x08024000 .. 0x0803ffff
** Programming Started **
** Programming Finished **
** Verify Started **
** Verified OK **
** Resetting Target **
shutdown command invoked
```

### Method 2: Manual OpenOCD Command

If you want more control, you can run OpenOCD directly:

```bash
openocd -f interface/stlink.cfg -f target/stm32h7x.cfg \
    -c "program build/firmware.elf verify reset exit"
```

### Method 3: Interactive OpenOCD Session

For debugging or manual control:

```bash
openocd -f interface/stlink.cfg -f target/stm32h7x.cfg
```

This starts an interactive OpenOCD session. In another terminal, connect via telnet:

```bash
telnet localhost 4444
```

Then manually program:

```
> halt
> flash write_image erase build/firmware.elf
> verify_image build/firmware.elf
> reset run
> exit
```

---

## Verification

### 1. Check LED Behavior

After flashing and reset, observe the board LEDs:

- **LD1 (Green/User LED)**: Should blink or show activity patterns defined in the firmware
- **LD2 (Power)**: Should remain solid green

### 2. Connect via Serial Terminal

Connect to the UART console to verify firmware is running:

```bash
screen /dev/ttyACM0 115200
```

Press Enter - you should see the CLI prompt:

```
PRISM> 
```

Type `help` to see available commands:

```
PRISM> help
```

### 3. Check Firmware Version

Query the firmware version:

```
PRISM> version
```

### 4. Check System Status

View system status:

```
PRISM> status
```

This should display:

- CPU usage
- Memory usage
- Network status
- Motor controller status

### 5. Exit Serial Monitor

To exit `screen`: Press `Ctrl+A`, then `K`, then `Y`

---

## Troubleshooting

### Problem: OpenOCD Can't Find ST-LINK

**Error:**

```
Error: open failed
```

**Solutions:**

1. Check USB cable is connected to CN1 (ST-LINK port)
2. Verify ST-LINK is detected: `lsusb | grep STM`
3. Check udev rules (Linux):

   ```bash
   sudo cp /usr/share/openocd/contrib/60-openocd.rules /etc/udev/rules.d/
   sudo udevadm control --reload-rules
   sudo udevadm trigger
   ```
4. Try running with sudo: `sudo make flash`

### Problem: Permission Denied on /dev/ttyACM0

**Error:**

```
/dev/ttyACM0: Permission denied
```

**Solution:**
Add your user to the `dialout` group:

```bash
sudo usermod -aG dialout $USER
```

Then log out and back in.

### Problem: Target Voltage Detected as 0V

**Error:**

```
Error: Target voltage may be too low for reliable debugging
```

**Solutions:**

1. Ensure board is powered (LD2 LED should be on)
2. Check USB cable is properly connected
3. Try a different USB port or cable
4. Check if JP5 jumper is set correctly (should connect ST-LINK MCU to target)

### Problem: Build Fails - Command Not Found

**Error:**

```
arm-none-eabi-gcc: command not found
```

**Solution:**
Install ARM GCC toolchain (see [Software Requirements](#software-requirements))

### Problem: OpenOCD Times Out

**Error:**

```
Error: timed out while waiting for target halted
```

**Solutions:**

1. Press the black RESET button (B2) on the board
2. Disconnect and reconnect USB cable
3. Try lower SWD speed:

   ```bash
   openocd -f interface/stlink.cfg -f target/stm32h7x.cfg \
       -c "adapter speed 1000" \
       -c "program build/firmware.elf verify reset exit"
   ```

### Problem: Verification Failed

**Error:**

```
Error: verification failed
```

**Solutions:**

1. Try erasing the flash first:

   ```bash
   openocd -f interface/stlink.cfg -f target/stm32h7x.cfg \
       -c "init" -c "reset halt" -c "flash erase_sector 0 0 last" \
       -c "reset" -c "exit"
   ```
1. Then reflash: `make flash`

### Problem: No Serial Port Appears

**Problem:**
The `/dev/ttyACM0` device doesn't appear after flashing.

**Solutions:**

1. Check firmware has UART initialized properly
2. Try different USB cable
3. Check dmesg for errors: `dmesg | tail -20`
4. Verify CDC-ACM driver is loaded: `lsmod | grep cdc_acm`

---

## Advanced Options

### Building with Different Optimization Levels

**Debug build (no optimization):**

```bash
make clean
make CFLAGS_OPT=-O0
```

**Size-optimized build:**

```bash
make clean
make CFLAGS_OPT=-Os
```

**Release build (default):**

```bash
make clean
make CFLAGS_OPT=-O2
```

### Running Validation Checks

Before committing changes, run the full validation suite:

```bash
make validate-all
```

This runs:

- Dual-build validation (-O0 and -O2)
- MISRA-C compliance checks (requires cppcheck)
- Hardware access layering validation

### Checking Memory Usage

View detailed memory usage:

```bash
make size
```

This displays:

- Flash usage (text + data sections)
- RAM usage (data + bss sections)
- Breakdown by section

### Starting Debug Session

Launch GDB debugging session:

```bash
make debug
```

This:

1. Starts OpenOCD in background
2. Launches GDB and connects to target
3. Loads symbols from ELF file

In GDB, you can:

- Set breakpoints: `break main`
- Continue execution: `continue`
- Single-step: `step` or `next`
- Inspect variables: `print variable_name`
- View backtrace: `backtrace`

### Using ST-LINK Utility (Windows)

On Windows, you can use STM32CubeProgrammer instead of OpenOCD:

1. Download and install [STM32CubeProgrammer](https://www.st.com/en/development-tools/stm32cubeprog.html)
2. Launch STM32CubeProgrammer
3. Connect via ST-LINK (USB)
4. Load `build/firmware.hex` or `build/firmware.elf`
5. Click "Download"

---

## Next Steps

After successfully installing the firmware:

1. **Explore the CLI**: Connect via serial and run `help` to see available commands
2. **Configure Network**: Set up Ethernet connection for REST API access (see [Network Configuration](../rest/rest-api.md))
3. **Connect Motor Controller**: Wire up the ODrive and configure CAN bus
4. **Test Haptic Control**: Try different valve presets and feel characteristics
5. **Review Documentation**: Read the full [documentation](../README.md) for detailed usage

---

## Additional Resources

- **Firmware README**: `firmware/README.md` - Architecture overview
- **Makefile**: `firmware/Makefile` - Build system details
- **CLI Reference**: [CLI Reference Guide](../cli/cli-reference.md)
- **REST API**: [REST API Documentation](../rest/rest-api.md)
- **Hardware**: [Nucleo-H753ZI User Manual](https://www.st.com/resource/en/user_manual/um2407-stm32h7-nucleo144-boards-mb1364-stmicroelectronics.pdf)
- **OpenOCD Manual**: [OpenOCD Documentation](http://openocd.org/doc/html/index.html)

---

## Quick Reference

### Common Commands

```bash
# Build firmware
make

# Flash firmware
make flash

# Clean build
make clean

# View memory usage
make size

# Start serial monitor
screen /dev/ttyACM0 115200

# Check ST-LINK connection
lsusb | grep STM

# View help
make help
```

### File Locations

- Firmware source: `firmware/src/`
- Build outputs: `firmware/build/`
- Linker script: `firmware/ld/STM32H753ZITx_FLASH.ld`
- Makefile: `firmware/Makefile`

### Board Connectors

- **CN1**: ST-LINK USB (Micro-USB) - **Use for programming**
- **CN13**: User USB (Type-C) - Target MCU USB
- **CN11/CN12**: Arduino connectors
- **CN7/CN8/CN9/CN10**: Morpho headers
- **B1**: USER button (blue)
- **B2**: RESET button (black)

---

**Document Version**: 1.0  
**Last Updated**: November 21, 2025  
**Target Hardware**: Nucleo-H753ZI (MB1364)  
**Firmware Version**: Compatible with PRISM firmware v1.x