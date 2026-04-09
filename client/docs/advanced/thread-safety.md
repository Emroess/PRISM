# Thread Safety Guide


## Table of Contents

- [Overview](#overview)
- [SteveClient Thread Safety](#steveclient-thread-safety)
  - [Default Behavior (Not Thread-Safe)](#default-behavior-not-thread-safe)
  - [Enable Thread Safety](#enable-thread-safety)
  - [Manual Locking](#manual-locking)
- [SteveStreamer Thread Safety](#stevestreamer-thread-safety)
  - [Callback Execution](#callback-execution)
  - [Thread-Safe Callbacks](#thread-safe-callbacks)
  - [Multiple Callbacks](#multiple-callbacks)
- [DataRecorder Thread Safety](#datarecorder-thread-safety)
- [Common Patterns](#common-patterns)
  - [Pattern 1: Single Client, Multiple Readers](#pattern-1-single-client-multiple-readers)
  - [Pattern 2: Command Queue](#pattern-2-command-queue)
  - [Pattern 3: Async Alternative](#pattern-3-async-alternative)
- [Troubleshooting](#troubleshooting)
  - [Race Conditions](#race-conditions)
  - [Deadlocks](#deadlocks)
  - [Callback Delays](#callback-delays)
- [Performance Considerations](#performance-considerations)
  - [Lock Contention](#lock-contention)
  - [Lock-Free Alternatives](#lock-free-alternatives)
- [Best Practices](#best-practices)


Understanding thread safety in PySteve and best practices for concurrent access.

## Overview

PySteve components have different thread safety guarantees:

| Component          | Thread Safe | Notes                                     |
| ------------------ | ----------- | ----------------------------------------- |
| `SteveClient`      | No*         | Use `threadsafe=True` or external locking |
| `SteveStreamer`    | Yes         | Callbacks run in separate thread          |
| `SteveAsyncClient` | Yes         | Uses asyncio event loop                   |
| `DataRecorder`     | Yes         | Internal locking for data buffer          |
| `RealtimeTuner`    | No          | Wraps SteveClient                         |

Can be made thread-safe with `threadsafe=True` parameter

## SteveClient Thread Safety

### Default Behavior (Not Thread-Safe)

By default, `SteveClient` is **not thread-safe**:

```python
from pysteve import SteveClient
import threading

client = SteveClient("192.168.1.100")
client.connect()

def worker():
    # UNSAFE: Multiple threads accessing client
    status = client.get_status()
    print(status)

# This can cause race conditions!
threads = [threading.Thread(target=worker) for _ in range(5)]
for t in threads:
    t.start()
```

**Problems**:

- Race conditions in HTTP requests
- Corrupted state
- Connection errors

### Enable Thread Safety

Use `threadsafe=True` for automatic locking:

```python
client = SteveClient("192.168.1.100", threadsafe=True)

def worker():
    # SAFE: Internal lock protects access
    status = client.get_status()
    print(status)

threads = [threading.Thread(target=worker) for _ in range(5)]
for t in threads:
    t.start()
for t in threads:
    t.join()
```

How it works:

- Internal `threading.Lock` acquired for each operation
- Sequential execution of methods
- Small performance overhead

### Manual Locking

For finer control, use external lock:

```python
import threading

client = SteveClient("192.168.1.100")  # threadsafe=False
client_lock = threading.Lock()

def worker():
    with client_lock:
        # Protected critical section
        status = client.get_status()
        position = status['pos_deg']
        
        if position > 45:
            client.update_config(viscous=0.15)
        else:
            client.update_config(viscous=0.08)

threads = [threading.Thread(target=worker) for _ in range(5)]
for t in threads:
    t.start()
```

**Advantages**:

- Control lock granularity
- Atomic multi-operation sequences
- Better performance when needed

## SteveStreamer Thread Safety

`SteveStreamer` **is thread-safe** with important considerations:

### Callback Execution

Callbacks run in **separate thread**:

```python
from pysteve import SteveClient

client = SteveClient("192.168.1.100")
client.connect()
client.start_streaming()

def callback(sample):
    # This runs in streamer thread, NOT main thread!
    print(f"Position: {sample['position_deg']}")

client.streamer.register_callback(callback)

# Main thread continues...
import time
time.sleep(10)
```

**Thread diagram**:

```
Main Thread:        [client.connect()] [start_streaming()] [time.sleep(10)]
                                |
Streamer Thread:                +--[TCP recv]->[callback()]->[TCP recv]->...
```

### Thread-Safe Callbacks

Protect shared state in callbacks:

```python
import threading

data_lock = threading.Lock()
shared_data = []

def callback(sample):
    # Callback runs in streamer thread
    with data_lock:
        shared_data.append(sample)

client.streamer.register_callback(callback)
client.start_streaming()

# Main thread accessing shared data
import time
time.sleep(5)

with data_lock:
    print(f"Collected {len(shared_data)} samples")
```

### Multiple Callbacks

Multiple callbacks are executed **sequentially** in streamer thread:

```python
def callback1(sample):
    # Runs first
    process_position(sample['position_deg'])

def callback2(sample):
    # Runs second (after callback1 completes)
    process_torque(sample['torque_nm'])

client.streamer.register_callback(callback1)
client.streamer.register_callback(callback2)
```

**Important**: Slow callbacks block subsequent callbacks!

```python
def slow_callback(sample):
    time.sleep(0.1)  # BAD: Blocks streamer thread!
    process(sample)
```

**Solution**: Use queue for async processing:

```python
import queue
import threading

sample_queue = queue.Queue()

def fast_callback(sample):
    # Fast: Just enqueue
    sample_queue.put(sample)

def worker_thread():
    while True:
        sample = sample_queue.get()
        if sample is None:
            break
        
        # Slow processing in separate thread
        time.sleep(0.1)
        process(sample)

# Start worker
worker = threading.Thread(target=worker_thread, daemon=True)
worker.start()

# Register fast callback
client.streamer.register_callback(fast_callback)
client.start_streaming()
```

## DataRecorder Thread Safety

`DataRecorder` **is thread-safe**:

```python
from pysteve.control import DataRecorder

recorder = DataRecorder(client, client.streamer)

# Thread 1: Recording
def record_worker():
    recorder.start_recording()
    time.sleep(10)
    recorder.stop_recording()

# Thread 2: Checking status
def status_worker():
    while True:
        print(f"Samples: {len(recorder.data)}")
        time.sleep(1)

t1 = threading.Thread(target=record_worker)
t2 = threading.Thread(target=status_worker)
t1.start()
t2.start()
```

Internal locking protects:

- `recorder.data` list
- `recorder.is_recording` flag
- `recorder.start_time` / `recorder.end_time`

## Common Patterns

### Pattern 1: Single Client, Multiple Readers

```python
client = SteveClient("192.168.1.100", threadsafe=True)

def monitor_position():
    while True:
        pos = client.get_position()
        print(f"Position: {pos:.2f}°")
        time.sleep(0.5)

def monitor_torque():
    while True:
        torque = client.get_torque()
        print(f"Torque: {torque:.3f} Nm")
        time.sleep(0.5)

t1 = threading.Thread(target=monitor_position, daemon=True)
t2 = threading.Thread(target=monitor_torque, daemon=True)
t1.start()
t2.start()
```

### Pattern 2: Command Queue

```python
import queue

command_queue = queue.Queue()

def command_executor():
    """Single thread executing commands"""
    client = SteveClient("192.168.1.100")  # No threadsafe needed
    client.connect()
    
    while True:
        cmd = command_queue.get()
        if cmd is None:
            break
        
        # Execute command
        if cmd['type'] == 'update_config':
            client.update_config(**cmd['params'])
        elif cmd['type'] == 'load_preset':
            client.load_preset(cmd['preset'])

executor = threading.Thread(target=command_executor, daemon=True)
executor.start()

# Other threads enqueue commands
def user_input_thread():
    while True:
        preset = input("Preset: ")
        command_queue.put({'type': 'load_preset', 'preset': preset})

threading.Thread(target=user_input_thread, daemon=True).start()
```

### Pattern 3: Async Alternative

For I/O-bound concurrency, prefer `SteveAsyncClient`:

```python
import asyncio
from pysteve import SteveAsyncClient

async def monitor_position(client):
    while True:
        pos = await client.get_position()
        print(f"Position: {pos:.2f}°")
        await asyncio.sleep(0.5)

async def monitor_torque(client):
    while True:
        torque = await client.get_torque()
        print(f"Torque: {torque:.3f} Nm")
        await asyncio.sleep(0.5)

async def main():
    async with SteveAsyncClient("192.168.1.100") as client:
        await asyncio.gather(
            monitor_position(client),
            monitor_torque(client)
        )

asyncio.run(main())
```

**Advantages**:

- No locking needed
- Better performance for I/O
- Cleaner code

## Troubleshooting

### Race Conditions

**Symptom**: Inconsistent state, unexpected errors

**Example**:

```python
# UNSAFE
if client.is_valve_running():
    # Another thread might stop valve here!
    client.update_config(viscous=0.1)
```

**Solution**: Atomic operations with lock

```python
with client_lock:
    if client.is_valve_running():
        client.update_config(viscous=0.1)
```

### Deadlocks

**Symptom**: Program hangs

**Example**:

```python
def callback(sample):
    # BAD: Callback tries to acquire same lock as main thread!
    with client_lock:
        client.update_config(viscous=0.1)

with client_lock:
    client.streamer.register_callback(callback)  # Callback called -> deadlock!
```

**Solution**: Avoid nested locking or use queue pattern

### Callback Delays

**Symptom**: Streaming data arrives late

**Cause**: Slow callbacks

**Solution**:

```python
# Use queue to offload work
callback_queue = queue.Queue()

def fast_callback(sample):
    callback_queue.put_nowait(sample)  # Non-blocking

def worker():
    while True:
        sample = callback_queue.get()
        slow_processing(sample)

threading.Thread(target=worker, daemon=True).start()
client.streamer.register_callback(fast_callback)
```

## Performance Considerations

### Lock Contention

High contention reduces performance:

```python
# High contention: Many threads, frequent access
client = SteveClient("192.168.1.100", threadsafe=True)

for _ in range(100):
    threading.Thread(target=lambda: client.get_status()).start()
# Poor performance: threads wait for lock
```

**Solution**: Reduce access frequency or use async

### Lock-Free Alternatives

For read-only access, consider caching:

```python
import threading
import time

class CachedClient:
    def __init__(self, client):
        self.client = client
        self.cached_status = None
        self.cache_time = 0
        self.cache_lock = threading.Lock()
        
        # Background updater
        threading.Thread(target=self._updater, daemon=True).start()
    
    def _updater(self):
        while True:
            status = self.client.get_status()  # Single client access
            
            with self.cache_lock:
                self.cached_status = status
                self.cache_time = time.time()
            
            time.sleep(0.01)  # 100Hz update
    
    def get_status(self):
        # Read from cache (fast, no client access)
        with self.cache_lock:
            return self.cached_status.copy()

# Multiple threads read from cache
cached = CachedClient(client)

def reader():
    status = cached.get_status()  # Fast, no contention
    print(status)

for _ in range(100):
    threading.Thread(target=reader).start()
```

## Best Practices

1. **Default to async** - Use `SteveAsyncClient` for concurrent I/O
2. **Enable threadsafe** - Use `threadsafe=True` if you need threads
3. **Keep callbacks fast** - Offload work to separate threads/queues
4. **Avoid nested locks** - Careful with locks in callbacks
5. **Use higher-level APIs** - `DataRecorder` handles threading for you