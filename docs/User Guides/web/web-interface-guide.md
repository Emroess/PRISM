[⬅ Back to Main README](../../../README.md#user-guides)

## Table of Contents

- [Overview](#overview)
- [Key Features](#key-features)
- [Accessing the Web Interface](#accessing-the-web-interface)
    - [Requirements](#requirements)
    - [Connection Steps](#connection-steps)
    - [Default Network Configuration](#default-network-configuration)
- [Interface Layout](#interface-layout)
    - [1. Real-Time Metrics Display](#1-real-time-metrics-display)
    - [2. System Controls](#2-system-controls)
    - [3. Configuration Editor](#3-configuration-editor)
    - [4. Preset Management](#4-preset-management)
- [Common Workflows](#common-workflows)
    - [Quick Start - Test Valve with Default Settings](#quick-start-test-valve-with-default-settings)
    - [Load and Test a Preset](#load-and-test-a-preset)
    - [Tune Parameters in Real-Time](#tune-parameters-in-real-time)
    - [Save Custom Preset](#save-custom-preset)
    - [Edit Existing Preset](#edit-existing-preset)
- [Technical Implementation](#technical-implementation)
    - [Architecture](#architecture)
    - [Data Flow](#data-flow)
    - [Update Mechanism](#update-mechanism)
    - [Sparkline Rendering](#sparkline-rendering)
    - [API Key Handling](#api-key-handling)
- [Browser Compatibility](#browser-compatibility)
    - [Mobile Access](#mobile-access)
- [Troubleshooting](#troubleshooting)
    - [Cannot Access Web Interface](#cannot-access-web-interface)
    - [Page Loads But No Data](#page-loads-but-no-data)
    - [Sparklines Not Drawing](#sparklines-not-drawing)
    - [Configuration Changes Not Applied](#configuration-changes-not-applied)
    - [Presets Don't Save](#presets-dont-save)
    - [Page Becomes Unresponsive](#page-becomes-unresponsive)
- [Performance Considerations](#performance-considerations)
    - [Network Bandwidth](#network-bandwidth)
    - [CPU Load](#cpu-load)
    - [Browser Performance](#browser-performance)
- [Advanced Usage](#advanced-usage)
    - [Custom Dashboards](#custom-dashboards)
    - [Integration with Other Tools](#integration-with-other-tools)
    - [Bookmarking](#bookmarking)
- [Security Considerations](#security-considerations)
    - [Network Security](#network-security)
    - [API Key Exposure](#api-key-exposure)
- [Comparison with Other Interfaces](#comparison-with-other-interfaces)
    - [Web Interface vs CLI](#web-interface-vs-cli)
    - [Web Interface vs REST API](#web-interface-vs-rest-api)
- [See Also](#see-also)
- [Support](#support)

﻿# PRISM Web Interface Guide

## Overview

PRISM includes an embedded HTML control panel accessible via any web browser on your network. The web interface provides real-time monitoring and control of valve simulations without requiring additional software installation or API integration. This makes it ideal for quick configuration, testing, and demonstration of haptic valve behaviors.

## Key Features

- **Real-time monitoring** - Live position, velocity, and torque visualization with sparkline graphs
- **Valve control** - Start/stop valve simulation with single-click buttons
- **Preset management** - Load, edit, and save haptic valve presets
- **Configuration tuning** - Adjust viscous damping, coulomb friction, and wall parameters in real-time
- **Zero installation** - Works in any modern web browser (Chrome, Firefox, Safari, Edge)
- **Embedded server** - Runs directly on STM32 firmware, no external dependencies
- **Automatic updates** - Display refreshes at 10 Hz for responsive feedback

## Accessing the Web Interface

### Requirements

- PRISM device connected to your network via Ethernet
- Web browser on computer connected to same network
- Device IP address (obtain via CLI command `ip_info` or `ethstatus`)

### Connection Steps

1. **Power on PRISM** and connect Ethernet cable
2. **Obtain IP address** using CLI or check your DHCP server/router
3. **Open web browser** and navigate to:

   ```
   http://<device-ip>:8080
   ```

   Example: `http://192.168.1.100:8080`
4. **Page loads automatically** with live data streaming

### Default Network Configuration

- **Port:** 8080 (HTTP)
- **Protocol:** HTTP (no HTTPS)
- **Authentication:** Embedded (API key baked into JavaScript)
- **Refresh Rate:** 10 Hz (100 ms intervals)

## Interface Layout

The web interface is organized into distinct sections:

### 1. Real-Time Metrics Display

Three live metrics with sparkline visualizations:

- **Position (deg)** - Current encoder position in degrees
- **Velocity (rad/s)** - Angular velocity in radians per second
- **Torque (Nm)** - Commanded torque in Newton-meters

Each metric includes:

- Large numeric display with 2 decimal precision
- Sparkline graph showing last 60 samples (6 seconds at 10 Hz)
- Min/max value indicators on sparkline
- Auto-scaling graph axes

### 2. System Controls

**Start/Stop Buttons:**

- **Start** - Begins valve simulation with current configuration
- **Stop** - Immediately halts valve simulation (red button)

**Load Preset:**

- Dropdown menu showing all 4 available presets with names
- **Apply** button loads selected preset and starts valve

### 3. Configuration Editor

Four primary haptic parameters with live adjustment:

- **Viscous (Nm·s/rad)** - Viscous damping coefficient
- **Coulomb (Nm)** - Coulomb friction torque
- **Wall Stiff (Nm/turn)** - Virtual wall stiffness
- **Wall Damp (Nm·s/turn)** - Virtual wall damping

**Update Configuration** button applies changes to running valve.

### 4. Preset Management

Complete preset editing interface:

**Select Preset to Edit** - Dropdown to choose preset 0-3

**Preset Parameters:**

- **Name** - Preset identifier (max 15 characters)
- **Viscous (Nm·s/rad)** - Viscous damping
- **Coulomb (Nm)** - Coulomb friction
- **Wall Stiff (Nm/turn)** - Wall stiffness
- **Wall Damp (Nm·s/turn)** - Wall damping
- **Travel (deg)** - Total angular travel (1-360°)
- **Torque Limit (Nm)** - Maximum torque (0-30 Nm)
- **Smoothing (ε)** - Detent smoothing epsilon

**Preset Actions:**

- **Save Preset Changes** - Writes modified preset parameters to flash
- **Save Current Config to Preset** - Copies active configuration to selected preset slot

## Common Workflows

### Quick Start - Test Valve with Default Settings

1. Navigate to `http://<device-ip>:8080`
2. Click **Start** button
3. Observe real-time position, velocity, torque on graphs
4. Click **Stop** when finished

### Load and Test a Preset

1. Open web interface
2. Select preset from **Load Preset** dropdown (e.g., "Preset 1 (smooth)")
3. Click **Apply** button
4. Valve starts with preset configuration
5. Monitor behavior on sparkline graphs
6. Click **Stop** when complete

### Tune Parameters in Real-Time

1. Start valve with current configuration
2. Adjust parameters in **Configuration Editor**:

   - Increase **Viscous** to add damping
   - Adjust **Coulomb** to change friction feel
   - Modify **Wall Stiff** to change detent strength
3. Click **Update Configuration**
4. Changes apply immediately to running valve
5. Observe effect on torque and velocity graphs

### Save Custom Preset

1. Configure valve parameters in **Configuration Editor**
2. Click **Update Configuration** to apply
3. Test valve behavior
4. In **Manage Presets** section, select preset slot (0-3)
5. Enter descriptive **Name** for preset
6. Click **Save Current Config to Preset**
7. Preset now saved in flash memory

### Edit Existing Preset

1. In **Manage Presets**, select preset from dropdown
2. Preset parameters load into edit fields
3. Modify desired parameters (name, viscous, coulomb, etc.)
4. Click **Save Preset Changes**
5. Updated preset writes to flash
6. Reload page to see updated preset name in **Load Preset** dropdown

## Technical Implementation

### Architecture

The web interface is a single-page application (SPA) embedded directly in the STM32 firmware:

- **HTML/CSS/JavaScript** - Minified and stored as C string constant
- **Size** - Approximately 10 KB (minified)
- **Storage** - Flash memory, no filesystem required
- **Server** - lwIP TCP stack with custom HTTP server
- **API Calls** - Uses REST API internally with embedded API key

### Data Flow

```
Browser → HTTP Request → lwIP TCP → HTTP Server → REST API → Valve Manager
                                                              ↓
Browser ← HTTP Response ← lwIP TCP ← JSON Response ←─────────┘
```

### Update Mechanism

JavaScript polls REST API endpoints at 10 Hz:

```javascript
function update() {
  fetch('/api/v1/status', {headers: {'X-API-Key': '...'}})
    .then(r => r.json())
    .then(j => {
      // Update position, velocity, torque displays
      // Append to sparkline data arrays
      // Redraw canvas graphs
    });
}
setInterval(update, 100);  // 100 ms = 10 Hz
```

### Sparkline Rendering

Each metric includes a `<canvas>` element with custom rendering:

- **60 samples** buffered (6 seconds)
- **Auto-scaling** y-axis based on min/max
- **Smooth line** interpolation
- **Min/max labels** in gray
- **2x resolution** for Retina displays

### API Key Handling

The firmware embeds the API key directly into the JavaScript code at compile time:

```c
#define HTTP_API_KEY "steve-valve-2025"

// In HTML string:
"fetch('/api/v1/status',{headers:{'X-API-Key':'" HTTP_API_KEY "'}})"
```

This eliminates the need for:

- Login forms
- Session management
- Cookie handling
- Authentication dialogs

**Note:** This means the API key is visible in browser source code. The web interface should only be used on trusted networks.

## Browser Compatibility

The web interface works on all modern browsers:

- **Chrome/Edge** - Tested, fully supported
- **Firefox** - Tested, fully supported
- **Safari** - Tested, fully supported
- **Mobile browsers** - Responsive design supports phones/tablets

### Mobile Access

The interface is mobile-friendly:

- Viewport meta tag for proper scaling
- Touch-friendly button sizing
- Responsive grid layout
- No hover-dependent features

Access from mobile device:

1. Connect phone/tablet to same WiFi network as PRISM
2. Open browser and navigate to `http://<device-ip>:8080`
3. Interface adapts to screen size

## Troubleshooting

### Cannot Access Web Interface

**Problem:** Browser shows "Connection refused" or timeout.

**Solutions:**

1. Verify PRISM Ethernet connection (check link LED)
2. Confirm IP address with `ip_info` CLI command
3. Check same network/subnet (ping device IP)
4. Verify port 8080 not blocked by firewall
5. Try different browser or clear browser cache

### Page Loads But No Data

**Problem:** Interface displays but metrics show "--".

**Solutions:**

1. Check valve is running: click **Start** button
2. Verify ODrive enabled and calibrated
3. Check browser console (F12) for JavaScript errors
4. Refresh page (Ctrl+R or Cmd+R)
5. Verify REST API working: `curl http://<ip>:8080/api/v1/status`

### Sparklines Not Drawing

**Problem:** Graphs are blank or not updating.

**Solutions:**

1. Check browser supports HTML5 Canvas (should be all modern browsers)
2. Look for JavaScript errors in browser console (F12)
3. Verify data is updating (numeric values should change)
4. Try different browser
5. Refresh page to reset state

### Configuration Changes Not Applied

**Problem:** Click "Update Configuration" but behavior doesn't change.

**Solutions:**

1. Verify valve is running (click **Start** if stopped)
2. Check that all parameter values are valid numbers
3. Look for error responses in browser console
4. Verify REST API working with CLI or curl
5. Try stopping and restarting valve

### Presets Don't Save

**Problem:** Click "Save Preset Changes" but presets revert.

**Solutions:**

1. Check flash write protection not enabled
2. Verify all preset parameters within valid ranges
3. Check browser console for 4xx/5xx HTTP errors
4. Ensure preset name is 15 characters or less
5. Try using REST API directly to verify: `POST /api/v1/presets`

### Page Becomes Unresponsive

**Problem:** Interface freezes or stops updating.

**Solutions:**

1. Refresh browser page (Ctrl+R)
2. Check network connection stable
3. Verify PRISM device not crashed (check LED, ping IP)
4. Close and reopen browser
5. Clear browser cache and cookies

## Performance Considerations

### Network Bandwidth

- **10 Hz updates** - Approximately 2-3 KB/s per browser
- **Minimal overhead** - Small JSON responses (~200 bytes each)
- **Multiple clients** - Server supports 8 concurrent connections

### CPU Load

- **Negligible impact** - HTML served from flash, minimal processing
- **JSON generation** - Pre-formatted strings, fast serialization
- **HTTP overhead** - ~0.5% CPU with 5 concurrent browsers

### Browser Performance

- **Canvas rendering** - Hardware accelerated on most browsers
- **Memory usage** - ~10-20 MB per tab
- **60 sample buffer** - Minimal memory overhead

## Advanced Usage

### Custom Dashboards

Since the web interface uses standard REST API calls, you can:

1. View page source (Ctrl+U) to see API endpoints
2. Build custom dashboards using same endpoints
3. Use API key from firmware source (`HTTP_API_KEY` in Makefile)
4. Create specialized monitoring tools

### Integration with Other Tools

The web interface can run alongside:

- **CLI control** - Use CLI and web interface simultaneously
- **REST API clients** - Python scripts can run while browser open
- **TCP streaming** - Data streaming works independently
- **Multiple browsers** - View from several computers at once

### Bookmarking

Create browser bookmark for quick access:

1. Navigate to PRISM web interface
2. Bookmark page (Ctrl+D or Cmd+D)
3. Rename to "PRISM Valve Controller"
4. Access instantly from bookmarks bar

## Security Considerations

### Network Security

The web interface has minimal security:

- **No authentication dialog** - API key embedded in JavaScript
- **No HTTPS** - Plain HTTP traffic
- **No user management** - Single shared access

**Recommendations:**

1. Use only on isolated/trusted networks
2. Implement firewall rules to restrict access to port 8080
3. Use VPN for remote access
4. Monitor connected clients if needed

### API Key Exposure

The API key (`steve-valve-2025`) is visible in:

- Browser page source
- Network traffic (HTTP not encrypted)
- JavaScript console

This is acceptable for:

- Lab environments
- Trusted networks
- Development/testing

Not suitable for:

- Public networks
- Untrusted environments
- Production deployments requiring security

## Comparison with Other Interfaces

### Web Interface vs CLI

| Feature           | Web Interface       | CLI              |
| ----------------- | ------------------- | ---------------- |
| Installation      | None (just browser) | Terminal program |
| Real-time graphs  | Yes (sparklines)    | No               |
| Preset management | Full GUI            | Command-based    |
| Learning curve    | Immediate           | Moderate         |
| Remote access     | Easy (any device)   | Serial only      |
| Scripting         | Not designed for it | Excellent        |

**Use Web Interface for:**

- Quick testing and demos
- Visual monitoring
- Non-technical users
- Remote operation

**Use CLI for:**

- Automation scripts
- Debugging
- Advanced configuration
- Console-based workflows

### Web Interface vs REST API

| Feature       | Web Interface | REST API         |
| ------------- | ------------- | ---------------- |
| Programming   | Not required  | Required         |
| Customization | Limited       | Unlimited        |
| Data logging  | Manual        | Automated        |
| Integration   | Standalone    | Full integration |
| Visualization | Built-in      | Custom           |

**Use Web Interface for:**

- Human interaction
- Manual testing
- Quick configuration
- Demonstrations

**Use REST API for:**

- Automation
- Data logging
- Custom applications
- ROS integration

## See Also

- [REST API Reference](../rest/rest-api.md) - Backend API used by web interface
- [REST API Examples](../rest/rest-api-examples.md) - Build custom clients
- [CLI Reference](../cli/cli-reference.md) - Command-line interface
- [Getting Started Guide](../getting-started/getting-started.md) - Initial setup
- [Streaming Guide](../stream/streaming-guide.md) - High-speed data logging

## Support

For web interface issues:

1. Check browser console (F12) for JavaScript errors
2. Verify REST API endpoints work directly (curl or Python)
3. Test with different browser
4. Review firmware HTTP server logs via CLI
5. Contact PRISM firmware team with specific error messages