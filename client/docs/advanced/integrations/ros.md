# ROS Integration Tutorial

Learn how to integrate PRISM haptic valves with ROS2 for robotics applications.

## Prerequisites

- PySteve installed with ROS extras: `pip install pysteve[ros]`
- ROS2 Humble, Iron, or Rolling installed
- PRISM device on network
- Basic ROS2 knowledge (nodes, topics, services)

## Overview

The ROS2 integration provides:
- **ROS2 node** - Bridge between PRISM and ROS ecosystem
- **Topics** - Publish valve state, subscribe to commands
- **Services** - Enable/disable motor, update parameters
- **Actions** - Move to target position with feedback
- **Launch files** - Easy deployment
- **Parameter server** - Dynamic reconfiguration

## Quick Start

### Start PRISM ROS Node

```bash
# Terminal 1: Start PRISM node
ros2 run pysteve steve_node --device-ip 192.168.1.100

# Terminal 2: Check topics
ros2 topic list

# Terminal 3: Echo state
ros2 topic echo /steve/state
```

### Python ROS Node

Create a simple publisher/subscriber:

```python
import rclpy
from rclpy.node import Node
from pysteve_msgs.msg import ValveState, ValveCommand

class SteveListener(Node):
    def __init__(self):
        super().__init__('steve_listener')
        
        # Subscribe to valve state
        self.sub = self.create_subscription(
            ValveState,
            '/steve/state',
            self.state_callback,
            10
        )
    
    def state_callback(self, msg):
        self.get_logger().info(
            f"Position: {msg.position_deg:.2f}°, "
            f"Torque: {msg.torque_nm:.3f} Nm"
        )

def main(args=None):
    rclpy.init(args=args)
    node = SteveListener()
    rclpy.spin(node)
    node.destroy_node()
    rclpy.shutdown()

if __name__ == '__main__':
    main()
```

## ROS2 Node Setup

### Installing ROS2 Bridge

```bash
# Install PySteve with ROS support
pip install pysteve[ros]

# Build ROS2 message package
cd ~/ros2_ws/src
git clone https://github.com/yourusername/pysteve_msgs
cd ~/ros2_ws
colcon build --packages-select pysteve_msgs
source install/setup.bash
```

### Message Definitions

**ValveState.msg**:
```
# Standard header with timestamp
std_msgs/Header header

# Valve position in degrees
float32 position_deg

# Angular velocity in degrees/second
float32 velocity_dps

# Applied torque in Newton-meters
float32 torque_nm

# Motor enabled flag
bool motor_enabled

# Valve control active flag
bool valve_running

# Current configuration
float32 viscous
float32 coulomb
float32 wall_stiffness
float32 wall_damping
```

**ValveCommand.msg**:
```
# Command type: 'enable', 'disable', 'start', 'stop', 'update'
string command

# Optional: Parameters for 'update' command
float32 viscous
float32 coulomb
float32 wall_stiffness
float32 wall_damping
float32 torque_limit
```

**ValveConfig.msg**:
```
float32 viscous
float32 coulomb
float32 wall_stiffness
float32 wall_damping
float32 smoothing
float32 torque_limit
float32 travel
```

## Topics

### Published Topics

**`/steve/state`** (pysteve_msgs/ValveState, 100 Hz)
- Real-time valve state updates

**`/steve/config`** (pysteve_msgs/ValveConfig, on change)
- Current valve configuration

**`/steve/diagnostics`** (diagnostic_msgs/DiagnosticArray, 1 Hz)
- Device health and status

### Subscribed Topics

**`/steve/command`** (pysteve_msgs/ValveCommand)
- Command valve operations

**`/steve/config_update`** (pysteve_msgs/ValveConfig)
- Update valve parameters

## Services

### Enable/Disable Motor

```bash
# Enable motor
ros2 service call /steve/enable_motor std_srvs/srv/Trigger

# Disable motor
ros2 service call /steve/disable_motor std_srvs/srv/Trigger
```

