# Valve Control Subsystem


## Table of Contents

- [Files](#files)
- [Architecture](#architecture)
- [Dependencies](#dependencies)


This directory contains the complete valve haptic control system implementation.

## Files

- **`valve_haptic.c`** - Main haptic feedback control logic
  - Real-time force feedback computation
  - PID control loops
  - Safety limits and monitoring

- **`valve_physics.c`** - Physics simulation and modeling
  - Valve dynamics modeling
  - Force/torque calculations
  - Environmental compensation

- **`valve_presets.c`** - Preset management system
  - Configuration storage and retrieval
  - Preset validation and interpolation
  - User-defined parameter sets

- **`valve_nvm.c`** - Non-volatile memory operations
  - Parameter persistence
  - Calibration data storage
  - Wear leveling

- **`valve_filters.c`** - Signal processing and filtering
  - Noise reduction
  - Derivative calculations
  - Stability enhancements

## Architecture

The valve subsystem provides:
- High-frequency control loops (1kHz+)
- Real-time safety monitoring
- Configurable control parameters
- Diagnostic and calibration interfaces

## Dependencies

- CMSIS DSP library for math operations
- Hardware drivers (ADC, PWM, encoders)
- System timing and interrupts