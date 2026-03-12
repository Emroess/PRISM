import socket
import threading
import json
import time
import math
from typing import Dict, Any

# ===================== ISAAC SIM SETUP =====================
from omni.isaac.kit import SimulationApp
simulation_app = SimulationApp({"headless": False})  # Set True for no GUI / server mode

from omni.isaac.core import World
from omni.isaac.core.articulations import Articulation
from omni.isaac.core.utils.prims import get_prim_at_path

# ===================== CONFIG (EDIT THESE) =====================
ARTICULATION_PRIM_PATH = "/World/PRISM_Articulation"   # ← YOUR SCENE'S ARTICULATION PATH
JOINT_INDEX = 0                                        # ← Which rotational DOF (0-based)
STM32_IP = "10.0.1.15"                                 # ← Firmware IP address
TCP_PORT = 8889                                        # ← Firmware HITL port
# ============================================================

world = World(stage_units_in_meters=1.0)
world.reset()

# Load your articulation
print(f"🔧 Loading articulation at {ARTICULATION_PRIM_PATH}")
articulation_prim = get_prim_at_path(ARTICULATION_PRIM_PATH)
if not articulation_prim:
    raise RuntimeError(f"❌ Articulation prim NOT FOUND at {ARTICULATION_PRIM_PATH}")

articulation = Articulation(prim_path=ARTICULATION_PRIM_PATH)
world.scene.add(articulation)
articulation.initialize()

print(f"✅ Articulation loaded — controlling joint index {JOINT_INDEX}")

# Thread-safe state arrays 
latest_command: Dict[str, Any] = {"torque_nm": 0.0, "seq": 0, "t_us": 0}
telemetry_to_send: Dict[str, Any] = {"pos": 0.0, "vel": 0.0, "fresh": False}
state_lock = threading.Lock()

def tcp_client_thread():
    """Background thread that securely talks to the STM32"""
    while simulation_app.is_running():
        try:
            client = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            client.settimeout(5.0)  # Connect timeout
            try:
                print(f"🔄 Connecting to STM32 at {STM32_IP}:{TCP_PORT}...")
                client.connect((STM32_IP, TCP_PORT))
                client.settimeout(0.1)  # Fast polling timeout after connect
                print("✅ Connected to STM32 HITL server")
            except Exception as e:
                print(f"⚠️ Connection failed: {e}. Retrying in 2s...")
                time.sleep(2.0)
                continue
            
            buf = b""
            while simulation_app.is_running():
                # 1. READ torque frames (handled in stream chunks)
                try:
                    chunk = client.recv(4096)
                    if not chunk:
                        print("❌ STM32 disconnected (empty socket read)")
                        break
                    buf += chunk
                except socket.timeout:
                    pass  # normal - no new data
                except Exception as e:
                    print(f"❌ Read error: {e}")
                    break
                
                time.sleep(0.001)  # Add a tiny sleep to reduce CPU usage
                
                # Parse complete newline-deliminated JSON lines
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    line_str = line.strip().decode('utf-8', errors='ignore')
                    if not line_str or 'hitl' in line_str:
                        continue  # Skip handshake frame
                    
                    try:
                        cmd = json.loads(line_str)
                        with state_lock:
                            latest_command["torque_nm"] = float(cmd.get("torque_nm", 0.0))
                            latest_command["seq"] = int(cmd.get("seq", 0))
                            latest_command["t_us"] = int(cmd.get("t_us", 0))
                    except json.JSONDecodeError:
                        pass # Ignore corrupted chunk
                
                # 2. SEND telemetry frames (if main thread updated them)
                with state_lock:
                    if telemetry_to_send["fresh"]:
                        pos = telemetry_to_send["pos"]
                        vel = telemetry_to_send["vel"]
                        telemetry_to_send["fresh"] = False
                        
                        reply = {"pos": pos, "vel": vel}
                        reply_str = json.dumps(reply) + "\n"
                        try:
                            client.settimeout(0.2)  # temporary send timeout
                            client.sendall(reply_str.encode('utf-8'))
                            client.settimeout(0.1)  # reset back
                        except socket.timeout:
                            print("⚠️ Send timeout - STM32 not reading?")
                        except Exception as e:
                            print(f"❌ Send error: {e}")
                            break
                            
        except Exception as e:
            print(f"⚠️ Socket loop failed: {e}")
            time.sleep(2.0)
            
        finally:
            try:
                client.close()
            except:
                pass

# ===================== START TCP CLIENT =====================
threading.Thread(target=tcp_client_thread, daemon=True).start()

# ===================== MAIN PHYSICS LOOP =====================
print("Isaac Sim + PRISM HITL running — torque control + telemetry loop active")
print("   Make sure firmware is running with: hitl enable")

last_print = time.time()
tick = 0

while simulation_app.is_running():
    # Advance Physics
    world.step(render=True)

    # 1. APPLY Torque to the Isaac Joint
    with state_lock:
        torque_nm = latest_command["torque_nm"]
        seq = latest_command["seq"]

    efforts = [0.0] * articulation.num_joints
    efforts[JOINT_INDEX] = torque_nm
    articulation.set_joint_efforts(efforts)

    # 2. READ Telemetry (MUST be on main thread for Omniverse safety)
    # Isaac Sim joint positions are in radians. Firmware expects degrees.
    pos_rad = float(articulation.get_joint_positions()[JOINT_INDEX])
    vel_rads = float(articulation.get_joint_velocities()[JOINT_INDEX])
    pos_deg = math.degrees(pos_rad)
    
    with state_lock:
        telemetry_to_send["pos"] = pos_deg
        telemetry_to_send["vel"] = vel_rads
        telemetry_to_send["fresh"] = True

    # 3. CONSOLE LOGGING (throttle to 1Hz)
    tick += 1
    now = time.time()
    if now - last_print > 1.0:
        print(f"↻ seq={seq:<8} τ={torque_nm:>7.3f} Nm  |  θ={pos_deg:>7.2f}°  ω={vel_rads:>7.3f} rad/s")
        last_print = now

simulation_app.close()
print("Isaac Sim HITL shutdown")