Python:
```python
from std_srvs.srv import Trigger

class SteveController(Node):
    def __init__(self):
        super().__init__('steve_controller')
        self.enable_client = self.create_client(Trigger, '/steve/enable_motor')
    
    def enable_motor(self):
        req = Trigger.Request()
        future = self.enable_client.call_async(req)
        rclpy.spin_until_future_complete(self, future)
        
        if future.result().success:
            self.get_logger().info("Motor enabled")
        else:
            self.get_logger().error(f"Failed: {future.result().message}")
```

### Start/Stop Valve

```bash
# Start valve control
ros2 service call /steve/start_valve std_srvs/srv/Trigger

# Stop valve control
ros2 service call /steve/stop_valve std_srvs/srv/Trigger
```

### Update Configuration

```bash
# Update parameters via service
ros2 service call /steve/update_config pysteve_msgs/srv/UpdateConfig \
  "{viscous: 0.08, coulomb: 0.015, wall_stiffness: 2.0}"
```

**UpdateConfig.srv**:
```
# Request
ValveConfig config
---
# Response
bool success
string message
```

### Load Preset

```bash
# Load preset
ros2 service call /steve/load_preset pysteve_msgs/srv/LoadPreset \
  "{preset_name: 'smooth'}"
```

**LoadPreset.srv**:
```
# Request
string preset_name  # 'smooth', 'medium', or 'tight'
---
# Response
bool success
string message
ValveConfig config
```

## Actions

### Move to Position

Move valve to target angle with feedback:

**MoveToPosition.action**:
```
# Goal
float32 target_deg     # Target position in degrees
float32 max_velocity   # Max velocity in dps
float32 tolerance      # Position tolerance in degrees
---
# Result
bool success
float32 final_position_deg
float32 error_deg
---
# Feedback
float32 current_position_deg
float32 distance_remaining_deg
float32 progress_percent
```

Command line:
```bash
ros2 action send_goal /steve/move_to_position pysteve_msgs/action/MoveToPosition \
  "{target_deg: 45.0, max_velocity: 30.0, tolerance: 1.0}" --feedback
```

Python client:
```python
from rclpy.action import ActionClient
from pysteve_msgs.action import MoveToPosition

class PositionMover(Node):
    def __init__(self):
        super().__init__('position_mover')
        self.action_client = ActionClient(
            self,
            MoveToPosition,
            '/steve/move_to_position'
        )
    
    def send_goal(self, target_deg):
        goal_msg = MoveToPosition.Goal()
        goal_msg.target_deg = target_deg
        goal_msg.max_velocity = 30.0
        goal_msg.tolerance = 1.0
        
        self.action_client.wait_for_server()
        self.future = self.action_client.send_goal_async(
            goal_msg,
            feedback_callback=self.feedback_callback
        )
        self.future.add_done_callback(self.goal_response_callback)
    
    def feedback_callback(self, feedback_msg):
        feedback = feedback_msg.feedback
        self.get_logger().info(
            f"Progress: {feedback.progress_percent:.1f}% "
            f"({feedback.current_position_deg:.1f}°)"
        )
    
    def goal_response_callback(self, future):
        goal_handle = future.result()
        if not goal_handle.accepted:
            self.get_logger().error("Goal rejected")
            return
        
        self.result_future = goal_handle.get_result_async()
        self.result_future.add_done_callback(self.result_callback)
    
    def result_callback(self, future):
        result = future.result().result
        if result.success:
            self.get_logger().info(
                f"Reached {result.final_position_deg:.2f}° "
                f"(error: {result.error_deg:.2f}°)"
            )
        else:
            self.get_logger().error("Move failed")
```

## Launch Files

### Basic Launch

