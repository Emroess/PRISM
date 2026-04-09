# PRISM Streaming Examples

Complete code examples for consuming real-time valve simulation data from PRISM's TCP streaming server.

## Python Examples

### Basic Python Stream Reader

Simple example for reading and printing streaming data:

```python
import socket
import json

def connect_stream(host='192.168.1.100', port=8888):
    """Connect to PRISM streaming server."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.connect((host, port))
    sock.settimeout(5.0)  # 5 second timeout
    return sock

def read_stream_data(sock):
    """Read one line of JSON data from stream."""
    buffer = b''
    while True:
        chunk = sock.recv(1)
        if not chunk:
            return None
        buffer += chunk
        if chunk == b'\n':
            try:
                return json.loads(buffer.decode('utf-8'))
            except json.JSONDecodeError as e:
                print(f"JSON decode error: {e}")
                return None

def main():
    # Connect to stream

## Table of Contents

- [Python Examples](#python-examples)
  - [Basic Python Stream Reader](#basic-python-stream-reader)
  - [Python Stream Reader with Threading](#python-stream-reader-with-threading)
  - [Python Data Logger](#python-data-logger)
- [C++ Examples](#c-examples)
  - [C++ Stream Reader with Boost.Asio](#c-stream-reader-with-boostasio)
- [JavaScript/Node.js Examples](#javascriptnodejs-examples)
  - [Node.js Stream Reader](#nodejs-stream-reader)
- [ROS 2 Example](#ros-2-example)
  - [ROS 2 Streaming Subscriber Node](#ros-2-streaming-subscriber-node)
  - [ROS 2 Launch File](#ros-2-launch-file)
- [MATLAB Example](#matlab-example)
  - [MATLAB Stream Reader](#matlab-stream-reader)
- [See Also](#see-also)


    sock = connect_stream('192.168.1.100', 8888)
    print("Connected to PRISM streaming server")
    
    try:
        # Read hello message
        hello = read_stream_data(sock)
        print(f"Hello: {hello}")
        
        # Read continuous data
        count = 0
        while count < 100:  # Read 100 samples
            data = read_stream_data(sock)
            if data and data.get('data_valid', False):
                print(f"[{count}] pos={data['position_deg']:.1f}° "
                      f"vel={data['omega_rad_s']:.3f} rad/s "
                      f"torque={data['torque_nm']:.3f} Nm")
                count += 1
    
    finally:
        sock.close()

if __name__ == "__main__":
    main()
```

### Python Stream Reader with Threading

Robust implementation with background thread and data callback:

