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
UDP_IP = "127.0.0.1"       # Listen on all available interfaces
UDP_PORT = 5005          # Port to listen on (ensure your hardware sends here)
ARTICULATION_PATH = "/World/Joint_Xform" # Path to the parent of your RevoluteJoint

# --- UDP Socket Setup ---
# We use UDP (SOCK_DGRAM) and set it to non-blocking so the simulation 
# doesn't pause while waiting for a network packet.
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((UDP_IP, UDP_PORT))
sock.setblocking(False) # MUST BE FALSE so the physics loop doesn't freeze!
print("CONNECTED TO SOCKET")
# --- Simulation Setup ---
world = World()

stage = stage_utils.get_current_stage()

# Add ground plane so we can actually visually see our objects and lighting
world.scene.add_default_ground_plane()

# --- Build Required Structures ---
# Required Hierarchy: World > Joint_Xform > Base/Cylinder > Joints

# 1. Create Joint_Xform (Articulation Root)
xform_path = ARTICULATION_PATH # /World/Joint_Xform
xform_prim = UsdGeom.Xform.Define(stage, xform_path)
UsdPhysics.ArticulationRootAPI.Apply(xform_prim.GetPrim())

# Move the entire root up so that local poses of children overlap perfectly without snapping constraints!
xform_prim.AddTranslateOp().Set(Gf.Vec3d(0.0, 0.0, 1.0))

# 2. Create Base Anchor (Fixed to world)
base_path = f"{xform_path}/Base"
base = UsdGeom.Cube.Define(stage, base_path)
base.CreateSizeAttr(1.0)
UsdPhysics.RigidBodyAPI.Apply(base.GetPrim())
UsdPhysics.CollisionAPI.Apply(base.GetPrim())

# Fix Base to World
fixed_joint_path = f"{xform_path}/FixedToWorld"
fixed_joint = UsdPhysics.FixedJoint.Define(stage, fixed_joint_path)
fixed_joint.CreateBody0Rel().SetTargets([]) 
fixed_joint.CreateBody1Rel().SetTargets([Sdf.Path(base_path)])

# 3. Create Cylinder (Rigid Body with Mass)
cylinder_path = f"{xform_path}/Cylinder"
cylinder = UsdGeom.Cylinder.Define(stage, cylinder_path)
cylinder.CreateRadiusAttr(0.1)
cylinder.CreateHeightAttr(0.5)
cylinder.CreateAxisAttr("Z") # Cylinder naturally points along Z

# Move the cylinder up so it sits exactly on top of the Base
# Base top is at Z=0.5 (since size is 1.0, centered at 0.0)
# Cylinder center should be Z=0.5 + 0.25 (half height) = 0.75
cylinder.AddTranslateOp().Set(Gf.Vec3d(0.0, 0.0, 0.75))

# Apply Rigid Body API
rigid_api = UsdPhysics.RigidBodyAPI.Apply(cylinder.GetPrim())
rigid_api.CreateRigidBodyEnabledAttr(True)

# Apply Collision API (Crucial for inertia computation)
UsdPhysics.CollisionAPI.Apply(cylinder.GetPrim())

# Apply Mass API
mass_api = UsdPhysics.MassAPI.Apply(cylinder.GetPrim())
mass_api.CreateMassAttr(1.0) # 1.0 kg simulation mass

# 4. Create Revolute Joint connecting Base -> Cylinder
joint_path = f"{xform_path}/RevoluteJoint"
joint = UsdPhysics.RevoluteJoint.Define(stage, joint_path)
joint.CreateAxisAttr("Z") # We want the rotation to be smoothly along Z

# Set local poses so that the bodies maintain their relative offsets
joint.CreateLocalPos0Attr(Gf.Vec3f(0.0, 0.0, 0.75))
joint.CreateLocalPos1Attr(Gf.Vec3f(0.0, 0.0, 0.0))

# Important for RevoluteJoint: The joint must connect two rigid bodies in the tree
joint.CreateBody0Rel().SetTargets([Sdf.Path(base_path)]) 
joint.CreateBody1Rel().SetTargets([Sdf.Path(cylinder_path)])

# 5. Joint Drive (Angular Drive with 0 Stiffness and 0 Damping)
drive = UsdPhysics.DriveAPI.Apply(joint.GetPrim(), "angular")
drive.CreateTypeAttr("force")
drive.CreateStiffnessAttr(0.0)
drive.CreateDampingAttr(0.0)


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
        # We just pass and let the simulation continue using the `last_torque`.
        pass
    except Exception as e:
        print(f"Network or parsing error: {e}")

    # 3. Apply the torque continuously, BUT only if physics view is initialized!
    # Checking world.is_playing() avoids the "Physics Simulation View is not created yet" warning.
    if world.is_playing():
        # The new Articulation (view-based) expects a 2D array: (num_robots, num_dof)
        prism_shaft.set_joint_efforts(np.array([[last_torque]]))
    else:
        # If the GUI is open and paused, we can wait until the user hits Play
        # or we implicitly call play ourselves if headless.
        pass

# Cleanup when the window is closed
sock.close()
simulation_app.close()