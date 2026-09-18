# STEVE: Simulated Task Exploration | Valve Emulation

**Welcome to STEVE** - A high-fidelity haptic feedback platform for valve simulation and research.

## Overview

STEVE (Simulated Task Exploration | Valve Emulation) is an advanced haptic control system designed to simulate realistic valve operation with programmable haptic characteristics. The system provides precise force feedback through a motor-driven interface, enabling research, training, and development applications in valve control and haptic interaction.

Built on the STM32H753ZI microcontroller with an ODrive motor controller, STEVE delivers real-time haptic rendering with fast response times and sophisticated physics-based feedback models.

## Key Features

### Haptic Control
- **Real-time force feedback** at 1 kHz+ control loop rates
- **Programmable valve characteristics** - damping, friction, stiffness
- **Virtual boundaries** with configurable wall stiffness and damping
- **Passivity-based control** ensuring stable energy-dissipative behavior
- **Multiple presets** for different valve types and scenarios

### Hardware Platform
- **STM32H753ZI** ARM Cortex-M7 microcontroller (480 MHz)
- **ODrive motor controller** for high-performance brushless motor control
- **CAN bus communication** for low-latency motor commands
- **Ethernet connectivity** for remote monitoring and control
- **USB serial interface** via onboard ST-LINK

### Software Architecture
- **MISRA-C:2012 compliant** embedded firmware
- **Modular design** with clear separation of concerns
- **lwIP TCP/IP stack** for networking features
- **CMSIS-DSP library** for optimized math operations
- **Real-time performance monitoring** and diagnostics

### User Interfaces

The valve feel is controlled by several key parameters:

**Command-Line Interface (CLI)** over USB serial
- Full control and configuration access
- Real-time status monitoring
- Diagnostic commands
- Direct hardware interaction
- See [CLI Reference Guide](cli/cli-reference.md)

**REST API** over Ethernet
- Remote control and monitoring
- JSON-based requests and responses
- X-API-Key authentication (default: `steve-valve-2025`)
- Suitable for automation and integration
- Robotics and testing frameworks
- Port 8080, base path `/api/v1`
- See [REST API Reference](rest/rest-api.md) and [Examples](rest/rest-api-examples.md)

**Web-based control panel** with embedded HTTP server
- Browser-based interface (port 8080)
- Real-time position, velocity, torque visualization with sparkline graphs
- Interactive parameter tuning and preset management
- Start/stop controls and live configuration updates
- No installation required - works in any modern browser
- See [Web Interface Guide](html/web-interface-guide.md)

**Real-time data streaming** for logging and visualization
- TCP streaming on port 8888
- JSON format for easy parsing
- Configurable intervals (specify in milliseconds)
- Multiple simultaneous clients (up to 6)
- Position, velocity, torque, and diagnostics
- See [Streaming Guide](stream/streaming-guide.md) and [Examples](stream/streaming-examples.md)

**PRISM-LOG session files** for offline analysis
- Canonical `.log` format covering kinematics, haptic torque, motor electricals, thermal, CAN, timing, and HITL
- Localhost viewer with default position / velocity / torque plots
- See [PRISM Log Format 1.0](log/prism-log-standard.md) and [Log Viewer](../tools/log_viewer/README.md)

## Configuration and Customization

### Haptic Parameters

### Hardware Components

```
┌─────────────────────────────────────────┐
│  STM32 Nucleo-H753ZI                   │
│  ┌──────────────────────────────────┐  │
│  │  ARM Cortex-M7 @ 480 MHz         │  │
│  │  - Valve control algorithms      │  │
│  │  - Network stack (lwIP)          │  │
│  │  - CLI interface                 │  │
│  │  - Performance monitoring        │  │
│  └──────────────────────────────────┘  │
│                                         │
│  ┌─────────┐  ┌─────────┐  ┌────────┐ │
│  │ CAN Bus │  │ Ethernet│  │  USB   │ │
│  └────┬────┘  └────┬────┘  └───┬────┘ │
└───────┼───────────┼────────────┼──────┘
        │           │            │
    ┌───┴───┐   ┌───┴───┐    ┌──┴──┐
    │ODrive │   │Network│    │ PC  │
    │       │   │ Host  │    │     │
    └───┬───┘   └───────┘    └─────┘
        │
    ┌───┴────┐
    │ Motor  │
    │Encoder │
    └────────┘
```

