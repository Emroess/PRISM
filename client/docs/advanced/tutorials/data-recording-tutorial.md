# Data Recording Tutorial

Learn how to capture, analyze, and export valve data for research and debugging.

## Prerequisites

- PySteve installed with data extras: `pip install pysteve[data]`
- PRISM device connected
- Basic Python knowledge

## Quick Start

```python
from pysteve import SteveClient
from pysteve.control import DataRecorder

# Connect and start valve
client = SteveClient("192.168.1.100")
client.connect()
client.enable_motor()
client.start_valve()

# Start recording
recorder = DataRecorder(client, client.streamer)
recorder.start_recording()

# ... operate the valve ...
input("Press Enter to stop recording...")

# Stop and export
recorder.stop_recording()
recorder.export_csv("my_recording.csv")

print(f"Recorded {len(recorder.data)} samples")
```

## DataRecorder Overview

The `DataRecorder` class captures streaming valve data with timestamps:

```python
from pysteve.control import DataRecorder

recorder = DataRecorder(
    client=client,
    streamer=client.streamer,
    buffer_size=10000  # Max samples before auto-flush
)
```

### Fields Captured

Each sample includes:
- `timestamp` (float): Unix timestamp in seconds
- `position_deg` (float): Valve position in degrees
- `velocity_dps` (float): Velocity in degrees/second
- `torque_nm` (float): Torque in Newton-meters
- `motor_enabled` (bool): Motor state
- `valve_running` (bool): Valve control active

## Basic Recording Workflow

### Manual Start/Stop

```python
# Start recording
recorder.start_recording()

# ... perform operations ...

# Stop recording
recorder.stop_recording()

# Access data
print(f"Samples: {len(recorder.data)}")
print(f"Duration: {recorder.duration:.2f} seconds")
print(f"First sample: {recorder.data[0]}")
```

### Context Manager (Auto Stop)

```python
with recorder:
    # Recording starts automatically
    time.sleep(10)
    # Recording stops when exiting
```

### Timed Recording

```python
# Record for exactly 30 seconds
recorder.record_duration(30.0)
```

## Export Formats

### CSV Export

Simple, human-readable format for Excel/spreadsheets:

```python
recorder.export_csv("data.csv")
```

Output:
```csv
timestamp,position_deg,velocity_dps,torque_nm,motor_enabled,valve_running
1678901234.567,45.2,12.3,-0.05,true,true
1678901234.577,45.3,12.1,-0.04,true,true
...
```

### JSON Export

Structured format with metadata:

```python
recorder.export_json("data.json")
```

Output:
```json
{
  "metadata": {
    "device_ip": "192.168.1.100",
    "start_time": 1678901234.567,
    "end_time": 1678901264.567,
    "duration": 30.0,
    "num_samples": 3000,
    "sample_rate": 100.0,
    "config": {
      "viscous": 0.1,
      "coulomb": 0.015,
      ...
    }
  },
  "data": [
    {
      "timestamp": 1678901234.567,
      "position_deg": 45.2,
      ...
    },
    ...
  ]
}
```

### HDF5 Export (Large Datasets)

Efficient binary format for large recordings:

```python
recorder.export_hdf5("data.h5")
```

Reading HDF5:
```python
import h5py

with h5py.File("data.h5", "r") as f:
    timestamps = f["timestamps"][:]
    positions = f["position_deg"][:]
    velocities = f["velocity_dps"][:]
    torques = f["torque_nm"][:]
    
    # Metadata
    device_ip = f.attrs["device_ip"]
    sample_rate = f.attrs["sample_rate"]
```

## Data Analysis

### Basic Statistics

```python
import numpy as np

# Extract fields
positions = np.array([s["position_deg"] for s in recorder.data])
velocities = np.array([s["velocity_dps"] for s in recorder.data])
torques = np.array([s["torque_nm"] for s in recorder.data])

# Compute statistics
print(f"Position range: [{positions.min():.1f}, {positions.max():.1f}] deg")
print(f"Mean velocity: {velocities.mean():.2f} dps")
print(f"Velocity std: {velocities.std():.2f} dps")
print(f"Max torque: {torques.max():.3f} Nm")
print(f"RMS torque: {np.sqrt(np.mean(torques**2)):.3f} Nm")
```

### Power Analysis

```python
# Compute instantaneous power
powers = [abs(s["velocity_dps"] * np.pi/180 * s["torque_nm"]) 
          for s in recorder.data]

mean_power = np.mean(powers)
total_energy = np.trapz(powers, dx=0.01)  # 100Hz sampling

print(f"Mean power: {mean_power:.3f} W")
print(f"Total energy: {total_energy:.3f} J")
```

### Frequency Analysis