```python
import socket
import json
import threading
import time
from typing import Callable, Optional

class SteveStreamClient:
    """Thread-safe PRISM streaming client."""
    
    def __init__(self, host: str = '192.168.1.100', port: int = 8888):
        self.host = host
        self.port = port
        self.socket: Optional[socket.socket] = None
        self.thread: Optional[threading.Thread] = None
        self.running = False
        self.connected = False
        self.callback: Optional[Callable] = None
        self.error_callback: Optional[Callable] = None
        self.samples_received = 0
        self.errors = 0
        
    def connect(self) -> bool:
        """Connect to PRISM streaming server."""
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.settimeout(5.0)
            self.socket.connect((self.host, self.port))
            self.connected = True
            print(f"Connected to PRISM at {self.host}:{self.port}")
            return True
        except Exception as e:
            print(f"Connection failed: {e}")
            self.connected = False
            return False
    
    def disconnect(self):
        """Disconnect from streaming server."""
        self.running = False
        if self.thread:
            self.thread.join(timeout=2.0)
        if self.socket:
            self.socket.close()
            self.socket = None
        self.connected = False
        print("Disconnected from PRISM")
    
    def set_data_callback(self, callback: Callable):
        """Set callback for receiving data packets."""
        self.callback = callback
    
    def set_error_callback(self, callback: Callable):
        """Set callback for error notifications."""
        self.error_callback = callback
    
    def start(self):
        """Start streaming in background thread."""
        if not self.connected:
            if not self.connect():
                return False
        
        self.running = True
        self.thread = threading.Thread(target=self._stream_loop, daemon=True)
        self.thread.start()
        return True
    
    def stop(self):
        """Stop streaming."""
        self.disconnect()
    
    def _read_line(self) -> Optional[str]:
        """Read one line from socket."""
        buffer = b''
        try:
            while self.running:
                chunk = self.socket.recv(1)
                if not chunk:
                    return None
                buffer += chunk
                if chunk == b'\n':
                    return buffer.decode('utf-8').strip()
        except socket.timeout:
            return None
        except Exception as e:
            if self.running:  # Only report if not shutting down
                if self.error_callback:
                    self.error_callback(f"Socket read error: {e}")
            return None
    
    def _stream_loop(self):
        """Background streaming loop."""
        try:
            # Read hello message
            hello_line = self._read_line()
            if hello_line:
                hello = json.loads(hello_line)
                print(f"Stream ready: {hello}")
            
            # Read continuous data
            while self.running:
                line = self._read_line()
                if not line:
                    if self.running:
                        # Connection lost
                        if self.error_callback:
                            self.error_callback("Connection lost")
                        break
                    continue
                
                try:
                    data = json.loads(line)
                    self.samples_received += 1
                    
                    if self.callback:
                        self.callback(data)
                
                except json.JSONDecodeError as e:
                    self.errors += 1
                    if self.error_callback:
                        self.error_callback(f"JSON decode error: {e}")
        
        except Exception as e:
            if self.error_callback:
                self.error_callback(f"Stream loop error: {e}")
        
        finally:
            self.connected = False
            print("Stream loop ended")
    
    def get_stats(self) -> dict:
        """Get client statistics."""
        return {
            'connected': self.connected,
            'samples_received': self.samples_received,
            'errors': self.errors
        }

# Example usage
def data_handler(data):
    """Process incoming data packet."""
    if data.get('data_valid', False):
        print(f"pos={data['position_deg']:.2f}° "
              f"vel={data['omega_rad_s']:.3f} "
              f"torque={data['torque_nm']:.3f}")

def error_handler(error):
    """Handle errors."""
    print(f"ERROR: {error}")

def main():
    # Create client
    client = SteveStreamClient('192.168.1.100', 8888)
    
    # Set callbacks
    client.set_data_callback(data_handler)
    client.set_error_callback(error_handler)
    
    # Start streaming
    if client.start():
        print("Streaming started. Press Ctrl+C to stop...")
        try:
            while True:
                time.sleep(1)
                stats = client.get_stats()
                print(f"Stats: {stats['samples_received']} samples, "
                      f"{stats['errors']} errors")
        except KeyboardInterrupt:
            print("\nStopping...")
    
    # Stop streaming
    client.stop()

if __name__ == "__main__":
    main()
```

### Python Data Logger

Log streaming data to CSV file:

