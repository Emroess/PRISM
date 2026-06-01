import time
import sys
import os
import csv
import threading
from collections import deque

class _Getch:
    """Gets a single character from standard input.  Does not echo to the screen."""
    def __init__(self):
        try:
            self.impl = _GetchWindows()
        except ImportError:
            self.impl = _GetchUnix()

    def __call__(self): return self.impl()


class _GetchUnix:
    def __init__(self):
        import tty, sys

    def __call__(self):
        import sys, tty, termios
        fd = sys.stdin.fileno()
        old_settings = termios.tcgetattr(fd)
        try:
            tty.setraw(sys.stdin.fileno())
            ch = sys.stdin.read(1)
        finally:
            termios.tcsetattr(fd, termios.TCSADRAIN, old_settings)
        return ch


class _GetchWindows:
    def __init__(self):
        import msvcrt

    def __call__(self):
        import msvcrt
        return msvcrt.getch()


getch = _Getch()

def get_key_input():
    first_char = getch()
    # Debug print with repr() to see exact type and content
    # print(f"Debug: Key pressed repr: {repr(first_char)}") # Uncomment for debugging
    
    # helper to convert to bytes if needed for consistent checking
    if isinstance(first_char, str):
        first_char = first_char.encode('utf-8')
        
    # Check for arrow keys (Windows: \xe0 or \x00 prefix, Linux: \x1b[)
    if first_char == b'\xe0' or first_char == b'\x00': # Windows arrow prefix
        second_char = getch()
        if isinstance(second_char, str): second_char = second_char.encode('utf-8')
        
        if second_char == b'K': return 'LEFT'
        if second_char == b'M': return 'RIGHT'
        if second_char == b'H': return 'UP'
        if second_char == b'P': return 'DOWN'
        return 'OTHER'
    
    if first_char == b'\r' or first_char == b'\n': # Enter
        return 'ENTER'
    
    if first_char == b'\x1b': # Unix escape sequence
        # Try to read more chars non-blocking if possible, but standard getch is blocking
        # For simple arrow key detection, we can just read the next two chars
        # This is a bit simplistic and might block if it's just ESC key...
        # But for this script, we assume arrows or enter.
        # \x1b [ D is Left Arrow
        try:
           # Naive approach for Unix arrows
           c2 = getch()
           if isinstance(c2, str): c2 = c2.encode('utf-8')
           c3 = getch()
           if isinstance(c3, str): c3 = c3.encode('utf-8')
           
           if c2 == b'[' and c3 == b'D': return 'LEFT'
           if c2 == b'[' and c3 == b'C': return 'RIGHT'
        except:
            pass

    return 'OTHER'

# Add 'client' directory to sys.path to find pysteve
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), '../../')))

from pysteve import SteveClient

# Configuration
STEVE_IP = "10.0.0.18"
# Connect
client = SteveClient(STEVE_IP)
client.connect()

# Generate unique filename
base_name = "lever_eval_reset_complex"
file_ext = ".csv"
filename = f"{base_name}{file_ext}"
counter = 1
    
while os.path.exists(filename):
    filename = f"{base_name}{counter}{file_ext}"
    counter += 1
    
print(f"Data will be saved to: {filename}")

# CSV header + intialize csv file
header = ["Trial ID", "Total Time (s)", "Time to start Turning (s)", "Total Turning Time (s)", "Final Rotation Progress (decimal)", "Max Rotation Progress (decimal)", "Max Rotation (deg)"]
# Create CSV
with open(filename, "w", newline="", encoding='utf-8') as file:
    writer = csv.writer(file)
    writer.writerow(header)


class MovementDetector:
    def __init__(self, window_size=20, threshold_deg=1.0):
        self.window_size = window_size
        self.threshold_deg = threshold_deg
        self.buffer = deque(maxlen=window_size) # tuples (timestamp, pos_deg)
    
    def add(self, timestamp, pos_deg):
        self.buffer.append((timestamp, pos_deg))
    
    def detected(self):
        if len(self.buffer) < self.window_size:
            return False, None
        
        start_time, start_pos = self.buffer[0]
        curr_time, curr_pos = self.buffer[-1]
        
        # Check if displacement across the window > threshold
        displacement = abs(curr_pos - start_pos)
        if displacement > self.threshold_deg:
            return True, start_time
        return False, None

