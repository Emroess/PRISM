"""
hitl_client.py - PRISM Hardware-in-the-Loop Isaac Sim motor clone

Connects to the STM32 firmware HITL TCP server on port 8889 and:
  1. Receives torque commands  {"torque_nm":<f>, "seq":<u32>, "t_us":<u64>}
  2. Integrates a virtual motor model (Newton's 2nd law for rotation):
         α  = (τ - b·ω) / J      (angular acceleration, rad/s²)
         ω += α · dt             (velocity, rad/s)
         θ += ω · dt             (position, deg)
  3. Sends back simulated encoder data {"pos":<deg>, "vel":<rad_s>}

Why is inertia (J) needed?
  Newton's 2nd law for rotation is τ = J·α, so α = τ/J.
  Without J we cannot convert a torque (N·m) into an acceleration (rad/s²)
  and therefore cannot integrate to velocity or position.  The default of
  J=0.001 kg·m² is a reasonable ballpark for a small brushless motor +
  gearbox + handle; tune it to match your physical system.

Usage (standalone, no Isaac Sim required):
  python hitl_client.py --ip 192.168.1.100
  python hitl_client.py --ip 192.168.1.100 --inertia 0.002 --damping 0.02
  python hitl_client.py --ip 192.168.1.100 --log hitl_data.csv

Isaac Sim usage (import as module):
  from hitl_client import PRISMHITLClient
  client = PRISMHITLClient(stm32_ip="192.168.1.100")
  await client.run_async()              # from SimulationApp step callback

Requires: Python 3.8+, no external dependencies beyond the stdlib.
"""

from __future__ import annotations

import argparse
import asyncio
import csv
import json
import math
import socket
import sys
import time
from typing import Optional


# ---------------------------------------------------------------------------
# HITL protocol constants
# ---------------------------------------------------------------------------
HITL_PORT: int = 8889
HITL_RECV_TIMEOUT_S: float = 0.1  # max seconds to wait for a torque packet

# ---------------------------------------------------------------------------
# PRISMHITLClient
# ---------------------------------------------------------------------------