**steve_node.launch.py**:
```python
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node

def generate_launch_description():
    return LaunchDescription([
        DeclareLaunchArgument(
            'device_ip',
            default_value='192.168.1.100',
            description='PRISM device IP address'
        ),
        
        DeclareLaunchArgument(
            'rate_hz',
            default_value='100',
            description='Streaming rate in Hz'
        ),
        
        Node(
            package='pysteve',
            executable='steve_node',
            name='steve_node',
            output='screen',
            parameters=[{
                'device_ip': LaunchConfiguration('device_ip'),
                'rate_hz': LaunchConfiguration('rate_hz'),
                'auto_enable': True,
                'auto_start': True
            }]
        )
    ])
```

Launch:
```bash
ros2 launch pysteve steve_node.launch.py device_ip:=192.168.1.100
```

### Multi-Device Launch

**multi_steve.launch.py**:
```python
from launch import LaunchDescription
from launch_ros.actions import Node

def generate_launch_description():
    return LaunchDescription([
        # Valve 1
        Node(
            package='pysteve',
            executable='steve_node',
            name='steve_valve_1',
            namespace='valve1',
            parameters=[{
                'device_ip': '192.168.1.100',
                'rate_hz': 100
            }]
        ),
        
        # Valve 2
        Node(
            package='pysteve',
            executable='steve_node',
            name='steve_valve_2',
            namespace='valve2',
            parameters=[{
                'device_ip': '192.168.1.101',
                'rate_hz': 100
            }]
        ),
        
        # Valve 3
        Node(
            package='pysteve',
            executable='steve_node',
            name='steve_valve_3',
            namespace='valve3',
            parameters=[{
                'device_ip': '192.168.1.102',
                'rate_hz': 100
            }]
        )
    ])
```

## Robot Integration Example

Integrate PRISM with MoveIt2 for robot manipulation:

```python
import rclpy
from rclpy.node import Node
from moveit_msgs.action import MoveGroup
from pysteve_msgs.msg import ValveState

class RobotValveController(Node):
    def __init__(self):
        super().__init__('robot_valve_controller')
        
        # Subscribe to valve state
        self.valve_sub = self.create_subscription(
            ValveState,
            '/steve/state',
            self.valve_callback,
            10
        )
        
        # MoveIt action client
        self.moveit_client = ActionClient(self, MoveGroup, 'move_action')
        
        self.valve_angle = 0.0
    
    def valve_callback(self, msg):
        self.valve_angle = msg.position_deg
    
    def grasp_valve(self):
        """Move robot to valve grasp pose"""
        goal = MoveGroup.Goal()
        goal.request.group_name = "manipulator"
        goal.request.goal_constraints = [self.create_valve_constraint()]
        
        self.moveit_client.send_goal_async(goal)
    
    def rotate_valve(self, target_angle):
        """Rotate valve to target angle"""
        # Calculate required end-effector rotation
        current_angle = self.valve_angle
        delta = target_angle - current_angle
        
        # Send Cartesian path
        # (Implementation depends on your robot)
        pass
```

## Parameter Server

Dynamic reconfiguration:

```python
from rcl_interfaces.msg import SetParametersResult

class SteveNode(Node):
    def __init__(self):
        super().__init__('steve_node')
        
        # Declare parameters
        self.declare_parameter('device_ip', '192.168.1.100')
        self.declare_parameter('rate_hz', 100)
        self.declare_parameter('auto_enable', False)
        self.declare_parameter('auto_start', False)
        
        # Valve parameters
        self.declare_parameter('viscous', 0.1)
        self.declare_parameter('coulomb', 0.015)
        self.declare_parameter('wall_stiffness', 2.0)
        
        # Register parameter callback
        self.add_on_set_parameters_callback(self.parameter_callback)
    
    def parameter_callback(self, params):
        for param in params:
            if param.name == 'viscous':
                self.steve_client.update_config(viscous=param.value)
            elif param.name == 'coulomb':
                self.steve_client.update_config(coulomb=param.value)
            elif param.name == 'wall_stiffness':
                self.steve_client.update_config(wall_stiffness=param.value)
        
        return SetParametersResult(successful=True)
```

