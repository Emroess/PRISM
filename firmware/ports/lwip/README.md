# lwIP Port Layer


## Table of Contents

- [Files](#files)
- [Purpose](#purpose)


This directory contains the platform-specific port and adaptation files for integrating the lwIP TCP/IP stack with the STM32H7 microcontroller in a bare-metal environment.

## Files

- `lwipopts.h` - Configuration options for lwIP, including memory settings, protocol enables, and hardware-specific optimizations
- `lwippools.h` - Memory pool definitions for lwIP buffers
- `sys_arch.c` - System abstraction layer (minimal implementation for NO_SYS=1)
- `ethernetif.c` / `ethernetif.h` - Ethernet interface driver connecting lwIP to STM32H7 ETH peripheral
- `arch/` - Architecture-specific header files (cc.h for compiler abstraction)

## Purpose

These files adapt the generic lwIP stack to work with:
- STM32H7 hardware (ETH peripheral, LAN8742 PHY)
- Bare-metal environment (NO_SYS=1, no RTOS)
- Hardware checksum offloading
- Custom memory management
- Static IP configuration

This separation allows the core lwIP library in `../third_party/lwip/` to remain unmodified while containing all platform-specific customizations here.