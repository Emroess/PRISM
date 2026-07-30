import socket
import json
import math
import asyncio
import omni.kit.app
import omni.timeline
from isaacsim.core.prims import SingleArticulation

"""
---------------------------------------------------------------------------
PRISM Hardware-In-The-Loop (HITL) Controller for Isaac Sim GUI
---------------------------------------------------------------------------
Copy and paste this script directly into the Isaac Sim Script Editor 
(Window > Script Editor) and click "Run". It uses an async loop to hook 
directly into the Isaac Sim physics renderer without freezing the GUI.

ISAAC SIM SCENE SETUP INSTRUCTIONS:
---------------------------------------------------------------------------
To control a rotational DOF via STM32 torque, your scene MUST be configured
with an Articulation Root and a force-driven Revolute Joint:

1. CREATE THE JOINT:
   - Select your Stator Mesh, then Shift-select your Rotor Mesh.
   - Go to Create > Physics > Joints > Revolute Joint.
   - Move the new joint to the exact center axis of rotation.

2. TURN ON TORQUE CONTROL:
   - Select the Revolute Joint.
   - Go to Property window > Add > Physics > Angular Drive.
   - In the new Angular Drive component:
     * Set "Type" to "Force" (this means Torque control).
     * Set "Damping" to 0.0 (The PRISM STM32 handles damping).
     * Set "Stiffness" to 0.0.

3. CREATE THE ARTICULATION ROOT:
   - Create an Xform (Create > Xform) named e.g. "Robot_Root_Xform".
   - Drag your Stator and Rotor meshes INSIDE this Xform.
   - Right-click the Xform > Add > Physics > Articulation Root.
   - Put that Xform path into ARTICULATION_PATH below.
---------------------------------------------------------------------------
"""

# ===================== CONFIG (EDIT THESE) =====================
STM32_IP = "10.0.1.15"
TCP_PORT = 8889
ARTICULATION_PATH = "/Test_World/Robot_Root_Xform"  # Match your Articulation Root
JOINT_INDEX = 0                                # The rotating DOF index
# ===============================================================

async def hitl_background_loop():
    print(f"🚀 Starting PRISM HITL Controller for {ARTICULATION_PATH}")
    
    timeline = omni.timeline.get_timeline_interface()
    robot = None
    sock = None
    buf = b""
    torque_nm = 0.0

    print(f"🔄 Waiting for Play button & STM32 connection at {STM32_IP}:{UDP_PORT}...") # changed from TCP TO UDP
    
    try:
        # Loop forever inside the Isaac Sim GUI
        while True:
            # Yield to Isaac Sim's physics/rendering engine (prevent freezing)
            await omni.kit.app.get_app().next_update_async()

            # Skip physics updates if simulation is stopped/paused
            if not timeline.is_playing():
                robot = None  # Reset so it re-initializes on next 'Play'
                continue

            if robot is None:
                try:
                    # Initialize after Play is pressed (physics context exists)
                    robot = SingleArticulation(prim_path=ARTICULATION_PATH)
                    robot.initialize()
                except Exception:
                    # Context might not be fully ready on the exact first frame
                    robot = None
                    continue

            # 1. Handle Connection
            if sock is None:
                try:
                    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM) # DGRAM is UDP, SOCK_STREAM is TCP, TCP is far more reliable, doesnt lose or misorder packages as much
                    sock.settimeout(0.5)  # Short timeout so GUI doesn't hard-freeze
                    sock.connect((STM32_IP, UDP_PORT)) # CHANGED FROM TCP TO UDP_PORT
                    sock.settimeout(0.0)  # Switch to pure non-blocking for I/O
                    print("✅ Connected to STM32 HITL server")
                    buf = b""
                except Exception:
                    sock = None
                    continue

            # 2. Read Torque from STM32
            try:
                chunk = sock.recv(4096)
                if not chunk:
                    print("❌ STM32 disconnected")
                    sock.close()
                    sock = None
                    continue
                buf += chunk
            except BlockingIOError:
                pass  # Normal: no new data yet
            except Exception:
                sock.close()
                sock = None
                continue

            # Parse JSON stream
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                try:
                    cmd = json.loads(line.decode('utf-8'))
                    if "torque_nm" in cmd:
                        torque_nm = float(cmd["torque_nm"])
                except Exception:
                    pass

            # 3. Apply Torque to Joint
            efforts = [0.0] * robot.num_joints
            efforts[JOINT_INDEX] = torque_nm
            robot.set_joint_efforts(efforts)

            # 4. Stream Telemetry back to STM32
            pos_rad = float(robot.get_joint_positions()[JOINT_INDEX])
            vel_rad_s = float(robot.get_joint_velocities()[JOINT_INDEX])
            
            # Firmware expects degrees
            reply = {"pos": math.degrees(pos_rad), "vel": vel_rad_s}
            
            try:
                msg = json.dumps(reply) + "\n"
                sock.sendall(msg.encode('utf-8'))
            except BlockingIOError:
                pass  # Buffer full, drop frame
            except Exception:
                sock.close()
                sock = None

    except asyncio.CancelledError:
        print("🛑 HITL script stopped by user")
        if sock:
            sock.close()

# If you click "Run" twice, cancel the old loop before starting a new one
if "prism_hitl_task" in globals():
    try:
        if not prism_hitl_task.done():
            prism_hitl_task.cancel()
    except AttributeError:
        try:
            prism_hitl_task.close()
        except AttributeError:
            pass

# Start the async loop inside the GUI
prism_hitl_task = asyncio.ensure_future(hitl_background_loop())