### Software Modules

**Application Layer:**
- `cli.c` - Command-line interface
- `perfmon.c` - Performance monitoring

**Valve Subsystem:**
- `valve_haptic.c` - Main haptic control logic
- `valve_physics.c` - Physics modeling
- `valve_presets.c` - Configuration management
- `valve_nvm.c` - Non-volatile storage
- `valve_filters.c` - Signal processing

**Network Layer:**
- `http_server.c` - Web interface
- `rest_api.c` - RESTful API handlers
- `stream_server.c` - Real-time data streaming
- `net_init.c` - Network initialization
- `ping.c` - Network diagnostics

**Protocol Layer:**
- `can_simple.c` - ODrive CAN protocol
- `odrive_manager.c` - Motor control abstraction

**Driver Layer:**
- `uart.c` - Serial communication
- `fdcan.c` - CAN bus driver
- `ethernet.c` - Ethernet MAC/PHY

## Applications

### Research and Development
- Haptic interface design and evaluation
- Human factors studies in valve operation
- Control algorithm development and testing
- Psychophysical experiments in force perception
- Automated testing via REST API

### Training and Education
- Operator training for industrial valve systems
- Educational demonstrations of control theory
- Hands-on learning for mechatronics students
- Remote training with networked haptic feedback
- Web-based interactive demonstrations

### Product Development
- Virtual prototyping of valve designs
- User experience testing before physical prototypes
- Rapid iteration on feel characteristics
- Cost-effective design exploration
- API-driven automated parameter optimization

### Testing and Validation
- Automated testing of valve control algorithms
- Characterization of haptic parameter effects
- Performance benchmarking and validation
- Quality assurance for production systems
- Continuous integration testing via REST API

### Robotics Integration
- Haptic feedback for teleoperation systems
- Force rendering in manipulation tasks
- Human-robot interaction research
- Bilateral control applications
- ROS and custom robotics frameworks

## Getting Started

### Documentation

This documentation set includes:

#### Getting Started

1. **[Firmware Installation Guide](firmware/firmware-installation.md)** - Install firmware on Nucleo-H753ZI
   - Hardware setup and connections
   - Software prerequisites (ARM GCC, OpenOCD, Make)
   - Building and flashing firmware
   - Verification and troubleshooting

2. **[Getting Started Guide](getting-started/getting-started.md)** - Connect and run your first commands
   - Hardware setup and USB connection
   - Serial terminal configuration
   - Basic operation workflow
   - Common tasks and troubleshooting

#### Command-Line Interface (CLI)

3. **[CLI Reference Guide](cli/cli-reference.md)** - Complete command documentation
   - All available commands with detailed descriptions
   - Usage examples and parameter explanations
   - Organized by functional category
   - Quick reference tables

#### REST API

4. **[REST API Reference](rest/rest-api.md)** - Complete HTTP API documentation
   - All endpoints with request/response formats
   - Authentication with X-API-Key header
   - JSON schemas and data types
   - Error handling and status codes
   - Best practices for robotics integration

5. **[REST API Examples](rest/rest-api-examples.md)** - Practical code examples
   - Python client library and examples
   - JavaScript/Node.js integration
   - curl command examples
   - C++ integration code
   - ROS 2 node implementation
   - Complete workflows and automation scripts

#### TCP Data Streaming

6. **[Streaming Guide](stream/streaming-guide.md)** - Real-time data streaming
   - TCP streaming setup and configuration
   - JSON data format and available fields
   - Configurable intervals (specify in milliseconds)
   - Connection and troubleshooting
   - Performance considerations

7. **[Streaming Examples](stream/streaming-examples.md)** - Streaming integration code
   - Python streaming client with threading
   - C++ with Boost.Asio
   - JavaScript/Node.js examples
   - ROS 2 subscriber node
   - MATLAB data acquisition
   - CSV data logging

#### Web Interface

8. **[Web Interface Guide](html/web-interface-guide.md)** - Embedded browser control panel
   - Access via any web browser on your network
   - Real-time sparkline visualization
   - Interactive valve control and preset management
   - Zero installation required
   - Troubleshooting and mobile access