```python
from scipy import signal
import numpy as np

# Get velocity signal
velocities = np.array([s["velocity_dps"] for s in recorder.data])
sample_rate = 100  # Hz

# Compute power spectral density
freqs, psd = signal.welch(velocities, fs=sample_rate, nperseg=1024)

# Find dominant frequency
dominant_idx = np.argmax(psd)
dominant_freq = freqs[dominant_idx]

print(f"Dominant frequency: {dominant_freq:.2f} Hz")

# Plot
import matplotlib.pyplot as plt
plt.semilogy(freqs, psd)
plt.xlabel("Frequency (Hz)")
plt.ylabel("PSD (dps²/Hz)")
plt.title("Velocity Power Spectrum")
plt.show()
```

## Visualization

### Real-Time Plotting

```python
from pysteve.utils import RealtimePlotter

# Create plotter
plotter = RealtimePlotter(
    fields=["position_deg", "velocity_dps", "torque_nm"],
    window_size=500,  # Show last 5 seconds at 100Hz
    update_interval=50  # Update every 50ms
)

# Register callback
client.streamer.register_callback(plotter.update)

# Start plotting (non-blocking)
import threading
plot_thread = threading.Thread(target=plotter.show, kwargs={"block": False})
plot_thread.daemon = True
plot_thread.start()

# Recording continues with live visualization
recorder.record_duration(30.0)
```

### Post-Recording Plots

```python
import matplotlib.pyplot as plt
import numpy as np

# Load data
timestamps = np.array([s["timestamp"] for s in recorder.data])
positions = np.array([s["position_deg"] for s in recorder.data])
velocities = np.array([s["velocity_dps"] for s in recorder.data])
torques = np.array([s["torque_nm"] for s in recorder.data])

# Relative time
t = timestamps - timestamps[0]

# Create figure
fig, axes = plt.subplots(3, 1, figsize=(10, 8), sharex=True)

# Position
axes[0].plot(t, positions)
axes[0].set_ylabel("Position (deg)")
axes[0].grid(True)

# Velocity
axes[1].plot(t, velocities)
axes[1].set_ylabel("Velocity (dps)")
axes[1].grid(True)

# Torque
axes[2].plot(t, torques)
axes[2].set_ylabel("Torque (Nm)")
axes[2].set_xlabel("Time (s)")
axes[2].grid(True)

plt.suptitle("Valve Recording")
plt.tight_layout()
plt.show()
```

### Phase Plot (Velocity vs Position)

```python
plt.figure(figsize=(8, 6))
plt.plot(positions, velocities, alpha=0.5)
plt.xlabel("Position (deg)")
plt.ylabel("Velocity (dps)")
plt.title("Phase Portrait")
plt.grid(True)
plt.show()
```

## Multi-Session Recording

### Recording Multiple Trials

```python
from pysteve import SteveClient
from pysteve.control import DataRecorder
import time

client = SteveClient("192.168.1.100")
client.connect()
client.enable_motor()
client.start_valve()

# Record multiple trials with different configurations
configs = [
    {"name": "low_damping", "viscous": 0.05},
    {"name": "medium_damping", "viscous": 0.1},
    {"name": "high_damping", "viscous": 0.15}
]

for config in configs:
    print(f"\nRecording {config['name']}...")
    
    # Apply configuration
    client.update_config(viscous=config["viscous"])
    time.sleep(1)  # Settle
    
    # Record
    recorder = DataRecorder(client, client.streamer)
    recorder.record_duration(10.0)
    
    # Export
    filename = f"trial_{config['name']}.csv"
    recorder.export_csv(filename)
    print(f"Saved to {filename}")

print("\nAll trials complete!")
```

### Automated Test Suite

```python
from pysteve.control import ParameterSweep, DataRecorder

sweep = ParameterSweep(client)

# Sweep viscous damping with data recording
results = sweep.sweep_range(
    parameter="viscous",
    start=0.05,
    end=0.2,
    steps=10,
    duration_per_step=5.0,
    export_data=True,  # Save each trial
    export_dir="viscous_sweep_data"
)

# Results include filenames
for result in results:
    print(f"viscous={result['value']:.2f} -> {result['data_file']}")
```

## Advanced Recording

### Triggered Recording

Start recording when condition is met:

```python
class TriggeredRecorder:
    def __init__(self, client, condition_fn):
        self.client = client
        self.condition_fn = condition_fn
        self.recorder = DataRecorder(client, client.streamer)
        self.triggered = False
    
    def callback(self, data):
        if not self.triggered and self.condition_fn(data):
            print("Trigger condition met! Starting recording...")
            self.recorder.start_recording()
            self.triggered = True
    
    def wait_for_trigger(self, timeout=60):
        self.client.streamer.register_callback(self.callback)
        
        start_time = time.time()
        while not self.triggered and (time.time() - start_time) < timeout:
            time.sleep(0.1)
        
        return self.triggered

# Example: Record when torque exceeds threshold
def high_torque_condition(data):
    return abs(data["torque_nm"]) > 0.1

triggered = TriggeredRecorder(client, high_torque_condition)

if triggered.wait_for_trigger(timeout=30):
    time.sleep(5)  # Record for 5 seconds after trigger
    triggered.recorder.stop_recording()
    triggered.recorder.export_csv("high_torque_event.csv")
else:
    print("Timeout: trigger condition not met")
```

