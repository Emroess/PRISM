import socket
import json
import time
import math
from omni.isaac.core.articulations import Articulation

class PrismHitlController:
    """
    Hardware-In-The-Loop (HITL) controller for integrating PRISM STM32 
    firmware into an existing Isaac Sim environment.

    This version runs entirely on the main Isaac Sim physics thread
    using non-blocking sockets. No background threads are spawned.

    ---------------------------------------------------------------------------
    ISAAC SIM SETUP INSTRUCTIONS:
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
       - Create an Xform (Create > Xform) named "Robot_Root_Xform".
       - Drag your Stator and Rotor meshes INSIDE this Xform.
       - Right-click the Xform > Add > Physics > Articulation Root.

    ---------------------------------------------------------------------------
    SCRIPT USAGE IN YOUR MAIN ISAAC LOOP:
    ---------------------------------------------------------------------------
        # 1. Pass the Articulation Root Xform path
        my_robot = Articulation(prim_path="/World/Robot_Root_Xform")
        
        # 2. Attach the HITL controller
        hitl = PrismHitlController(my_robot, joint_index=0, stm32_ip="10.0.1.15")

        # 3. Inside your physics tick loop:
        while simulation_app.is_running():
            world.step(render=True)
            
            # This handles receiving STM32 torque, applying it to Joint 0,
            # and sending the new rad/sec velocities back to the STM32.
            hitl.step()  
    """
    def __init__(self, articulation: Articulation, joint_index: int, stm32_ip: str, tcp_port: int = 8889):
        self.articulation = articulation
        self.joint_index = joint_index
        self.stm32_ip = stm32_ip
        self.tcp_port = tcp_port
        
        self.client = None
        self._buf = b""
        
        # State
        self.torque_nm = 0.0
        self.seq = 0
        self.t_us = 0
        
        # Logging throttle
        self._last_print = time.time()
        self._tick = 0

    def _connect(self):
        """Attempts a non-blocking connection to the STM32."""
        try:
            sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            sock.settimeout(0.5)  # Brief timeout for the initial connection attempt
            sock.connect((self.stm32_ip, self.tcp_port))
            
            # Switch to pure non-blocking mode for the main loop
            sock.setblocking(False)
            self.client = sock
            self._buf = b""
            print(f"✅ Connected to STM32 HITL server at {self.stm32_ip}:{self.tcp_port}")
        except Exception as e:
            # Silently fail and retry later so we don't spam the GUI console
            pass

    def step(self):
        """
        MUST be called inside the main Isaac Sim physics loop after world.step().
        """
        # 1. Manage connection
        if self.client is None:
            now = time.time()
            if now - self._last_print > 2.0:
                print(f"🔄 Looking for STM32 at {self.stm32_ip}:{self.tcp_port}...")
                self._last_print = now
            self._connect()
            return

        # 2. READ torque from STM32 (Non-blocking)
        try:
            chunk = self.client.recv(4096)
            if not chunk:
                # Empty read on a connected socket means the remote closed it
                print("❌ STM32 disconnected")
                self.client.close()
                self.client = None
                return
            self._buf += chunk
        except BlockingIOError:
            # Normal: no data available right now
            pass
        except Exception as e:
            print(f"❌ Socket read error: {e}")
            self.client.close()
            self.client = None
            return

        # Parse complete lines
        while b"\n" in self._buf:
            line, self._buf = self._buf.split(b"\n", 1)
            line_str = line.strip().decode('utf-8', errors='ignore')
            
            if not line_str or 'hitl' in line_str:
                continue
            try:
                cmd = json.loads(line_str)
                self.torque_nm = float(cmd.get("torque_nm", 0.0))
                self.seq = int(cmd.get("seq", 0))
                self.t_us = int(cmd.get("t_us", 0))
            except json.JSONDecodeError:
                pass

        # 3. APPLY Torque to the Isaac Joint
        efforts = [0.0] * self.articulation.num_joints
        efforts[self.joint_index] = self.torque_nm
        self.articulation.set_joint_efforts(efforts)

        # 4. READ Telemetry & SEND to STM32
        # Isaac Sim uses radians, Firmware uses degrees
        pos_rad = float(self.articulation.get_joint_positions()[self.joint_index])
        vel_rads = float(self.articulation.get_joint_velocities()[self.joint_index])
        pos_deg = math.degrees(pos_rad)

        reply = {"pos": pos_deg, "vel": vel_rads}
        reply_str = json.dumps(reply) + "\n"
        
        try:
            self.client.sendall(reply_str.encode('utf-8'))
        except BlockingIOError:
            pass # Socket buffer full, drop this frame
        except Exception:
            self.client.close()
            self.client = None

        # 5. Console heartbeat (1Hz)
        self._tick += 1
        now = time.time()
        if now - self._last_print > 1.0:
            print(f"↻ seq={self.seq:<8} τ={self.torque_nm:>7.3f} Nm  |  θ={pos_deg:>7.2f}°  ω={vel_rads:>7.3f} rad/s")
            self._last_print = now

    def close(self):
        """Cleanly close the socket"""
        if self.client:
            self.client.close()
            self.client = None