### Quick Start - CLI

1. **Connect** your STEVE device via USB (ST-LINK port)
2. **Open** a serial terminal at 115200 baud
3. **Type** `help` to see available commands
4. **Enable** the motor: `odrive_enable`
5. **Start** valve control: `valve_start`
6. **Explore** different presets: `valve_preset smooth`

See the [Getting Started Guide](getting-started/getting-started.md) for detailed instructions.

### Quick Start - Web Interface

1. **Connect** STEVE to your network via Ethernet
2. **Find device IP** using CLI command: `ip_info` or `ethstatus`
3. **Open browser** and navigate to `http://<device-ip>:8080`
4. **Click Start** to begin valve simulation
5. **Load presets** from dropdown and click Apply
6. **Monitor** real-time position, velocity, torque on sparkline graphs

See the [Web Interface Guide](html/web-interface-guide.md) for detailed usage.

### Quick Start - REST API

1. **Find device IP** using CLI command: `ethstatus`
2. **Start HTTP server** using CLI: `http start`
3. **Test connection:**
   ```bash
   curl -H "X-API-Key: steve-valve-2025" \
        http://192.168.1.100:8080/api/v1/status
   ```
4. **Use Python client:**
   ```python
   from steve_client import SteveClient
   steve = SteveClient("192.168.1.100")
   steve.odrive_enable()
   steve.start(preset="smooth")
   status = steve.get_status()
   ```

See the [REST API Examples](rest/rest-api-examples.md) for complete code examples.

## Technical Specifications

### Processing
- **Microcontroller:** STM32H753ZI (ARM Cortex-M7)
- **Clock Speed:** 480 MHz
- **Flash Memory:** 2 MB
- **RAM:** 1 MB (512 KB DTCM, 512 KB SRAM)
- **FPU:** Double-precision (DP-FPU)

### Control Performance
- **Control Loop Rate:** 1000+ Hz
- **Position Resolution:** 8192 counts per revolution (13-bit encoder)
- **Velocity Estimation:** First-order discrete derivative with filtering
- **Torque Resolution:** 16-bit (ODrive-dependent)
- **Latency:** < 1 ms end-to-end (CAN + computation + CAN)

### Communication
- **UART:** 115200 baud (CLI over ST-LINK USB)
- **CAN:** Up to 1 Mbps (ODrive communication)
- **Ethernet:** 10/100 Mbps (LAN8742A PHY)
- **HTTP:** Port 8080 (web interface)
- **Streaming:** Port 8888 (TCP data stream)

### Physical Characteristics
- **Operating Voltage:** 5V USB or external 7-12V
- **Motor Power:** Via ODrive (up to 24V typical)
- **Dimensions:** Nucleo-144 form factor (144 × 89 mm)
- **Mounting:** Four M3 mounting holes

## Configuration and Customization

### Haptic Parameters

The valve feel is controlled by several key parameters:

**Viscous Damping** (`valve_damping`)
- Velocity-dependent resistance
- Range: 0.01 - 0.5 N·m·s/rad
- Lower = smoother, higher = more viscous

**Coulomb Friction** (`valve_friction`)
- Constant resistance to motion
- Range: 0.005 - 0.05 N·m
- Simulates bearing and seal friction

**Wall Stiffness** (`valve_wall_k`)
- End-stop spring constant
- Range: 0.5 - 5.0 N·m/turn
- Higher = harder boundaries

**Wall Damping** (`valve_wall_c`)
- End-stop energy dissipation
- Range: 0.05 - 0.5 N·m·s/turn
- Critical for stability at limits

**Torque Limit** (`valve_torquelimit`)
- Maximum output torque
- Range: 0.1 - 2.0 N·m (hardware dependent)
- Safety parameter

### Presets

STEVE includes several built-in presets:

- **default** - Balanced configuration for general use
- **smooth** - Low friction and damping for easy rotation
- **stiff** - High stiffness for precise positioning
- **heavy** - High damping for sluggish, viscous feel

Create custom presets with `valve_preset_save` to save your configurations.

### Network Configuration

Set a static IP address for network features:

```
setip 192.168.1.100 255.255.255.0 192.168.1.1
```