```python
import socket
import json
import csv
import time
from datetime import datetime

class SteveDataLogger:
    """Log PRISM streaming data to CSV file."""
    
    def __init__(self, host='192.168.1.100', port=8888):
        self.host = host
        self.port = port
        self.socket = None
        
    def connect(self):
        """Connect to PRISM."""
        self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.socket.connect((self.host, self.port))
        self.socket.settimeout(5.0)
        # Read hello message
        self._read_line()
    
    def _read_line(self):
        """Read one line from socket."""
        buffer = b''
        while True:
            chunk = self.socket.recv(1)
            if not chunk:
                return None
            buffer += chunk
            if chunk == b'\n':
                return buffer.decode('utf-8').strip()
    
    def log_to_csv(self, filename, duration_sec=60, sample_rate_hz=50):
        """Log data to CSV file for specified duration."""
        self.connect()
        
        # Open CSV file
        with open(filename, 'w', newline='') as csvfile:
            fieldnames = [
                'timestamp_ms', 't_us', 'seq',
                'position_turns', 'position_deg',
                'omega_rad_s', 'torque_nm', 'filt_torque_nm',
                'status', 'passivity_mj', 'data_valid'
            ]
            writer = csv.DictWriter(csvfile, fieldnames=fieldnames)
            writer.writeheader()
            
            start_time = time.time()
            samples = 0
            target_samples = duration_sec * sample_rate_hz
            
            print(f"Logging to {filename}...")
            print(f"Target: {target_samples} samples ({duration_sec}s @ {sample_rate_hz}Hz)")
            
            while samples < target_samples:
                line = self._read_line()
                if not line:
                    break
                
                try:
                    data = json.loads(line)
                    
                    # Write to CSV
                    row = {
                        'timestamp_ms': data.get('timestamp_ms', 0),
                        't_us': data.get('t_us', 0),
                        'seq': data.get('seq', 0),
                        'position_turns': data.get('position_turns', 0.0),
                        'position_deg': data.get('position_deg', 0.0),
                        'omega_rad_s': data.get('omega_rad_s', 0.0),
                        'torque_nm': data.get('torque_nm', 0.0),
                        'filt_torque_nm': data.get('filt_torque_nm', 0.0),
                        'status': data.get('status', 'UNKNOWN'),
                        'passivity_mj': data.get('passivity_mj', 0),
                        'data_valid': data.get('data_valid', False)
                    }
                    writer.writerow(row)
                    
                    samples += 1
                    if samples % 100 == 0:
                        print(f"  {samples}/{target_samples} samples...")
                
                except json.JSONDecodeError:
                    continue
            
            elapsed = time.time() - start_time
            print(f"Logged {samples} samples in {elapsed:.2f}s "
                  f"(avg rate: {samples/elapsed:.1f} Hz)")
        
        self.socket.close()

# Example usage
if __name__ == "__main__":
    logger = SteveDataLogger('192.168.1.100', 8888)
    
    # Generate filename with timestamp
    filename = f"steve_log_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
    
    # Log 60 seconds at 50 Hz
    logger.log_to_csv(filename, duration_sec=60, sample_rate_hz=50)
    
    print(f"Data saved to {filename}")
```

## C++ Examples

### C++ Stream Reader with Boost.Asio

```cpp
#include <boost/asio.hpp>
#include <iostream>
#include <string>
#include <nlohmann/json.hpp>

using boost::asio::ip::tcp;
using json = nlohmann::json;

class SteveStreamClient {
public:
    SteveStreamClient(const std::string& host, int port)
        : host_(host), port_(port), socket_(io_context_) {}
    
    bool connect() {
        try {
            tcp::resolver resolver(io_context_);
            auto endpoints = resolver.resolve(host_, std::to_string(port_));
            boost::asio::connect(socket_, endpoints);
            std::cout << "Connected to PRISM at " << host_ << ":" << port_ << std::endl;
            return true;
        } catch (std::exception& e) {
            std::cerr << "Connection failed: " << e.what() << std::endl;
            return false;
        }
    }
    
    void read_stream(int num_samples) {
        try {
            boost::asio::streambuf buffer;
            
            // Read hello message
            boost::asio::read_until(socket_, buffer, '\n');
            std::istream is(&buffer);
            std::string line;
            std::getline(is, line);
            auto hello = json::parse(line);
            std::cout << "Hello: " << hello.dump() << std::endl;
            
            // Read data samples
            for (int i = 0; i < num_samples; ++i) {
                boost::asio::read_until(socket_, buffer, '\n');
                std::getline(is, line);
                
                auto data = json::parse(line);
                
                if (data["data_valid"].get<bool>()) {
                    std::cout << "[" << i << "] "
                              << "pos=" << data["position_deg"].get<double>() << "° "
                              << "vel=" << data["omega_rad_s"].get<double>() << " "
                              << "torque=" << data["torque_nm"].get<double>() << std::endl;
                }
            }
            
        } catch (std::exception& e) {
            std::cerr << "Stream error: " << e.what() << std::endl;
        }
    }
    
    void close() {
        socket_.close();
    }

private:
    std::string host_;
    int port_;
    boost::asio::io_context io_context_;
    tcp::socket socket_;
};

int main() {
    SteveStreamClient client("192.168.1.100", 8888);
    
    if (client.connect()) {
        client.read_stream(100);  // Read 100 samples
        client.close();
    }
    
    return 0;
}
```

## JavaScript/Node.js Examples

### Node.js Stream Reader