def complex_eval_reset():

    # client.load_preset(2)  # Load heavy preset
    client.update_config(
        viscous=0.1,
        coulomb=0.2,
        wall_stiffness=150,
        wall_damping=0,
        open_pos=0,
        closed_pos=180,
        torque_limit=10,
        smoothing=100
    )
    trial_counter = 0

    try:
        # keep looping until user is done 
        while True:
            # Ensure clean state by stopping first
            # This handles internal server state and ODrive mode reset
            client.stop_valve() 
            time.sleep(0.2)
            
            print(f"\n--- TRIAL {trial_counter} ---")
            input(f"Press Enter to start time recording script for Trial {trial_counter}...")
            start_time = time.time()
            
            client.enable_motor()
            client.start_valve()
            print("     Valve active. Monitoring movement (Complex Mode)...")

            # Shared state for thread communication
            monitor_state = {
                "running": True,
                "did_move": False,
                "start_turn_time": 0.0,
                "max_pos_deg": 0.0
            }
            
            # Define thread function locally to access scope
            def monitor_lever():
                # Initialize detector inside the thread
                detector = MovementDetector(window_size=40, threshold_deg=5.0)
                
                while monitor_state["running"]:
                    try:
                        status = client.get_status()
                        curr_pos = status.get('pos_deg', 0.0)
                        curr_time = time.time()
                        
                        detector.add(curr_time, curr_pos)
                        
                        # Update max position
                        if abs(curr_pos) > monitor_state["max_pos_deg"]:
                            monitor_state["max_pos_deg"] = abs(curr_pos)
                        
                        is_detected, detection_time = detector.detected()
                        
                        if not monitor_state["did_move"] and is_detected:
                            monitor_state["start_turn_time"] = detection_time
                            monitor_state["did_move"] = True
                            print(f"\n     [Movement Detected] Start Time: {detection_time - start_time:.4f}s relative to trigger")
                    except Exception as e:
                        print(f"     Error in monitor thread: {e}")
                        
                    time.sleep(0.01)

            # Start monitoring thread
            t = threading.Thread(target=monitor_lever)
            t.start()
            
            # Wait for user completion (BLOCKING)
            # The thread records movement in the background
            input("Press Enter to signal valve finished turning...")
            
            # Stop thread
            monitor_state["running"] = False
            t.join()

            
            # collect data
            curr_time = time.time()
            
            if monitor_state["did_move"]:
                time_to_start_turning = monitor_state["start_turn_time"] - start_time
                total_turning_time = curr_time - monitor_state["start_turn_time"]
            else:
                # Did not move
                time_to_start_turning = 0
                total_turning_time = 0

            # rotation progress
            curr_status = client.get_status()
            pos_deg = curr_status['pos_deg']
            final_rotation_progress = pos_deg / 90.0 # 90 degrees is the max rotation
            
            # Max rotation stats
            max_rotation_deg = monitor_state["max_pos_deg"]
            max_rotation_progress = max_rotation_deg / 90.0

            # Decision Loop
            print("Press ENTER to signal robot finished moving (save and continue), or LEFT ARROW to redo (discard data)...")
            
            curr_time = time.time()
            total_time = curr_time - start_time
            
            while True:
                key = get_key_input()
                if key == 'ENTER':
                    # Save Logic
                    with open(filename, "a", newline="", encoding='utf-8') as file:
                        writer = csv.writer(file)
                        writer.writerow([trial_counter, total_time, time_to_start_turning, total_turning_time, final_rotation_progress, max_rotation_progress, max_rotation_deg])
                    print(f"     Trial {trial_counter} SAVED.")
                    trial_counter += 1
                    break
                elif key == 'LEFT':
                    # Redo Logic
                    print(f"     Trial {trial_counter} DISCARDED (Redoing).")
                    break

            # Reset Phase
            input("Press Enter to reset end-effector...")
            print("     Resetting to 0 degrees...")
            # Use Velocity Control (Mode 2) for smooth return
            # Mode 3 (Position) without trajectory planning can be violent
            client.set_odrive_mode(2)

            while True:
                # 1. Get current status
                curr_status = client.get_status()
                pos_deg = curr_status['pos_deg']
                
                # Normalize angle to [-180, 180] to handle wrapping (e.g. 359 -> -1)
                # This prevents the "runaway" where crossing 0 causes it to race the long way around.
                if pos_deg > 180:
                    pos_deg -= 360
                elif pos_deg < -180:
                    pos_deg += 360
                    
                # 2. Check exit condition (close enough to 0)
                if abs(pos_deg) < 1.0:
                    break
                
                # 3. Calculate Velocity Command
                # Convert degrees to turns (1 turn = 360 degrees)
                pos_turns = pos_deg / 360.0
                target_turns = 0.0
                
                # P-Control: Velocity proportional to error
                # target_vel (turns/s) = error (turns) * gain
                kp = 10.0  # Gain: 0.25 turns error (90deg) -> 2.5 turns/s max speed
                error_turns = target_turns - pos_turns
                cmd_vel = error_turns * kp
                
                # 4. Safety Clamp (Max speed)
                # Limit to 5 turns/second
                max_vel = 3.0
                cmd_vel = max(min(cmd_vel, max_vel), -max_vel)
                
                client.set_odrive_velocity(cmd_vel)
                time.sleep(0.02) # Control loop rate (~50Hz)

            # Stop motion
            client.set_odrive_velocity(0)
            print("     Reset complete.")
            
            # Post-Reset Decision Loop
            print("\nPress ENTER to Continue to next trial, or RIGHT ARROW to Exit...")
            while True:
                key = get_key_input()
                if key == 'ENTER':
                    print("Continuing...")
                    break
                elif key == 'RIGHT':
                    print("Exiting program.")
                    return # Exit function, eventually exits main
            
            time.sleep(0.5)
            # Loop continues, re-enabling valve at top of loop
        

    finally:
        client.stop_valve()
        client.disconnect()


def main():
    
    try:
        complex_eval_reset()
        print("Program exited safely.")
    except KeyboardInterrupt:
        print("\n\nExamples interrupted by user.")
    except Exception as e:
        print(f"\n\nError: {e}")
        import traceback
        traceback.print_exc()


if __name__ == "__main__":
    main()