class PRISMHITLClient:
    """
    Thin Python motor integrator for PRISM HITL simulation.

    Physics model for the simulated revolute joint:
        α  = (τ_drive - b·ω) / J
        ω += α · dt          (rad/s)
        θ += ω · dt          (degrees)

    Parameters
    ----------
    stm32_ip : str
        IP address of the STM32 running the HITL firmware.
    port : int
        HITL TCP port (default: 8889 — matches HITL_PORT in firmware).
    inertia_kg_m2 : float
        Moment of inertia J of the simulated motor + gearbox + handle assembly
        (kg·m²).  Default: 0.001 kg·m².  Set this to match your hardware for
        realistic velocity dynamics.  If you don't know the exact value, start
        with the default and compare the simulated and measured position traces.
    damping : float
        Viscous damping coefficient b (N·m·s/rad) of the virtual bearing.
        This represents rotor + bearing friction NOT captured by the firmware
        physics model.  Default: 0.005 N·m·s/rad (typically very small).
    pos_limit_deg : float
        Soft position limits ±pos_limit_deg (degrees).  If the simulated joint
        hits a limit the velocity is reflected (elastic collision). Default: 360.
    log_path : Optional[str]
        If set, log every sample to a CSV file (timestamp, seq, torque, pos, vel).
    """

    def __init__(
        self,
        stm32_ip: str,
        port: int = HITL_PORT,
        inertia_kg_m2: float = 0.001,
        damping: float = 0.005,
        pos_limit_deg: float = 360.0,
        log_path: Optional[str] = None,
    ) -> None:
        self.stm32_ip = stm32_ip
        self.port = port
        self.inertia_kg_m2 = inertia_kg_m2
        self.damping = damping
        self.pos_limit_deg = pos_limit_deg
        self.log_path = log_path

        # Simulation state
        self._pos_deg: float = 0.0
        self._vel_rad_s: float = 0.0
        self._last_torque: float = 0.0

        # Timing
        self._last_t_us: int = 0
        self._samples: int = 0
        self._errors: int = 0

        # Stats
        self._t_start = time.monotonic()

    # ------------------------------------------------------------------
    # Physics integrator
    # ------------------------------------------------------------------

    def _step(self, torque_nm: float, dt_s: float) -> None:
        """Euler integration of the single-DOF rotational model."""
        if dt_s <= 0.0 or dt_s > 0.1:
            # Reject unreasonable time steps (first packet or large gap)
            return

        # α = (τ - b·ω) / J
        alpha = (torque_nm - self.damping * self._vel_rad_s) / self.inertia_kg_m2

        # Integrate velocity and position
        self._vel_rad_s += alpha * dt_s
        self._pos_deg   += math.degrees(self._vel_rad_s * dt_s)

        # Soft position limits — elastic reflection
        if abs(self._pos_deg) > self.pos_limit_deg:
            self._pos_deg   = math.copysign(self.pos_limit_deg, self._pos_deg)
            self._vel_rad_s = -self._vel_rad_s * 0.5  # damp on bounce

    # ------------------------------------------------------------------
    # JSON helpers
    # ------------------------------------------------------------------

    @staticmethod
    def _parse_torque_frame(line: str) -> Optional[tuple[float, int, int]]:
        """
        Parse a torque frame JSON line from the firmware.

        Expected format:
            {"torque_nm":<float>,"seq":<int>,"t_us":<int>}

        Returns: (torque_nm, seq, t_us_accum) or None on parse error.
        """
        try:
            obj = json.loads(line)
            return float(obj["torque_nm"]), int(obj["seq"]), int(obj["t_us"])
        except (json.JSONDecodeError, KeyError, ValueError, TypeError):
            return None

    def _make_encoder_frame(self) -> bytes:
        """Build the encoder JSON line to send back to the firmware."""
        msg = (
            f'{{"pos":{self._pos_deg:.4f},"vel":{self._vel_rad_s:.6f}}}\n'
        )
        return msg.encode("ascii")

    # ------------------------------------------------------------------
    # Blocking run loop (standalone use)
    # ------------------------------------------------------------------

    def run(self) -> None:
        """
        Blocking run loop.  Connects to the STM32, processes torque frames,
        and sends back encoder data until interrupted.
        """
        log_file = None
        log_writer = None
        if self.log_path:
            log_file = open(self.log_path, "w", newline="")
            log_writer = csv.writer(log_file)
            log_writer.writerow(
                ["wall_time_s", "seq", "t_us", "torque_nm", "pos_deg", "vel_rad_s"]
            )

        try:
            print(f"[HITL] Connecting to {self.stm32_ip}:{self.port} …")
            with socket.create_connection((self.stm32_ip, self.port), timeout=10.0) as sock:
                sock.settimeout(HITL_RECV_TIMEOUT_S)
                print(f"[HITL] Connected. Inertia J={self.inertia_kg_m2:.4f} kg·m²  "
                      f"damping b={self.damping:.4f} N·m·s/rad")
                print("[HITL] Press Ctrl+C to stop.")

                buf = b""
                while True:
                    # Receive bytes from firmware
                    try:
                        chunk = sock.recv(256)
                    except socket.timeout:
                        print("[HITL] Warning: torque packet timeout — is the valve running?",
                              flush=True)
                        continue

                    if not chunk:
                        print("[HITL] Connection closed by firmware.")
                        break

                    buf += chunk

                    # Process complete lines
                    while b"\n" in buf:
                        line_bytes, buf = buf.split(b"\n", 1)
                        line = line_bytes.strip().decode("ascii", errors="ignore")
                        if not line or line.startswith("{\"hitl\""):
                            # Skip the hello frame
                            continue

                        result = self._parse_torque_frame(line)
                        if result is None:
                            self._errors += 1
                            continue

                        torque_nm, seq, t_us = result

                        # Compute Δt from firmware monotonic timestamp
                        if self._last_t_us > 0:
                            dt_s = (t_us - self._last_t_us) * 1e-6
                        else:
                            dt_s = 0.001  # 1 ms default for first packet
                        self._last_t_us = t_us

                        # Integrate the virtual motor
                        self._step(torque_nm, dt_s)
                        self._samples += 1
                        self._last_torque = torque_nm

                        # Send encoder feedback
                        enc_bytes = self._make_encoder_frame()
                        try:
                            sock.sendall(enc_bytes)
                        except OSError as e:
                            print(f"[HITL] Send error: {e}")
                            break

                        # Log data
                        if log_writer:
                            log_writer.writerow([
                                f"{time.monotonic() - self._t_start:.6f}",
                                seq, t_us,
                                f"{torque_nm:.6f}",
                                f"{self._pos_deg:.4f}",
                                f"{self._vel_rad_s:.6f}",
                            ])

                        # Console heartbeat every ~200 ms (estimated)
                        if self._samples % 200 == 0:
                            print(
                                f"[HITL] seq={seq:8d}  "
                                f"τ={torque_nm:+7.4f} N·m  "
                                f"θ={self._pos_deg:+8.3f}°  "
                                f"ω={self._vel_rad_s:+8.4f} rad/s  "
                                f"errors={self._errors}",
                                flush=True,
                            )

        except ConnectionRefusedError:
            print(f"[HITL] Error: Connection refused. Is the STM32 running HITL firmware "
                  f"with HITL mode enabled?")
            print( "       Enable with: curl -X POST http://<ip>:8080/api/v1/hitl "
                  "-H 'X-API-Key: steve-valve-2025' "
                  "-H 'Content-Type: application/json' -d '{\"enabled\":true}'")
        except KeyboardInterrupt:
            print(f"\n[HITL] Stopped. Sent {self._samples} encoder frames, {self._errors} errors.")
        finally:
            if log_file:
                log_file.close()
                print(f"[HITL] Log saved to {self.log_path}")

    # ------------------------------------------------------------------
    # Async run loop (Isaac Sim embedded use)
    # ------------------------------------------------------------------

    async def run_async(self) -> None:
        """
        Async coroutine for embedding in Isaac Sim's simulation loop.

        Example (inside a SimulationApp step callback):
            import asyncio
            from hitl_client import PRISMHITLClient
            client = PRISMHITLClient(stm32_ip="192.168.1.100")
            asyncio.ensure_future(client.run_async())

        Isaac Sim step callbacks are called synchronously, so schedule this
        coroutine once at startup and let the asyncio event loop drive it.
        """
        reader, writer = await asyncio.open_connection(self.stm32_ip, self.port)
        print(f"[HITL] async connected to {self.stm32_ip}:{self.port}")

        buf = b""
        try:
            while True:
                chunk = await asyncio.wait_for(reader.read(256), timeout=HITL_RECV_TIMEOUT_S)
                if not chunk:
                    break
                buf += chunk

                while b"\n" in buf:
                    line_bytes, buf = buf.split(b"\n", 1)
                    line = line_bytes.strip().decode("ascii", errors="ignore")
                    if not line or "hitl" in line:
                        continue

                    result = self._parse_torque_frame(line)
                    if result is None:
                        self._errors += 1
                        continue

                    torque_nm, _seq, t_us = result

                    dt_s = (t_us - self._last_t_us) * 1e-6 if self._last_t_us > 0 else 0.001
                    self._last_t_us = t_us

                    self._step(torque_nm, dt_s)
                    self._samples += 1

                    enc = self._make_encoder_frame()
                    writer.write(enc)
                    await writer.drain()

        except (asyncio.TimeoutError, asyncio.IncompleteReadError, ConnectionResetError):
            print("[HITL] async: connection lost")
        finally:
            writer.close()

    # ------------------------------------------------------------------
    # Isaac Sim joint state accessors (for USD stage sync)
    # ------------------------------------------------------------------

    @property
    def position_deg(self) -> float:
        """Current simulated joint position (degrees)."""
        return self._pos_deg

    @property
    def velocity_rad_s(self) -> float:
        """Current simulated joint velocity (rad/s)."""
        return self._vel_rad_s

    def reset(self, pos_deg: float = 0.0, vel_rad_s: float = 0.0) -> None:
        """Reset simulation state (e.g., on USD stage load)."""
        self._pos_deg = pos_deg
        self._vel_rad_s = vel_rad_s
        self._last_t_us = 0
        self._samples = 0
        self._errors = 0


