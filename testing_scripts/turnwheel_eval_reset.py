import time
import sys
import os
import csv
import threading

# Add 'client' directory to sys.path to find pysteve
sys.path.append(os.path.abspath(os.path.join(os.path.dirname(__file__), '../../')))

from pysteve import SteveClient

# Configuration
STEVE_IP = "10.0.0.18"
# Connect
client = SteveClient(STEVE_IP)
client.connect()

# CSV header + intialize csv file
header = ["Eval #", "Total Time (s)", "Time to start Turning (s)", "Total Turning Time (s)", "Rotation Progress (decimal)"]
with open("turnwheel_eval_reset.csv", "w", newline="", encoding='utf-8') as file:
    writer = csv.writer(file)
    writer.writerow(header)


# Global variables (removed threading vars)
max_deg = 0;

def simple_eval_reset():
    i = 1
    #client.load_preset(3)  # Load turnwheel preset
    client.start_valve()

    try:
        # keep looping until user is done 
        while True:
            # Ensure clean state by stopping first
            # This handles internal server state and ODrive mode reset
            client.stop_valve() 
            time.sleep(1)
            global max_deg
            max_deg = 0
            input("Press Enter to start time recording script...")
            start_time = time.time()
            
            # Shared state for thread communication
            monitor_state = {
                "running": True,
                "did_move": False,
                "start_turn_time": 0.0
            }
            
            # Define thread function locally to access scope
            def monitor_lever():
                global max_deg
                while monitor_state["running"]:
                    status = client.get_status()
                    current_deg = status['pos_deg']
                    if not monitor_state["did_move"] and current_deg >= 8.0:
                        monitor_state["start_turn_time"] = time.time()
                        monitor_state["did_move"] = True
                    
                    if current_deg > max_deg:
                        max_deg = current_deg
                        #print(max_deg)
                    time.sleep(0.05)
                        

            # Start monitoring thread
            t = threading.Thread(target=monitor_lever)
            t.start()
            
            client.enable_motor()
            client.start_valve()
            print("Valve active. Monitoring movement...")
            
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
            pos_deg = max_deg
            rotation_progress = pos_deg / 360 # 360 degrees is the max rotation
            
            input("Press Enter to reset valve (ends trial)...")
            # total time
            curr_time = time.time()
            total_time = curr_time - start_time
            # write to CSV (append mode)
            with open("turnwheel_eval_reset.csv", "a", newline="", encoding='utf-8') as file:
                writer = csv.writer(file)
                dict = [i, total_time, time_to_start_turning, total_turning_time, rotation_progress]
                writer.writerow(dict) 
                print(dict)
                i += 1
            
            
            max_deg = 0
            
            time.sleep(1)
            # Loop continues, re-enabling valve at top of loop
        

    finally:
        client.stop_valve()
        client.disconnect()



def main():
    

    try:

        simple_eval_reset()

        print("Program exited safely.")
    except KeyboardInterrupt:
        print("\n\nExamples interrupted by user.")
    except Exception as e:
        print(f"\n\nError: {e}")
        import traceback

        traceback.print_exc()


if __name__ == "__main__":
    main()