### Windowed Recording

Keep only last N seconds:

```python
from collections import deque
import time

class WindowedRecorder:
    def __init__(self, client, window_seconds=10.0):
        self.client = client
        self.window_seconds = window_seconds
        self.data = deque(maxlen=int(window_seconds * 100))  # 100Hz
        
        client.streamer.register_callback(self._callback)
    
    def _callback(self, sample):
        self.data.append(sample)
    
    def save_snapshot(self, filename):
        """Save current window"""
        import csv
        with open(filename, "w", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=list(self.data[0].keys()))
            writer.writeheader()
            writer.writerows(self.data)

# Usage: Always keeps last 10 seconds in memory
recorder = WindowedRecorder(client, window_seconds=10.0)

# At any point, save the last 10 seconds
input("Press Enter to save snapshot...")
recorder.save_snapshot("last_10_seconds.csv")
```

## Data Synchronization

### Sync with External Events

```python
import time

class EventRecorder:
    def __init__(self, client):
        self.recorder = DataRecorder(client, client.streamer)
        self.events = []
    
    def mark_event(self, event_name):
        """Mark an event at current time"""
        self.events.append({
            "name": event_name,
            "timestamp": time.time()
        })
        print(f"Event marked: {event_name}")
    
    def export_with_events(self, data_file, events_file):
        """Export data and event markers"""
        self.recorder.export_csv(data_file)
        
        import csv
        with open(events_file, "w", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=["name", "timestamp"])
            writer.writeheader()
            writer.writerows(self.events)

# Usage
recorder = EventRecorder(client)
recorder.recorder.start_recording()

# Mark events during recording
time.sleep(2)
recorder.mark_event("robot_contact")
time.sleep(3)
recorder.mark_event("valve_full_open")
time.sleep(2)
recorder.mark_event("robot_release")

recorder.recorder.stop_recording()
recorder.export_with_events("data.csv", "events.csv")
```

## Best Practices

### 1. Set Appropriate Buffer Size

```python
# For long recordings (>1 min), use larger buffer
recorder = DataRecorder(client, client.streamer, buffer_size=50000)

# For short, frequent recordings, use smaller buffer
recorder = DataRecorder(client, client.streamer, buffer_size=5000)
```

### 2. Check Disk Space

```python
import shutil

def check_disk_space(required_mb=100):
    stat = shutil.disk_usage(".")
    free_mb = stat.free / (1024**2)
    
    if free_mb < required_mb:
        print(f"WARNING: Only {free_mb:.1f} MB free")
        return False
    return True

if check_disk_space(required_mb=100):
    recorder.record_duration(300)  # 5 minutes
```

### 3. Add Metadata

```python
# Include configuration in filename
config = client.get_config()
timestamp = time.strftime("%Y%m%d_%H%M%S")
filename = f"valve_{timestamp}_v{config['viscous']:.2f}_c{config['coulomb']:.3f}.csv"

recorder.export_csv(filename)
```

### 4. Validate Data Quality

```python
def validate_recording(recorder):
    """Check for common issues"""
    if len(recorder.data) == 0:
        print("ERROR: No data recorded")
        return False
    
    # Check sample rate
    timestamps = [s["timestamp"] for s in recorder.data]
    dts = np.diff(timestamps)
    mean_dt = np.mean(dts)
    expected_dt = 0.01  # 100Hz = 10ms
    
    if abs(mean_dt - expected_dt) > 0.001:
        print(f"WARNING: Sample rate off. Expected {expected_dt:.3f}s, got {mean_dt:.3f}s")
    
    # Check for gaps
    max_gap = np.max(dts)
    if max_gap > 0.05:  # 50ms gap
        print(f"WARNING: Large gap detected: {max_gap:.3f}s")
    
    return True

recorder.record_duration(10.0)
if validate_recording(recorder):
    recorder.export_csv("validated_data.csv")
```

## Troubleshooting

### No Data Recorded

**Problem**: `len(recorder.data)` is 0

**Solution**: Ensure streaming is started
```python
client.start_streaming()  # Start streaming explicitly
recorder.start_recording()
```

### Missing Samples

**Problem**: Gaps in data

**Solution**: Reduce network load or increase buffer size
```python
# Larger buffer
recorder = DataRecorder(client, client.streamer, buffer_size=20000)

# Lower streaming rate if needed
client.start_streaming(rate_hz=50)  # Reduce from 100Hz
```

### Export Fails

**Problem**: File write error

**Solution**: Check permissions and disk space
```python
import os

try:
    recorder.export_csv("data.csv")
except IOError as e:
    print(f"Export failed: {e}")
    # Try alternate location
    recorder.export_csv(os.path.expanduser("~/data.csv"))
```

## Next Steps

- [Parameter Tuning Tutorial](parameter-tuning-tutorial.md) - Use recordings to optimize parameters
- [Parameter Sweeps Tutorial](parameter-sweeps-tutorial.md) - Automated recording for testing
- [API Reference: DataRecorder](../../pyprism_api_reference.md) - Full API documentation