Configuration is saved to non-volatile memory and persists across reboots.

## Safety Considerations

### Torque Limits
Always set appropriate torque limits before operation:
```
valve_torquelimit 0.5
```
Start with lower values and increase as needed.

### Emergency Stop
Use `odrive_estop` for immediate motor shutdown in emergencies.

### Mechanical Limits
Configure virtual walls to prevent damage:
```
valve_wall_k 2.0
valve_wall_c 0.2
```

### Thermal Protection
Monitor motor and controller temperatures:
```
can_telemetry
```
FET temperature should stay below 80°C, motor temperature per specs.

### Power Supply
Ensure adequate power supply for motor operation:
- ODrive requires separate power (typically 12-24V)
- Current capacity should exceed motor peak draw
- Use proper gauge wiring and connectors

## Development and Contribution

### Firmware Source

The firmware source code is organized as follows:

```
firmware/
├── src/
│   ├── app/          # Application interfaces (CLI, perfmon)
│   ├── valve/        # Valve control subsystem
│   ├── network/      # Network services (HTTP, streaming)
│   ├── protocols/    # Communication protocols (CAN)
│   └── drivers/      # Hardware drivers (UART, FDCAN)
├── inc/              # Header files
├── third_party/      # External libraries
│   ├── lwip/        # TCP/IP stack
│   └── stm32cube/   # STM32 HAL
└── Makefile          # Build system
```

### Build System

The firmware uses ARM GCC toolchain:
- MISRA-C:2012 compliance checking
- Optimized for performance (-O2)
- Comprehensive warnings enabled
- ~142 KB flash usage

### Coding Standards

- **MISRA-C:2012** for safety-critical guidelines
- **OpenBSD style** for formatting and structure
- No typedef for structs (explicit `struct` keyword)
- Clear separation of concerns
- Comprehensive documentation

## Support and Resources

### Documentation
- [Getting Started Guide](getting-started.md) - Initial setup
- [CLI Reference](cli-reference.md) - Command documentation
- Firmware README files in source directories

### Hardware
- [STM32 Nucleo-H753ZI](https://www.st.com/en/evaluation-tools/nucleo-h753zi.html)
- [ODrive Motor Controller](https://odriverobotics.com/)

### Software Tools
- [STM32CubeIDE](https://www.st.com/en/development-tools/stm32cubeide.html) - Development environment
- [STM32CubeProg](https://www.st.com/en/development-tools/stm32cubeprog.html) - Programming tool
- [GNU ARM Embedded Toolchain](https://developer.arm.com/tools-and-software/open-source-software/developer-tools/gnu-toolchain/gnu-rm)

### Common Issues

See the [Getting Started Guide - Troubleshooting](getting-started.md#troubleshooting) section for solutions to common problems.

## Version History

Current firmware version includes:
- Real-time haptic control with passivity guarantees
- Complete CLI with 40+ commands
- Ethernet networking with HTTP and streaming
- Preset management system
- Performance monitoring and diagnostics
- Non-volatile configuration storage

## License and Credits

STEVE firmware utilizes several open-source components:
- **lwIP** - Lightweight TCP/IP stack (BSD license)
- **STM32Cube HAL** - STMicroelectronics hardware abstraction layer
- **CMSIS-DSP** - ARM optimized math library

## Acknowledgments

This project represents the culmination of research in haptic interfaces, control systems, and embedded software engineering. The modular architecture and comprehensive feature set make STEVE a versatile platform for haptic exploration and valve simulation applications.

## Next Steps and Future Development

We're excited about the future of STEVE and welcome your participation in shaping its development! See our **[Next Steps](next-steps.md)** document to learn about:

- **Testing opportunities** - Help us find bugs and improve quality
- **Feedback channels** - Share your ideas for improvements
- **Future enhancements** - Advanced motor control, touchscreen interfaces, data logging
- **How to contribute** - Join the community and collaborate

Your insights and engagement make STEVE better for everyone. **[Read the full Next Steps document →](next-steps.md)**

---

**Ready to get started?** Head to the [Getting Started Guide](getting-started.md) to connect your STEVE device and run your first commands!

For detailed command information, see the [CLI Reference Guide](cli-reference.md).
