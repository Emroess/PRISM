# Protocols Subsystem


## Table of Contents

- [Files](#files)
- [Architecture](#architecture)
- [Dependencies](#dependencies)


This directory contains communication protocol implementations that operate above the hardware drivers.

## Files

- **`can_simple.c`** - Simple CAN protocol for valve control
  - Lightweight protocol for valve position and status communication
  - Command/response message format
  - Error detection and retransmission

## Architecture

Protocols provide:
- Message framing and parsing
- Error correction and validation
- Flow control
- Protocol-specific state management

## Dependencies

- Hardware drivers (FDCAN)
- Valve subsystem interfaces
- System timing services