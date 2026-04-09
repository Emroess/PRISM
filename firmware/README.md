# Firmware Directory Structure


## Table of Contents

- [Directory Layout](#directory-layout)
  - [`src/`](#src)
  - [`third_party/`](#third_party)
  - [`inc/`](#inc)
  - [`ports/`](#ports)
  - [`ld/`](#ld)
  - [`startup/`](#startup)
- [Build System](#build-system)
- [Key Improvements](#key-improvements)


This document describes the reorganized firmware directory structure for improved maintainability and code organization.

## Directory Layout

### `src/`
Main source code directory, organized by functional subsystems:

- **`drivers/`** - Hardware abstraction layer (HAL) drivers
  - `uart.c` - UART communication driver
  - `fdcan.c` - FDCAN (CAN FD) communication driver

- **`protocols/`** - Communication protocol implementations
  - `can_simple.c` - Simple CAN protocol for valve control

- **`valve/`** - Valve control subsystem
  - `valve_haptic.c` - Haptic feedback control
  - `valve_physics.c` - Physics simulation and modeling
  - `valve_presets.c` - Preset management
  - `valve_nvm.c` - Non-volatile memory operations
  - `valve_filters.c` - Signal filtering

- **`network/`** - Network services and protocols
  - `net_init.c` - Network initialization (formerly app_ethernet.c)
  - `stream_server.c` - TCP streaming server (formerly ethernet_stream.c)
  - `http_server.c` - HTTP infrastructure (split from ethernet_http.c)
  - `rest_api.c` - REST API handlers (split from ethernet_http.c)
  - `ping.c` - ICMP ping functionality

- **`app/`** - Application-level interfaces
  - `cli.c` - Command-line interface
  - `perfmon.c` - Performance monitoring

### `third_party/`
External libraries and vendor code:

- **`lwip/`** - Lightweight TCP/IP stack (lwIP 2.2.1)
- **`stm32cube/`** - STMicroelectronics STM32Cube HAL and BSP

### `inc/`
Header files, mirroring the `src/` structure for clean includes.

### `ports/`
Port-specific implementations (lwIP ports for STM32).

### `ld/`
Linker scripts for different memory configurations.

### `startup/`
MCU startup code and vector tables.

## Build System

- **Makefile**: Configured for ARM GCC toolchain with MISRA-C compliance
- **C_SOURCES**: Updated to include all new subdirectory paths
- **C_INCLUDES**: Updated to include all new header paths
- **Memory Usage**: ~142KB FLASH (identical to pre-reorganization)

## Key Improvements

1. **Functional Separation**: Code organized by purpose rather than mixing concerns
2. **Clear Naming**: Explicit file names (e.g., `rest_api.c` vs `ethernet_http.c`)
3. **Third-Party Isolation**: Vendor code separated from custom implementations
4. **Scalability**: Easy to add new drivers, protocols, or features in appropriate directories
5. **Maintainability**: Engineers can quickly locate relevant code by function