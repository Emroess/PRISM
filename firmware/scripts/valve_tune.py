#!/usr/bin/env python3
"""
Valve tuning diagnostic script - interacts with firmware over serial
"""
import serial
import time
import sys

SERIAL_PORT = '/dev/ttyACM0'
BAUD_RATE = 115200

def send_command(ser, cmd, delay=0.5):
    """Send command and wait for response"""
    print(f"\n>>> {cmd}")
    ser.write(f"{cmd}\n".encode())
    time.sleep(delay)
    
    # Read response
    response = ""
    while ser.in_waiting > 0:
        response += ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
        time.sleep(0.1)
    
    if response:
        print(response, end='')
    return response

def main():
    print("=== Valve Diagnostic Workflow ===\n")
    
    try:
        # Open serial connection
        ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
        time.sleep(0.5)
        
        # Clear any pending data
        ser.reset_input_buffer()
        
        # Step 1: Initialize Odrive
        print("\n--- Step 1: Initialize Odrive ---")
        send_command(ser, "odrive_init", 2)
        send_command(ser, "odrive_enable", 2)
        
        # Step 2: Load valve preset
        print("\n--- Step 2: Load Valve Preset (Gate Valve) ---")
        send_command(ser, "valve_preset 0", 1)
        send_command(ser, "valve_info", 1)
        
        # Step 3: Start valve control loop
        print("\n--- Step 3: Start Valve Control Loop ---")
        send_command(ser, "valve_start", 2)
        
        # Let it run for a bit
        print("\nWaiting 5 seconds for valve to stabilize...")
        time.sleep(5)
        
        # Step 4: Get diagnostics
        print("\n--- Step 4: Valve Status ---")
        send_command(ser, "valve_status", 1)
        
        print("\n\n=== Diagnostics Complete ===")
        print("Review output above for tuning recommendations.")
        
        ser.close()
        
    except serial.SerialException as e:
        print(f"Error: {e}")
        print("Make sure /dev/ttyACM0 is not in use by another program")
        sys.exit(1)
    except KeyboardInterrupt:
        print("\nInterrupted by user")
        ser.close()
        sys.exit(0)

if __name__ == '__main__':
    main()