```javascript
const net = require('net');
const readline = require('readline');

class SteveStreamClient {
    constructor(host = '192.168.1.100', port = 8888) {
        this.host = host;
        this.port = port;
        this.client = null;
        this.rl = null;
        this.connected = false;
        this.samplesReceived = 0;
    }
    
    connect() {
        return new Promise((resolve, reject) => {
            this.client = new net.Socket();
            
            this.client.connect(this.port, this.host, () => {
                console.log(`Connected to PRISM at ${this.host}:${this.port}`);
                this.connected = true;
                
                // Set up line reader
                this.rl = readline.createInterface({
                    input: this.client,
                    crlfDelay: Infinity
                });
                
                resolve();
            });
            
            this.client.on('error', (err) => {
                console.error('Connection error:', err.message);
                this.connected = false;
                reject(err);
            });
            
            this.client.on('close', () => {
                console.log('Connection closed');
                this.connected = false;
            });
        });
    }
    
    onData(callback) {
        if (!this.rl) {
            throw new Error('Not connected');
        }
        
        this.rl.on('line', (line) => {
            try {
                const data = JSON.parse(line);
                this.samplesReceived++;
                callback(data);
            } catch (err) {
                console.error('JSON parse error:', err.message);
            }
        });
    }
    
    disconnect() {
        if (this.client) {
            this.client.destroy();
            this.client = null;
        }
        this.connected = false;
    }
    
    getStats() {
        return {
            connected: this.connected,
            samplesReceived: this.samplesReceived
        };
    }
}

// Example usage
async function main() {
    const client = new SteveStreamClient('192.168.1.100', 8888);
    
    try {
        await client.connect();
        
        client.onData((data) => {
            if (data.data_valid) {
                console.log(`pos=${data.position_deg.toFixed(2)}° ` +
                           `vel=${data.omega_rad_s.toFixed(3)} ` +
                           `torque=${data.torque_nm.toFixed(3)}`);
            }
        });
        
        // Run for 10 seconds
        await new Promise(resolve => setTimeout(resolve, 10000));
        
        const stats = client.getStats();
        console.log(`Received ${stats.samplesReceived} samples`);
        
    } catch (err) {
        console.error('Error:', err.message);
    } finally {
        client.disconnect();
    }
}

main();
```

## ROS 2 Example

### ROS 2 Streaming Subscriber Node