# ---------------------------------------------------------------------------
# CLI entry point (standalone use)
# ---------------------------------------------------------------------------

def _parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(
        description="PRISM HITL client — Isaac Sim virtual motor integrator"
    )
    p.add_argument(
        "--ip", required=True,
        help="IP address of the STM32 PRISM device"
    )
    p.add_argument(
        "--port", type=int, default=HITL_PORT,
        help=f"HITL TCP port (default: {HITL_PORT})"
    )
    p.add_argument(
        "--inertia", type=float, default=0.001,
        metavar="J_kg_m2",
        help=(
            "Moment of inertia of motor+gearbox+handle assembly (kg·m²). "
            "Default: 0.001. This determines how quickly the simulated motor "
            "accelerates under a given torque: α = τ/J. Increase for a heavier "
            "load, decrease for a lighter one."
        )
    )
    p.add_argument(
        "--damping", type=float, default=0.005,
        metavar="b_Nm_s_per_rad",
        help="Virtual bearing viscous damping (N·m·s/rad). Default: 0.005."
    )
    p.add_argument(
        "--pos-limit", type=float, default=360.0,
        metavar="DEG",
        help="Soft position limit ±deg (degrees). Default: 360."
    )
    p.add_argument(
        "--log", metavar="FILE",
        help="CSV log file path (timestamp, seq, t_us, torque_nm, pos_deg, vel_rad_s)"
    )
    return p.parse_args()


def main() -> None:
    args = _parse_args()
    client = PRISMHITLClient(
        stm32_ip=args.ip,
        port=args.port,
        inertia_kg_m2=args.inertia,
        damping=args.damping,
        pos_limit_deg=args.pos_limit,
        log_path=args.log,
    )

    print("=" * 60)
    print("PRISM Isaac Sim HITL Client")
    print("=" * 60)
    print(f"  STM32 IP:    {args.ip}:{args.port}")
    print(f"  Inertia J:   {args.inertia:.4f} kg·m²")
    print(f"  Damping b:   {args.damping:.4f} N·m·s/rad")
    print(f"  Pos limit:   ±{args.pos_limit:.1f}°")
    if args.log:
        print(f"  Log file:    {args.log}")
    print()
    print("Before connecting, enable HITL mode on the firmware:")
    print(
        f"  curl -X POST http://{args.ip}:8080/api/v1/hitl "
        "-H 'X-API-Key: steve-valve-2025' "
        "-H 'Content-Type: application/json' -d '{\"enabled\": true}'"
    )
    print()

    client.run()


if __name__ == "__main__":
    main()