Set parameters:
```bash
# Update viscous damping
ros2 param set /steve_node viscous 0.12

# Update multiple parameters
ros2 param set /steve_node coulomb 0.018
ros2 param set /steve_node wall_stiffness 2.5
```

## Visualization in RViz2

Visualize valve state in RViz2:

```python
from visualization_msgs.msg import Marker

class SteveVisualizer(Node):
    def __init__(self):
        super().__init__('steve_visualizer')
        
        self.marker_pub = self.create_publisher(Marker, '/steve/marker', 10)
        
        self.valve_sub = self.create_subscription(
            ValveState,
            '/steve/state',
            self.valve_callback,
            10
        )
    
    def valve_callback(self, msg):
        # Create cylinder marker for valve
        marker = Marker()
        marker.header.frame_id = "world"
        marker.header.stamp = self.get_clock().now().to_msg()
        marker.type = Marker.CYLINDER
        marker.action = Marker.ADD
        
        # Position
        marker.pose.position.x = 0.0
        marker.pose.position.y = 0.0
        marker.pose.position.z = 0.0
        
        # Orientation (rotate based on valve angle)
        from tf_transformations import quaternion_from_euler
        q = quaternion_from_euler(0, 0, msg.position_deg * 3.14159 / 180.0)
        marker.pose.orientation.x = q[0]
        marker.pose.orientation.y = q[1]
        marker.pose.orientation.z = q[2]
        marker.pose.orientation.w = q[3]
        
        # Scale
        marker.scale.x = 0.1
        marker.scale.y = 0.1
        marker.scale.z = 0.02
        
        # Color (based on torque)
        torque_normalized = abs(msg.torque_nm) / 0.5  # Normalize to 0-1
        marker.color.r = torque_normalized
        marker.color.g = 1.0 - torque_normalized
        marker.color.b = 0.0
        marker.color.a = 1.0
        
        self.marker_pub.publish(marker)
```

## Data Recording

Record ROS bag for analysis:

```bash
# Record all PRISM topics
ros2 bag record /steve/state /steve/config /steve/diagnostics

# Record specific topics with rate limit
ros2 bag record -e "/steve/.*" --max-bag-duration 300
```

Playback:
```bash
# Play back recorded data
ros2 bag play steve_recording

# Play at half speed
ros2 bag play steve_recording --rate 0.5
```

## Troubleshooting

### Node Not Starting

**Problem**: ROS node fails to start

**Solution**: Check device connectivity
```bash
# Test PRISM connection
python3 -c "from pysteve import SteveClient; c = SteveClient('192.168.1.100'); c.connect(); print('OK')"
```

### No Data on Topics

**Problem**: `/steve/state` topic has no messages

**Solution**: Ensure streaming is enabled
```python
# Check node parameters
ros2 param get /steve_node auto_start

# Start manually if needed
ros2 service call /steve/start_valve std_srvs/srv/Trigger
```

### High Latency

**Problem**: Valve state updates are delayed

**Solution**: Reduce QoS queue depth and increase rate
```bash
# Restart with higher rate
ros2 run pysteve steve_node --device-ip 192.168.1.100 --rate-hz 200
```

## Next Steps

- [MuJoCo Integration](mujoco-integration.md) - Combine ROS with MuJoCo
- [Multi-Device Coordination](../advanced/multi-device.md) - Control multiple valves
- [Data Recording Tutorial](data-recording.md) - Analysis techniques
- [ROS2 Documentation](https://docs.ros.org/en/humble/)

## Example Package Structure

```
steve_ros_example/
├── CMakeLists.txt
├── package.xml
├── launch/
│   ├── steve_node.launch.py
│   └── multi_steve.launch.py
├── config/
│   └── steve_params.yaml
└── scripts/
    ├── steve_controller.py
    └── steve_visualizer.py
```

