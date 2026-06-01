# 1. Start the Isaac Sim environment (must be before other omni imports)
from isaacsim import SimulationApp
simulation_app = SimulationApp({"headless": False})

import socket
import struct
import numpy as np
from isaacsim.core.api import World
from isaacsim.core.prims import Articulation
from pxr import UsdGeom, UsdPhysics, Sdf, Gf
import isaacsim.core.utils.stage as stage_utils

# --- Configuration ---
UDP_IP = "0.0.0.0"       # Listen on all available interfaces
UDP_PORT = 5005          # Port to listen on (ensure your hardware sends here)
STAGE_PATH = "/home/uw/Documents/isaac_sim_files/Andrew_test_rotation.usd"
ARTICULATION_PATH = "/World/Joint_Xform" # Path to the parent of your RevoluteJoint

# --- UDP Socket Setup ---
# We use UDP (SOCK_DGRAM) and set it to non-blocking so the simulation 
# doesn't pause while waiting for a network packet.
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((UDP_IP, UDP_PORT))
sock.setblocking(False) # MUST BE FALSE so the physics loop doesn't freeze! 

# --- Simulation Setup ---
# Open the pre-made stage before initializing the World
if STAGE_PATH and STAGE_PATH != "/path/to/your/premade_stage.usd":
    stage_utils.open_stage(STAGE_PATH)
    simulation_app.update()
else:
    print(f"WARNING: STAGE_PATH not updated! Make sure to set STAGE_PATH if you want to load a pre-made stage.")

# World automatically attaches to the default prim (e.g. /World) and respects other prims at the same level (e.g. /Environment)
world = World()

# To apply torques in Isaac Sim 5.X+, we use the Articulation class from isaacsim.core.prims.
# This class acts as a view, even for a single robot.
prism_shaft = Articulation(prim_paths_expr=ARTICULATION_PATH, name="prism_shaft")
world.scene.add(prism_shaft)

world.reset()
print(f"Num dofs: {prism_shaft.num_dof}")
if prism_shaft.num_dof == 0:
    print("WARNING: Articulation has 0 Degrees of Freedom! Torques will not work.")

print(f"Listening for hardware torque commands on port {UDP_PORT}...")

# Keep track of the last received torque to constantly apply it
last_torque = 0.0

# --- Main HiL Simulation Loop ---
while simulation_app.is_running():
    # 1. Step the physics and rendering
    world.step(render=True)
    
    # 2. Check for new incoming hardware commands
    try:
        # Buffer size of 1024 bytes. 
        data, addr = sock.recvfrom(1024)
        
        # Unpack the data. Assuming the hardware sends a standard 4-byte float (Nm)
        # The '<f' denotes little-endian float. Adjust based on your hardware's struct packing.
        torque_command = struct.unpack('<f', data)[0] 
        last_torque = torque_command
        
    except BlockingIOError:
        # This exception is thrown if no data is currently in the socket buffer.
        # We just pass and let the simulation continue using `last_torque`.
        last_torque = 0
    except Exception as e:
        print(f"Network or parsing error: {e}")

    # 3. Apply the torque continuously, BUT only if physics view is initialized!
    # Checking world.is_playing() avoids the "Physics Simulation View is not created yet" warning.
    if world.is_playing():
        # The new Articulation (view-based) expects a 2D array: (num_robots, num_dof)
        prism_shaft.set_joint_efforts(np.array([[last_torque]]))

    # If headless was set to True, you may want to explicitly call world.play()
    # But for headless=False, you can hit the Play button in the Isaac Sim UI!

# Cleanup when the window is closed
sock.close()
simulation_app.close()