```python
import rclpy
from rclpy.node import Node
from std_msgs.msg import Float64
from geometry_msgs.msg import Twist
import socket
import json
import threading

class SteveStreamNode(Node):
    """ROS 2 node for PRISM TCP streaming."""
    
    def __init__(self):
        super().__init__('steve_stream_node')
        
        # Parameters
        self.declare_parameter('host', '192.168.1.100')
        self.declare_parameter('port', 8888)
        
        self.host = self.get_parameter('host').value
        self.port = self.get_parameter('port').value
        
        # Publishers
        self.position_pub = self.create_publisher(Float64, 'steve/position', 10)
        self.velocity_pub = self.create_publisher(Float64, 'steve/velocity', 10)
        self.torque_pub = self.create_publisher(Float64, 'steve/torque', 10)
        
        # Socket
        self.socket = None
        self.running = False
        self.thread = None
        
        # Connect and start
        self.connect()
        self.start_streaming()
        
        self.get_logger().info(f'PRISM stream node started ({self.host}:{self.port})')
    
    def connect(self):
        """Connect to PRISM streaming server."""
        try:
            self.socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.socket.settimeout(5.0)
            self.socket.connect((self.host, self.port))
            self.get_logger().info('Connected to PRISM')
        except Exception as e:
            self.get_logger().error(f'Connection failed: {e}')
            raise
    
    def start_streaming(self):
        """Start streaming in background thread."""
        self.running = True
        self.thread = threading.Thread(target=self._stream_loop, daemon=True)
        self.thread.start()
    
    def _read_line(self):
        """Read one line from socket."""
        buffer = b''
        while self.running:
            try:
                chunk = self.socket.recv(1)
                if not chunk:
                    return None
                buffer += chunk
                if chunk == b'\n':
                    return buffer.decode('utf-8').strip()
            except socket.timeout:
                continue
            except Exception as e:
                self.get_logger().error(f'Socket read error: {e}')
                return None
        return None
    
    def _stream_loop(self):
        """Background streaming loop."""
        try:
            # Read hello message
            hello_line = self._read_line()
            if hello_line:
                hello = json.loads(hello_line)
                self.get_logger().info(f"Stream ready: {hello}")
            
            # Read continuous data
            while self.running:
                line = self._read_line()
                if not line:
                    break
                
                try:
                    data = json.loads(line)
                    
                    if data.get('data_valid', False):
                        # Publish position
                        pos_msg = Float64()
                        pos_msg.data = data['position_deg']
                        self.position_pub.publish(pos_msg)
                        
                        # Publish velocity
                        vel_msg = Float64()
                        vel_msg.data = data['omega_rad_s']
                        self.velocity_pub.publish(vel_msg)
                        
                        # Publish torque
                        torque_msg = Float64()
                        torque_msg.data = data['torque_nm']
                        self.torque_pub.publish(torque_msg)
                
                except json.JSONDecodeError as e:
                    self.get_logger().warn(f'JSON decode error: {e}')
        
        except Exception as e:
            self.get_logger().error(f'Stream loop error: {e}')
        
        finally:
            self.get_logger().info('Stream loop ended')
    
    def destroy_node(self):
        """Clean up on shutdown."""
        self.running = False
        if self.thread:
            self.thread.join(timeout=2.0)
        if self.socket:
            self.socket.close()
        super().destroy_node()

def main(args=None):
    rclpy.init(args=args)
    node = SteveStreamNode()
    
    try:
        rclpy.spin(node)
    except KeyboardInterrupt:
        pass
    finally:
        node.destroy_node()
        rclpy.shutdown()

if __name__ == '__main__':
    main()
```

### ROS 2 Launch File

```python
from launch import LaunchDescription
from launch_ros.actions import Node

def generate_launch_description():
    return LaunchDescription([
        Node(
            package='steve_stream',
            executable='steve_stream_node',
            name='steve_stream',
            parameters=[
                {'host': '192.168.1.100'},
                {'port': 8888}
            ],
            output='screen'
        )
    ])
```

## MATLAB Example

### MATLAB Stream Reader

```matlab
% Connect to PRISM streaming server
host = '192.168.1.100';
port = 8888;
t = tcpclient(host, port);
t.Timeout = 5;

fprintf('Connected to PRISM at %s:%d\n', host, port);

% Read hello message
hello_line = readline(t);
hello = jsondecode(hello_line);
fprintf('Stream ready: interval=%d ms\n', hello.interval_ms);

% Pre-allocate arrays for data
num_samples = 1000;
position = zeros(num_samples, 1);
velocity = zeros(num_samples, 1);
torque = zeros(num_samples, 1);
timestamps = zeros(num_samples, 1);

% Read data
fprintf('Reading %d samples...\n', num_samples);
for i = 1:num_samples
    line = readline(t);
    data = jsondecode(line);
    
    if data.data_valid
        position(i) = data.position_deg;
        velocity(i) = data.omega_rad_s;
        torque(i) = data.torque_nm;
        timestamps(i) = data.timestamp_ms;
    end
    
    if mod(i, 100) == 0
        fprintf('  %d/%d samples\n', i, num_samples);
    end
end

% Close connection
clear t;
fprintf('Disconnected\n');

% Plot results
figure;
subplot(3,1,1);
plot(timestamps, position);
ylabel('Position (deg)');
title('PRISM Streaming Data');

subplot(3,1,2);
plot(timestamps, velocity);
ylabel('Velocity (rad/s)');

subplot(3,1,3);
plot(timestamps, torque);
ylabel('Torque (Nm)');
xlabel('Time (ms)');
```

## See Also

- [Streaming Guide](streaming-guide.md) - Complete streaming documentation
- [REST API Reference](../rest/rest-api.md) - Control streaming via REST API
- [CLI Reference](../cli/cli-reference.md) - Control streaming via CLI
- [Getting Started Guide](../getting-started/getting-started.md) - Initial setup

