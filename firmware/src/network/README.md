# Network Services Subsystem


## Table of Contents

- [Files](#files)
- [Architecture](#architecture)
- [Dependencies](#dependencies)


This directory contains all network-related functionality for remote control and monitoring.

## Files

- **`net_init.c`** - Network stack initialization
  - Ethernet hardware setup
  - IP configuration (static/DHCP)
  - lwIP stack initialization

- **`stream_server.c`** - TCP streaming server
  - Real-time data streaming
  - High-throughput data transmission
  - Connection management

- **`http_server.c`** - HTTP infrastructure
  - HTTP/1.1 protocol implementation
  - Request parsing and routing
  - Response generation

- **`rest_api.c`** - REST API handlers
  - JSON-based API endpoints
  - Configuration management
  - Status reporting

- **`ping.c`** - ICMP ping functionality
  - Network connectivity testing
  - Diagnostic utilities

## Architecture

Network services provide:
- RESTful API for configuration and control
- Real-time data streaming
- Remote CLI access
- Network diagnostics

## Dependencies

- lwIP TCP/IP stack
- JSMN JSON parser
- Valve and system interfaces
- Hardware Ethernet driver