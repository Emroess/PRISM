# Drivers Subsystem


## Table of Contents

- [Files](#files)
- [Architecture](#architecture)
- [Dependencies](#dependencies)


This directory contains hardware abstraction layer (HAL) drivers for the STM32H7 microcontroller.

## Files

- **`uart.c`** - UART communication driver
  - Implements serial communication for CLI and debugging
  - Configured for UART4 with DMA support
  - Thread-safe operations with mutex protection

- **`fdcan.c`** - FDCAN (CAN FD) communication driver
  - Implements CAN FD protocol for high-speed communication
  - Supports extended frames and flexible data rates
  - Used for valve control and system coordination

## Architecture

Each driver provides:
- Initialization functions
- Configuration management
- Data transmission/reception
- Error handling and recovery
- Hardware abstraction for portability

## Dependencies

- STM32Cube HAL (GPIO, RCC, UART, FDCAN)
- CMSIS core definitions
- Board-specific pin configurations