<div align="center">
   <img src="../assets/images/PRISMlogo.png" width="500" alt="PRISM Logo">

   <h3>Firmware Installation Guide</h3>

   <a href="odriveparam.md">
      <img src="https://img.shields.io/badge/⬅_ODrive_Config-2ea44f?style=for-the-badge" alt="ODrive Config">
   </a>
   &nbsp;
   <a href="../../README.md">
      <img src="https://img.shields.io/badge/Back_to_README-e34c26?style=for-the-badge" alt="Back to README">
   </a>
</div>

---

> [!NOTE]
> This guide walks you through installing the PRISM firmware onto a Nucleo-H753ZI development board using the onboard ST-LINK debugger/programmer.

---

## Table of Contents

| Section | Description |
|:--------|:------------|
| [Prerequisites](#prerequisites) | Hardware and knowledge requirements |
| [Hardware Setup](#hardware-setup) | Board inspection, connection, verification |
| [Software Requirements](#software-requirements) | ARM GCC, OpenOCD, Make |
| [Building the Firmware](#building-the-firmware) | Clone, compile, verify build |
| [Flashing the Firmware](#flashing-the-firmware) | Program the board via ST-LINK |
| [Verification](#verification) | Confirm firmware is running |
| [Troubleshooting](#troubleshooting) | Common issues and fixes |
| [Advanced Options](#advanced-options) | Debug builds, GDB, Windows tools |

---

## Prerequisites

| Item | Required |
|:-----|:---------|
| **Nucleo-H753ZI** development board | Yes |
| **USB Type-A to Micro-USB cable** (for ST-LINK) | Yes |
| **Host computer** — Linux, macOS, or Windows | Yes |
| ODrive motor controller + brushless motor | Optional (for full system testing) |

> [!TIP]
> Basic familiarity with command-line terminal usage, embedded systems concepts, and the Make build system is helpful.

---

## Hardware Setup

### 1. Inspect the Board

The Nucleo-H753ZI has two USB connectors:

| Connector | Location | Purpose |
|:----------|:---------|:--------|
| **CN1 (USB ST-LINK)** | Top of board | **Use this one for programming** |
| **CN13 (USB USER)** | Near RJ45 port | Target MCU — not for programming |

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

> [!TIP]
> You may need to add your user to the `dialout` group to access the serial port:

```bash
sudo usermod -aG dialout $USER
```

Then log out and log back in for the changes to take effect.

---

## Software Requirements

### 1. ARM GCC Toolchain

The firmware is built using the ARM GCC compiler. Install it using your package manager:

<details>
<summary><strong>Ubuntu / Debian</strong></summary>
<br>

```bash
sudo apt-get update
sudo apt-get install gcc-arm-none-eabi binutils-arm-none-eabi
```

</details>

<details>
<summary><strong>Fedora / RHEL</strong></summary>
<br>

```bash
sudo dnf install arm-none-eabi-gcc-cs arm-none-eabi-binutils
```

</details>

<details>
<summary><strong>macOS (Homebrew)</strong></summary>
<br>

```bash
brew install --cask gcc-arm-embedded
```

</details>

Verify installation:

```bash
arm-none-eabi-gcc --version
```

### 2. OpenOCD

OpenOCD (Open On-Chip Debugger) is used to program and debug the microcontroller via ST-LINK.

<details>
<summary><strong>Ubuntu / Debian</strong></summary>
<br>

```bash
sudo apt-get install openocd
```

</details>

<details>
<summary><strong>Fedora / RHEL</strong></summary>
<br>

```bash
sudo dnf install openocd
```

</details>

<details>
<summary><strong>macOS (Homebrew)</strong></summary>
<br>

```bash
brew install openocd
```

</details>

Verify installation:

```bash
openocd --version
```

> [!NOTE]
> Expected output should show version 0.11.0 or newer.

### 3. Make

The build system uses GNU Make:

<details>
<summary><strong>Ubuntu / Debian</strong></summary>
<br>

```bash
sudo apt-get install build-essential
```

</details>

<details>
<summary><strong>Fedora / RHEL</strong></summary>
<br>

```bash
sudo dnf groupinstall "Development Tools"
```

</details>

<details>
<summary><strong>macOS</strong></summary>
<br>

Make is included with Xcode Command Line Tools:

```bash
xcode-select --install
```

</details>

### 4. Optional: Serial Monitor

To interact with the firmware via UART, install a serial terminal:

| Tool | Command |
|:-----|:--------|
| `screen` | `screen /dev/ttyACM0 115200` |
| `picocom` | `picocom -b 115200 /dev/ttyACM0` |
| `minicom` | `minicom -D /dev/ttyACM0 -b 115200` |
| PuTTY (GUI) | `sudo apt-get install putty` |

---

## Building the Firmware

### Download the Firmware

```bash
git clone https://github.com/Emroess/PRISM.git
```

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

> [!WARNING]
> Flashing the firmware to the Nucleo-STM32H7ZI while a CAN transceiver is connected may result in an OpenOCD core timeout error. Disconnect the transceiver before flashing.

The simplest way to flash the firmware is using the provided Makefile target:

```bash
cd /path/to/PRISM/firmware
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

<details>
<summary><strong>Method 2 — Manual OpenOCD Command</strong></summary>
<br>

If you want more control, you can run OpenOCD directly:

```bash
openocd -f interface/stlink.cfg -f target/stm32h7x.cfg \
    -c "program build/firmware.elf verify reset exit"
```

</details>

<details>
<summary><strong>Method 3 — Interactive OpenOCD Session</strong></summary>
<br>

For debugging or manual control:

```bash
openocd -f interface/stlink.cfg -f target/stm32h7x.cfg
```

In another terminal, connect via telnet:

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

</details>

---

## Verification

After flashing and reset, run through this checklist:

| Step | Action | Expected Result |
|:----:|:-------|:----------------|
| 1 | Observe board LEDs | LD1 blinks (activity), LD2 solid green (power) |
| 2 | Connect serial terminal | `screen /dev/ttyACM0 115200` |
| 3 | Press **Enter** | You see `PRISM>` prompt |
| 4 | Type `help` | Full command list displays |
| 5 | Type `version` | Firmware version string |
| 6 | Type `status` | CPU, memory, network, motor controller status |

> [!TIP]
> To exit `screen`: Press `Ctrl+A`, then `K`, then `Y`.

---

## Troubleshooting

<details>
<summary><strong>OpenOCD Can't Find ST-LINK</strong> — <code>Error: open failed</code></summary>
<br>

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

</details>

<details>
<summary><strong>Permission Denied on /dev/ttyACM0</strong> — <code>/dev/ttyACM0: Permission denied</code></summary>
<br>

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

</details>

<details>
<summary><strong>Target Voltage Detected as 0V</strong> — <code>Error: Target voltage may be too low for reliable debugging</code></summary>
<br>

**Error:**

```
Error: Target voltage may be too low for reliable debugging
```

**Solutions:**

1. Ensure board is powered (LD2 LED should be on)
2. Check USB cable is properly connected
3. Try a different USB port or cable
4. Check if JP5 jumper is set correctly (should connect ST-LINK MCU to target)

</details>

<details>
<summary><strong>Build Fails - Command Not Found</strong> — <code>arm-none-eabi-gcc: command not found</code></summary>
<br>

**Error:**

```
arm-none-eabi-gcc: command not found
```

**Solution:**
Install ARM GCC toolchain (see [Software Requirements](#software-requirements))

</details>

<details>
<summary><strong>OpenOCD Times Out</strong> — <code>Error: timed out while waiting for target halted</code></summary>
<br>

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

</details>

<details>
<summary><strong>Verification Failed</strong> — <code>Error: verification failed</code></summary>
<br>

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

</details>

<details>
<summary><strong>No Serial Port Appears</strong></summary>
<br>

**Problem:**
The `/dev/ttyACM0` device doesn't appear after flashing.

**Solutions:**

1. Check firmware has UART initialized properly
2. Try different USB cable
3. Check dmesg for errors: `dmesg | tail -20`
4. Verify CDC-ACM driver is loaded: `lsmod | grep cdc_acm`

</details>


---

## Advanced Options

<details>
<summary><strong>Building with Different Optimization Levels</strong></summary>
<br>

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

</details>

<details>
<summary><strong>Running Validation Checks</strong></summary>
<br>

Before committing changes, run the full validation suite:

```bash
make validate-all
```

This runs:

- Dual-build validation (-O0 and -O2)
- MISRA-C compliance checks (requires cppcheck)
- Hardware access layering validation

</details>

<details>
<summary><strong>Checking Memory Usage</strong></summary>
<br>

View detailed memory usage:

```bash
make size
```

This displays:

- Flash usage (text + data sections)
- RAM usage (data + bss sections)
- Breakdown by section

</details>

<details>
<summary><strong>Starting Debug Session</strong></summary>
<br>

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

</details>

<details>
<summary><strong>Using ST-LINK Utility (Windows)</strong></summary>
<br>

On Windows, you can use STM32CubeProgrammer instead of OpenOCD:

1. Download and install [STM32CubeProgrammer](https://www.st.com/en/development-tools/stm32cubeprog.html)
2. Launch STM32CubeProgrammer
3. Connect via ST-LINK (USB)
4. Load `build/firmware.hex` or `build/firmware.elf`
5. Click "Download"

</details>


---

## Additional Resources

| Resource | Link |
|:---------|:-----|
| Firmware README | `firmware/README.md` |
| Makefile | `firmware/Makefile` |
| CLI Reference | [CLI Reference](../User%20Guides/cli/CLI%20Reference.md) |
| REST API | [REST API](../User%20Guides/rest/rest-api.md) |
| Nucleo-H753ZI User Manual | [ST Documentation](https://www.st.com/resource/en/user_manual/um2407-stm32h7-nucleo144-boards-mb1364-stmicroelectronics.pdf) |
| OpenOCD Manual | [OpenOCD Docs](http://openocd.org/doc/html/index.html) |

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

| Item | Path |
|:-----|:-----|
| Firmware source | `firmware/src/` |
| Build outputs | `firmware/build/` |
| Linker script | `firmware/ld/STM32H753ZITx_FLASH.ld` |
| Makefile | `firmware/Makefile` |

### Board Connectors

| Connector | Purpose |
|:----------|:--------|
| **CN1** | ST-LINK USB (Micro-USB) — **Use for programming** |
| **CN13** | User USB (Type-C) — Target MCU USB |
| **CN11/CN12** | Arduino connectors |
| **CN7–CN10** | Morpho headers |
| **B1** | USER button (blue) |
| **B2** | RESET button (black) |

---

<div align="center">
   <sub>
      <a href="odriveparam.md">← ODrive Config</a> · <a href="../../README.md">Back to Main README</a>
   </sub>
</div>