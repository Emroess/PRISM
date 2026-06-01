import socket
import struct
import time
import math

# --- Configuration ---
# Use localhost for testing on the same machine as Isaac Sim
TARGET_IP = "127.0.0.1" 
TARGET_PORT = 5005
UPDATE_RATE_HZ = 100  # Send 100 packets per second

# --- UDP Socket Setup ---
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

print(f"Starting mock hardware sender to {TARGET_IP}:{TARGET_PORT}")
print("Press Ctrl+C to stop.")

start_time = time.time()

try:
    while True:
        current_time = time.time() - start_time
        
        # --- Generate Mock Torque ---
        # Create a sine wave that oscillates between -8.0 and 8.0 Nm.
        # This matches the 8 Nm maximum torque limit of PRISM's 
        # Preset 0 (Quarter-Turn valve) and Preset 3 (Turn wheel)[cite: 73].
        frequency = 0.5  # 0.5 Hz (one full back-and-forth oscillation every 2 seconds)
        torque_command = 8.0 * math.sin(2 * math.pi * frequency * current_time)
        
        # --- Pack and Send Data ---
        # Pack the float as little-endian ('<f') to match the receiver
        data = struct.pack('<f', torque_command)
        sock.sendto(data, (TARGET_IP, TARGET_PORT))
        
        # Print to console so you can verify it's working
        print(f"Sent Torque: {torque_command:+.2f} Nm", end="\r")
        
        # Sleep to maintain the target update rate
        time.sleep(1.0 / UPDATE_RATE_HZ)

except KeyboardInterrupt:
    print("\nStopped mock hardware sender.")
finally:
    sock.close()