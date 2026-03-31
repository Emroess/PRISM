# PRISM REST API Examples

This guide provides practical code examples for integrating with the PRISM REST API in various programming languages. All examples include proper authentication and error handling.

## Table of Contents

- [Quick Start](#quick-start)
- [Python Examples](#python-examples)
- [JavaScript Examples](#javascript-examples)
- [curl Examples](#curl-examples)
- [C++ Examples](#c-examples)
- [ROS Integration](#ros-integration)
- [Complete Workflows](#complete-workflows)

---

## Quick Start

### Prerequisites

- PRISM device on network (e.g., `192.168.1.100`)
- HTTP server started (CLI command: `http start`)
- API key: `steve-valve-2025`

### Test Connection

```bash
curl -H "X-API-Key: steve-valve-2025" \
     http://192.168.1.100:8080/api/v1/status
```

If successful, you'll see JSON with current valve status.

---

## Python Examples

### Installation

```bash
pip install requests
```

### Basic Connection

```python
import requests
import json

# Configuration
BASE_URL = "http://192.168.1.100:8080/api/v1"
API_KEY = "steve-valve-2025"
HEADERS = {"X-API-Key": API_KEY}

def get_status():
    """Get current valve status."""
    response = requests.get(f"{BASE_URL}/status", headers=HEADERS)
    response.raise_for_status()
    return response.json()

# Get status
try:
    status = get_status()
    print(f"Position: {status['pos_deg']:.2f} degrees")
    print(f"Velocity: {status['vel_rad_s']:.3f} rad/s")
    print(f"Torque: {status['torque_nm']:.3f} Nm")
except requests.exceptions.RequestException as e:
    print(f"Error: {e}")
```

### Complete Python Client Class

```python
import requests
import time
from typing import Dict, Optional, Any

class SteveClient:
    """Client for PRISM REST API."""
    
    def __init__(self, host: str, port: int = 8080, api_key: str = "steve-valve-2025"):
        """
        Initialize PRISM client.
        
        Args:
            host: IP address or hostname (e.g., "192.168.1.100")
            port: HTTP port (default: 8080)
            api_key: API authentication key
        """
        self.base_url = f"http://{host}:{port}/api/v1"
        self.headers = {"X-API-Key": api_key}
        
    def _request(self, method: str, endpoint: str, data: Optional[Dict] = None) -> Dict:
        """Make authenticated API request."""
        url = f"{self.base_url}/{endpoint}"
        headers = self.headers.copy()
        
        if data is not None:
            headers["Content-Type"] = "application/json"
            
        response = requests.request(method, url, headers=headers, json=data)
        response.raise_for_status()
        return response.json()
    
    def get_status(self) -> Dict:
        """Get real-time valve status."""
        return self._request("GET", "status")
    
    def get_config(self) -> Dict:
        """Get current configuration."""
        return self._request("GET", "config")
    
    def set_config(self, **params) -> Dict:
        """
        Update configuration parameters.
        
        Args:
            viscous: Viscous damping (N·m·s/rad)
            coulomb: Coulomb friction (N·m)
            wall_stiffness: Wall stiffness (N·m/turn)
            wall_damping: Wall damping (N·m·s/turn)
            torque_limit: Maximum torque (N·m)
        """
        return self._request("POST", "config", params)
    
    def start(self, preset: Optional[str] = None) -> Dict:
        """
        Start valve control.
        
        Args:
            preset: Optional preset name to load
        """
        data = {"action": "start"}
        if preset:
            data["preset"] = preset
        return self._request("POST", "control", data)
    
    def stop(self) -> Dict:
        """Stop valve control."""
        return self._request("POST", "control", {"action": "stop"})
    
    def load_preset(self, preset_index: int) -> Dict:
        """Load a preset configuration by index (0-3)."""
        return self._request("POST", "control", {"preset": preset_index})
    
    def get_presets(self) -> list:
        """Get all available presets."""
        return self._request("GET", "presets")
    
    def save_preset(self, index: int, **params) -> Dict:
        """
        Save preset configuration.
        
        Args:
            index: Preset slot (0-3)
            name: Preset name
            viscous, coulomb, wall_stiffness, wall_damping, etc.
        """
        data = {"index": index, **params}
        return self._request("POST", "presets", data)
    
    def save_current_as_preset(self, index: int) -> Dict:
        """Save current configuration to preset slot."""
        return self._request("POST", "presets", 
                            {"index": index, "save_current": True})
    
    def odrive_enable(self) -> Dict:
        """Enable ODrive motor controller."""
        return self._request("POST", "odrive", {"action": "enable"})
    
    def odrive_disable(self) -> Dict:
        """Disable ODrive motor controller."""
        return self._request("POST", "odrive", {"action": "disable"})
    
    def odrive_status(self) -> Dict:
        """Get ODrive status."""
        return self._request("GET", "odrive")
    
    def odrive_set_torque(self, torque: float) -> Dict:
        """Set ODrive torque command."""
        return self._request("POST", "odrive", 
                            {"action": "set_torque", "value": torque})
    
    def get_performance(self) -> Dict:
        """Get performance statistics."""
        return self._request("GET", "performance")
    
    def start_streaming(self, interval_ms: int = 100) -> Dict:
        """Start data streaming server."""
        return self._request("POST", "stream", 
                            {"action": "start", "interval_ms": interval_ms})
    
    def stop_streaming(self) -> Dict:
        """Stop data streaming server."""
        return self._request("POST", "stream", {"action": "stop"})
    
    def wait_for_mode(self, target_mode: str, timeout: float = 5.0) -> bool:
        """
        Wait for valve to enter target mode.
        
        Args:
            target_mode: "running" or "idle"
            timeout: Maximum wait time in seconds
            
        Returns:
            True if mode reached, False if timeout
        """
        start_time = time.time()
        while time.time() - start_time < timeout:
            status = self.get_status()
            if status.get("mode") == target_mode:
                return True
            time.sleep(0.1)
        return False


# Example usage
if __name__ == "__main__":
    # Connect to PRISM
    steve = SteveClient("192.168.1.100")
    
    # Enable ODrive
    print("Enabling ODrive...")
    steve.odrive_enable()
    time.sleep(0.5)
    
    # Start with smooth preset
    print("Starting valve control with 'smooth' preset...")
    steve.start(preset="smooth")
    
    # Monitor for 10 seconds
    print("\nMonitoring valve status:")
    for i in range(10):
        status = steve.get_status()
        print(f"  Pos: {status['pos_deg']:7.2f}° | "
              f"Vel: {status['vel_rad_s']:7.3f} rad/s | "
              f"Torque: {status['torque_nm']:7.3f} Nm")
        time.sleep(1)
    
    # Stop
    print("\nStopping valve...")
    steve.stop()
    steve.odrive_disable()
    print("Done!")
```

### Data Collection Example

```python
import csv
import time
from datetime import datetime

def collect_data(steve: SteveClient, duration_sec: float, sample_rate_hz: float):
    """
    Collect valve data to CSV file.
    
    Args:
        steve: SteveClient instance
        duration_sec: Collection duration in seconds
        sample_rate_hz: Sampling rate in Hz
    """
    filename = f"steve_data_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
    interval = 1.0 / sample_rate_hz
    samples = int(duration_sec * sample_rate_hz)
    
    with open(filename, 'w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['timestamp', 'pos_deg', 'vel_rad_s', 'torque_nm', 'energy_j'])
        
        start_time = time.time()
        for i in range(samples):
            target_time = start_time + i * interval
            
            # Get status
            status = steve.get_status()
            timestamp = time.time() - start_time
            
            # Write row
            writer.writerow([
                f"{timestamp:.3f}",
                f"{status['pos_deg']:.6f}",
                f"{status['vel_rad_s']:.6f}",
                f"{status['torque_nm']:.6f}",
                f"{status['energy_j']:.9f}"
            ])
            
            # Sleep until next sample
            sleep_time = target_time - time.time()
            if sleep_time > 0:
                time.sleep(sleep_time)
    
    print(f"Data saved to {filename}")

# Example: Collect 30 seconds at 10 Hz
steve = SteveClient("192.168.1.100")
steve.odrive_enable()
steve.start()
collect_data(steve, duration_sec=30, sample_rate_hz=10)
steve.stop()
steve.odrive_disable()
```

### Parameter Sweep Example

```python
import numpy as np

def sweep_damping(steve: SteveClient, 
                  damping_values: list, 
                  test_duration: float = 5.0):
    """
    Sweep through different damping values and collect RMS metrics.
    
    Args:
        steve: SteveClient instance
        damping_values: List of damping values to test
        test_duration: Test duration per value (seconds)
    """
    results = []
    
    for damping in damping_values:
        print(f"\nTesting damping = {damping:.4f}")
        
        # Set damping
        steve.set_config(viscous=damping)
        time.sleep(0.5)  # Let system settle
        
        # Collect data
        samples = []
        for _ in range(int(test_duration * 10)):  # 10 Hz sampling
            status = steve.get_status()
            samples.append({
                'pos': status['pos_deg'],
                'vel': status['vel_rad_s'],
                'torque': status['torque_nm']
            })
            time.sleep(0.1)
        
        # Compute RMS
        pos_rms = np.sqrt(np.mean([s['pos']**2 for s in samples]))
        vel_rms = np.sqrt(np.mean([s['vel']**2 for s in samples]))
        torque_rms = np.sqrt(np.mean([s['torque']**2 for s in samples]))
        
        results.append({
            'damping': damping,
            'pos_rms': pos_rms,
            'vel_rms': vel_rms,
            'torque_rms': torque_rms
        })
        
        print(f"  Position RMS: {pos_rms:.2f}°")
        print(f"  Velocity RMS: {vel_rms:.3f} rad/s")
        print(f"  Torque RMS: {torque_rms:.3f} Nm")
    
    return results

# Example: Test different damping values
steve = SteveClient("192.168.1.100")
steve.odrive_enable()
steve.start()

damping_values = [0.02, 0.04, 0.06, 0.08, 0.10]
results = sweep_damping(steve, damping_values)

steve.stop()
steve.odrive_disable()

# Print summary
print("\n=== SWEEP RESULTS ===")
for r in results:
    print(f"Damping {r['damping']:.2f}: "
          f"Pos RMS={r['pos_rms']:.2f}°, "
          f"Vel RMS={r['vel_rms']:.3f} rad/s, "
          f"Torque RMS={r['torque_rms']:.3f} Nm")
```

---

## JavaScript Examples

### Node.js with axios

```javascript
const axios = require('axios');

class SteveClient {
    constructor(host, port = 8080, apiKey = 'steve-valve-2025') {
        this.baseURL = `http://${host}:${port}/api/v1`;
        this.headers = { 'X-API-Key': apiKey };
    }

    async request(method, endpoint, data = null) {
        const config = {
            method,
            url: `${this.baseURL}/${endpoint}`,
            headers: this.headers
        };
        
        if (data) {
            config.headers['Content-Type'] = 'application/json';
            config.data = data;
        }
        
        const response = await axios(config);
        return response.data;
    }

    async getStatus() {
        return this.request('GET', 'status');
    }

    async setConfig(params) {
        return this.request('POST', 'config', params);
    }

    async start(preset = null) {
        const data = { action: 'start' };
        if (preset) data.preset = preset;
        return this.request('POST', 'control', data);
    }

    async stop() {
        return this.request('POST', 'control', { action: 'stop' });
    }

    async odriveEnable() {
        return this.request('POST', 'odrive', { action: 'enable' });
    }

    async odriveDisable() {
        return this.request('POST', 'odrive', { action: 'disable' });
    }
}

// Example usage
async function main() {
    const steve = new SteveClient('192.168.1.100');
    
    try {
        // Enable and start
        await steve.odriveEnable();
        await steve.start('smooth');
        
        // Monitor for 10 seconds
        for (let i = 0; i < 10; i++) {
            const status = await steve.getStatus();
            console.log(`Pos: ${status.pos_deg.toFixed(2)}° | ` +
                       `Vel: ${status.vel_rad_s.toFixed(3)} rad/s | ` +
                       `Torque: ${status.torque_nm.toFixed(3)} Nm`);
            await new Promise(resolve => setTimeout(resolve, 1000));
        }
        
        // Stop
        await steve.stop();
        await steve.odriveDisable();
    } catch (error) {
        console.error('Error:', error.message);
    }
}

main();
```

### Browser JavaScript (fetch API)

```html
<!DOCTYPE html>
<html>
<head>
    <title>PRISM Control</title>
</head>
<body>
    <h1>PRISM Valve Control</h1>
    <div id="status">
        <p>Position: <span id="pos">--</span>°</p>
        <p>Velocity: <span id="vel">--</span> rad/s</p>
        <p>Torque: <span id="tor">--</span> Nm</p>
    </div>
    <button onclick="startValve()">Start</button>
    <button onclick="stopValve()">Stop</button>

    <script>
        const BASE_URL = 'http://192.168.1.100:8080/api/v1';
        const API_KEY = 'steve-valve-2025';
        const HEADERS = { 'X-API-Key': API_KEY };

        async function apiRequest(endpoint, method = 'GET', data = null) {
            const options = {
                method,
                headers: HEADERS
            };
            
            if (data) {
                options.headers['Content-Type'] = 'application/json';
                options.body = JSON.stringify(data);
            }
            
            const response = await fetch(`${BASE_URL}/${endpoint}`, options);
            if (!response.ok) {
                throw new Error(`HTTP ${response.status}`);
            }
            return response.json();
        }

        async function updateStatus() {
            try {
                const status = await apiRequest('status');
                document.getElementById('pos').textContent = status.pos_deg.toFixed(2);
                document.getElementById('vel').textContent = status.vel_rad_s.toFixed(3);
                document.getElementById('tor').textContent = status.torque_nm.toFixed(3);
            } catch (error) {
                console.error('Status update failed:', error);
            }
        }

        async function startValve() {
            try {
                await apiRequest('odrive', 'POST', { action: 'enable' });
                await apiRequest('control', 'POST', { action: 'start', preset: 'smooth' });
                alert('Valve started');
            } catch (error) {
                alert('Failed to start: ' + error.message);
            }
        }

        async function stopValve() {
            try {
                await apiRequest('control', 'POST', { action: 'stop' });
                await apiRequest('odrive', 'POST', { action: 'disable' });
                alert('Valve stopped');
            } catch (error) {
                alert('Failed to stop: ' + error.message);
            }
        }

        // Update status every 100ms
        setInterval(updateStatus, 100);
    </script>
</body>
</html>
```

---

## curl Examples

### Get Status

```bash
curl -H "X-API-Key: steve-valve-2025" \
     http://192.168.1.100:8080/api/v1/status
```

### Get Configuration

```bash
curl -H "X-API-Key: steve-valve-2025" \
     http://192.168.1.100:8080/api/v1/config
```

### Update Configuration

```bash
curl -X POST \
     -H "X-API-Key: steve-valve-2025" \
     -H "Content-Type: application/json" \
     -d '{"viscous": 0.06, "coulomb": 0.012}' \
     http://192.168.1.100:8080/api/v1/config
```

### Start Valve

```bash
curl -X POST \
     -H "X-API-Key: steve-valve-2025" \
     -H "Content-Type: application/json" \
     -d '{"action": "start"}' \
     http://192.168.1.100:8080/api/v1/control
```

### Start with Preset

```bash
curl -X POST \
     -H "X-API-Key: steve-valve-2025" \
     -H "Content-Type: application/json" \
     -d '{"action": "start", "preset": "smooth"}' \
     http://192.168.1.100:8080/api/v1/control
```

### Stop Valve

```bash
curl -X POST \
     -H "X-API-Key: steve-valve-2025" \
     -H "Content-Type: application/json" \
     -d '{"action": "stop"}' \
     http://192.168.1.100:8080/api/v1/control
```

### Enable ODrive

```bash
curl -X POST \
     -H "X-API-Key: steve-valve-2025" \
     -H "Content-Type: application/json" \
     -d '{"action": "enable"}' \
     http://192.168.1.100:8080/api/v1/odrive
```

### Get Performance Metrics

```bash
curl -H "X-API-Key: steve-valve-2025" \
     http://192.168.1.100:8080/api/v1/performance
```

### Save Current Config as Preset

```bash
curl -X POST \
     -H "X-API-Key: steve-valve-2025" \
     -H "Content-Type: application/json" \
     -d '{"index": 0, "save_current": true}' \
     http://192.168.1.100:8080/api/v1/presets
```

### Bash Script for Monitoring

```bash
#!/bin/bash
# monitor_steve.sh - Monitor PRISM status

API_KEY="steve-valve-2025"
HOST="192.168.1.100"
BASE_URL="http://${HOST}:8080/api/v1"

echo "Monitoring PRISM valve..."
echo "Press Ctrl+C to stop"
echo ""

while true; do
    status=$(curl -s -H "X-API-Key: ${API_KEY}" "${BASE_URL}/status")
    
    pos=$(echo "$status" | jq -r '.pos_deg')
    vel=$(echo "$status" | jq -r '.vel_rad_s')
    tor=$(echo "$status" | jq -r '.torque_nm')
    
    printf "\rPos: %7.2f° | Vel: %7.3f rad/s | Torque: %7.3f Nm" \
           "$pos" "$vel" "$tor"
    
    sleep 0.1
done
```

---

## C++ Examples

### Using libcurl and nlohmann/json

```cpp
#include <iostream>
#include <string>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

class SteveClient {
private:
    std::string base_url;
    std::string api_key;
    CURL* curl;
    
    static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
        ((std::string*)userp)->append((char*)contents, size * nmemb);
        return size * nmemb;
    }
    
    json request(const std::string& method, const std::string& endpoint, 
                 const json& data = json()) {
        std::string url = base_url + "/" + endpoint;
        std::string response_string;
        
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_string);
        
        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, ("X-API-Key: " + api_key).c_str());
        
        if (method == "POST") {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            headers = curl_slist_append(headers, "Content-Type: application/json");
            std::string json_str = data.dump();
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, json_str.c_str());
        } else {
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        }
        
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        
        CURLcode res = curl_easy_perform(curl);
        curl_slist_free_all(headers);
        
        if (res != CURLE_OK) {
            throw std::runtime_error(curl_easy_strerror(res));
        }
        
        return json::parse(response_string);
    }
    
public:
    SteveClient(const std::string& host, int port = 8080, 
                const std::string& key = "steve-valve-2025") 
        : api_key(key) {
        base_url = "http://" + host + ":" + std::to_string(port) + "/api/v1";
        curl = curl_easy_init();
        if (!curl) {
            throw std::runtime_error("Failed to initialize curl");
        }
    }
    
    ~SteveClient() {
        curl_easy_cleanup(curl);
    }
    
    json getStatus() {
        return request("GET", "status");
    }
    
    json setConfig(const json& params) {
        return request("POST", "config", params);
    }
    
    json start(const std::string& preset = "") {
        json data = {{"action", "start"}};
        if (!preset.empty()) {
            data["preset"] = preset;
        }
        return request("POST", "control", data);
    }
    
    json stop() {
        return request("POST", "control", {{"action", "stop"}});
    }
    
    json odriveEnable() {
        return request("POST", "odrive", {{"action", "enable"}});
    }
    
    json odriveDisable() {
        return request("POST", "odrive", {{"action", "disable"}});
    }
};

// Example usage
int main() {
    try {
        SteveClient steve("192.168.1.100");
        
        // Enable ODrive
        std::cout << "Enabling ODrive..." << std::endl;
        steve.odriveEnable();
        
        // Start with smooth preset
        std::cout << "Starting valve..." << std::endl;
        steve.start("smooth");
        
        // Monitor for 10 seconds
        for (int i = 0; i < 10; i++) {
            auto status = steve.getStatus();
            std::cout << "Pos: " << status["pos_deg"] << "° | "
                     << "Vel: " << status["vel_rad_s"] << " rad/s | "
                     << "Torque: " << status["torque_nm"] << " Nm"
                     << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        // Stop
        std::cout << "Stopping..." << std::endl;
        steve.stop();
        steve.odriveDisable();
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}
```

---

## ROS Integration

### ROS 2 Python Node

```python
#!/usr/bin/env python3
import rclpy
from rclpy.node import Node
from std_msgs.msg import Float64
from sensor_msgs.msg import JointState
import requests

class SteveValveNode(Node):
    def __init__(self):
        super().__init__('steve_valve_node')
        
        # Parameters
        self.declare_parameter('host', '192.168.1.100')
        self.declare_parameter('port', 8080)
        self.declare_parameter('api_key', 'steve-valve-2025')
        self.declare_parameter('update_rate', 10.0)  # Hz
        
        host = self.get_parameter('host').value
        port = self.get_parameter('port').value
        api_key = self.get_parameter('api_key').value
        
        self.base_url = f"http://{host}:{port}/api/v1"
        self.headers = {"X-API-Key": api_key}
        
        # Publishers
        self.joint_state_pub = self.create_publisher(
            JointState, 'joint_states', 10)
        
        # Subscribers
        self.torque_sub = self.create_subscription(
            Float64, 'valve_torque_cmd', self.torque_callback, 10)
        
        # Timer for status updates
        update_period = 1.0 / self.get_parameter('update_rate').value
        self.timer = self.create_timer(update_period, self.update_callback)
        
        self.get_logger().info('PRISM valve node started')
    
    def update_callback(self):
        """Periodic status update."""
        try:
            response = requests.get(f"{self.base_url}/status", 
                                   headers=self.headers, timeout=1.0)
            if response.status_code == 200:
                status = response.json()
                
                # Publish joint state
                msg = JointState()
                msg.header.stamp = self.get_clock().now().to_msg()
                msg.name = ['valve_joint']
                msg.position = [status['pos_deg'] * 3.14159 / 180.0]  # Convert to radians
                msg.velocity = [status['vel_rad_s']]
                msg.effort = [status['torque_nm']]
                
                self.joint_state_pub.publish(msg)
        except Exception as e:
            self.get_logger().error(f'Update failed: {e}')
    
    def torque_callback(self, msg):
        """Handle torque command."""
        try:
            data = {"action": "set_torque", "value": msg.data}
            requests.post(f"{self.base_url}/odrive", 
                         headers=self.headers, json=data, timeout=1.0)
        except Exception as e:
            self.get_logger().error(f'Torque command failed: {e}')

def main(args=None):
    rclpy.init(args=args)
    node = SteveValveNode()
    rclpy.spin(node)
    node.destroy_node()
    rclpy.shutdown()

if __name__ == '__main__':
    main()
```

---

## Complete Workflows

### Automated Test Sequence

```python
def run_test_sequence(steve: SteveClient):
    """Run automated test sequence."""
    print("=== PRISM Automated Test Sequence ===\n")
    
    # 1. System initialization
    print("1. Initializing system...")
    steve.odrive_enable()
    time.sleep(1.0)
    
    odrive_status = steve.odrive_status()
    if odrive_status['axis_state'] != 8:
        print("ERROR: ODrive not in closed loop control")
        return False
    print("   ODrive enabled and ready\n")
    
    # 2. Test each preset
    presets = steve.get_presets()
    for preset in presets:
        print(f"2. Testing preset: {preset['name']} (index {preset['index']})")
        
        steve.load_preset(preset['index'])
        steve.start()
        time.sleep(2.0)
        
        # Collect samples
        samples = []
        for _ in range(50):  # 5 seconds at 10 Hz
            status = steve.get_status()
            samples.append(status)
            time.sleep(0.1)
        
        # Analyze
        max_pos = max(s['pos_deg'] for s in samples)
        min_pos = min(s['pos_deg'] for s in samples)
        mean_torque = sum(s['torque_nm'] for s in samples) / len(samples)
        
        print(f"   Position range: {min_pos:.1f}° to {max_pos:.1f}°")
        print(f"   Mean torque: {mean_torque:.3f} Nm\n")
        
        steve.stop()
        time.sleep(1.0)
    
    # 3. Performance check
    print("3. Checking performance metrics...")
    perf = steve.get_performance()
    print(f"   Loop time: {perf['loop_total_mean_us']:.1f} μs")
    print(f"   Torque clamps: {perf['torque_clamps']}")
    print(f"   Stability events: {perf['stability_events']}\n")
    
    # 4. Shutdown
    print("4. Shutting down...")
    steve.odrive_disable()
    print("   Test sequence complete!\n")
    
    return True

# Run test
steve = SteveClient("192.168.1.100")
success = run_test_sequence(steve)
print(f"Test {'PASSED' if success else 'FAILED'}")
```

### Production Monitoring Script

```python
#!/usr/bin/env python3
"""
PRISM production monitoring script.
Logs status, detects anomalies, sends alerts.
"""

import time
import logging
from datetime import datetime

# Configure logging
logging.basicConfig(
    filename=f'steve_monitor_{datetime.now().strftime("%Y%m%d")}.log',
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s'
)

def monitor_steve(steve: SteveClient, duration_hours: float):
    """Monitor PRISM for specified duration."""
    logging.info("=== Monitoring started ===")
    
    start_time = time.time()
    end_time = start_time + duration_hours * 3600
    
    error_count = 0
    max_errors = 10
    
    while time.time() < end_time:
        try:
            # Get status
            status = steve.get_status()
            
            # Check for anomalies
            if status['mode'] != 'running':
                logging.warning(f"Valve not running: mode={status['mode']}")
            
            if abs(status['torque_nm']) > 0.8:
                logging.warning(f"High torque: {status['torque_nm']:.3f} Nm")
            
            # Get ODrive status
            odrive = steve.odrive_status()
            if odrive['axis_error'] != 0:
                logging.error(f"ODrive error: {odrive['axis_error']}")
                error_count += 1
            
            if odrive['fet_temp'] > 70:
                logging.warning(f"High FET temp: {odrive['fet_temp']:.1f}°C")
            
            # Reset error count on success
            if error_count > 0:
                error_count -= 1
            
        except Exception as e:
            logging.error(f"Monitoring error: {e}")
            error_count += 1
            
            if error_count >= max_errors:
                logging.critical("Too many errors, stopping monitoring")
                break
        
        time.sleep(10)  # Check every 10 seconds
    
    logging.info("=== Monitoring stopped ===")

# Run monitoring
steve = SteveClient("192.168.1.100")
monitor_steve(steve, duration_hours=8)  # Monitor for 8 hours
```

---

## Troubleshooting

### Connection Issues

```python
def diagnose_connection(host):
    """Diagnose connection issues."""
    import socket
    
    print(f"Diagnosing connection to {host}...")
    
    # Test network reachability
    try:
        socket.create_connection((host, 8080), timeout=5)
        print("✓ Network connection OK")
    except Exception as e:
        print(f"✗ Network connection failed: {e}")
        return
    
    # Test HTTP connection
    try:
        import requests
        response = requests.get(f"http://{host}:8080", timeout=5)
        print(f"✓ HTTP server responding (status {response.status_code})")
    except Exception as e:
        print(f"✗ HTTP request failed: {e}")
        return
    
    # Test API authentication
    try:
        response = requests.get(
            f"http://{host}:8080/api/v1/status",
            headers={"X-API-Key": "steve-valve-2025"},
            timeout=5
        )
        if response.status_code == 200:
            print("✓ API authentication OK")
        else:
            print(f"✗ API authentication failed: HTTP {response.status_code}")
    except Exception as e:
        print(f"✗ API request failed: {e}")

diagnose_connection("192.168.1.100")
```

---

## See Also

- [REST API Reference](rest-api.md) - Complete API documentation
- [Web Interface Guide](../html/web-interface-guide.md) - Browser-based control using REST API
- [Streaming Guide](../stream/streaming-guide.md) - Real-time TCP data streaming
- [Streaming Examples](../stream/streaming-examples.md) - Streaming integration code
- [CLI Reference](../cli/cli-reference.md) - Command-line interface
- [Getting Started Guide](../getting-started/getting-started.md) - Initial setup

For questions or issues, refer to the project repository or documentation.

