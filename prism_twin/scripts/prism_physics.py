import numpy as np
import json
from pathlib import Path

class PrismPhysics:
    def __init__(self, config):
        """
        config: dict containing HIL physics parameters
        {
            'hil_b_viscous_nm_s_per_rad': float,
            'hil_tau_c_coulomb_nm': float,
            'hil_k_w_wall_stiffness_nm_per_turn': float,
            'hil_c_w_wall_damping_nm_s_per_turn': float,
            'hil_eps_smoothing': float,
            'hil_tau_max_limit_nm': float,
            'closed_position_deg': float,
            'open_position_deg': float,
            'degrees_per_turn': float
        }
        """
        self.config = config
        self.TWO_PI = 2 * np.pi

    def smooth_sign(self, omega, eps):
        """sgn_ε(ω) = ω / sqrt(ω² + ε²)"""
        return omega / np.sqrt(omega**2 + eps**2)

    def compute_wall_penetration(self, theta_turns, theta_off, theta_on):
        """
        p = min(0, θ - θ_off) + max(0, θ - θ_on)
        Negative on left, positive on right, zero inside window
        """
        left_penetration = min(0.0, theta_turns - theta_off)
        right_penetration = max(0.0, theta_turns - theta_on)
        return left_penetration + right_penetration

    def compute_wall_torque(self, theta_turns, omega_turns_per_s):
        """
        τ_wall = -k_w * p - c_w * ṗ
        """
        theta_off = self.config['closed_position_deg'] / self.config['degrees_per_turn']
        theta_on = self.config['open_position_deg'] / self.config['degrees_per_turn']
        kw = self.config['hil_k_w_wall_stiffness_nm_per_turn']
        cw = self.config['hil_c_w_wall_damping_nm_s_per_turn']

        penetration = self.compute_wall_penetration(theta_turns, theta_off, theta_on)

        if abs(penetration) < 1e-6:
            return 0.0

        stiffness_torque = -kw * penetration
        # p_dot ~= omega when moving into/out of wall
        penetration_velocity = omega_turns_per_s if penetration != 0.0 else 0.0
        damping_torque = -cw * penetration_velocity

        return stiffness_torque + damping_torque

    def compute_torque(self, theta_deg, omega_rad_s):
        """
        τ(θ,ω) = -b*ω - τ_c*sgn_ε(ω) + τ_wall(θ,ω)
        """
        b = self.config['hil_b_viscous_nm_s_per_rad']
        tau_c = self.config['hil_tau_c_coulomb_nm']
        eps = self.config['hil_eps_smoothing']
        max_torque = self.config['hil_tau_max_limit_nm']
        deg_per_turn = self.config['degrees_per_turn']

        theta_turns = theta_deg / deg_per_turn
        omega_turns_per_s = omega_rad_s / (2 * np.pi)

        # Viscous damping
        viscous_torque = -b * omega_rad_s

        # Coulomb friction
        friction_torque = -tau_c * self.smooth_sign(omega_rad_s, eps)

        # Wall torque
        wall_torque = self.compute_wall_torque(theta_turns, omega_turns_per_s)

        total_torque = viscous_torque + friction_torque + wall_torque

        # Clamp torque
        return np.clip(total_torque, -max_torque, max_torque)

def _load_generated_firmware_presets() -> dict[str, dict]:
    config_path = Path(__file__).resolve().parents[1] / "config" / "generated_firmware_presets.json"
    if not config_path.exists():
        return {}

    with config_path.open("r", encoding="utf-8") as file:
        payload = json.load(file)
    return payload.get("twin_hil_config", {})


def _fallback_presets() -> dict[str, dict]:
    return {
        "LIGHT": {
            "closed_position_deg": 0.0,
            "open_position_deg": 90.0,
            "degrees_per_turn": 360.0,
            "hil_b_viscous_nm_s_per_rad": 0.02,
            "hil_tau_c_coulomb_nm": 0.06,
            "hil_k_w_wall_stiffness_nm_per_turn": 10.0,
            "hil_c_w_wall_damping_nm_s_per_turn": 0.1,
            "hil_eps_smoothing": 0.001,
            "hil_tau_max_limit_nm": 8.0,
            "torque_limit_nm": 8.0,
        },
        "MEDIUM": {
            "closed_position_deg": 0.0,
            "open_position_deg": 90.0,
            "degrees_per_turn": 360.0,
            "hil_b_viscous_nm_s_per_rad": 0.04,
            "hil_tau_c_coulomb_nm": 0.12,
            "hil_k_w_wall_stiffness_nm_per_turn": 15.0,
            "hil_c_w_wall_damping_nm_s_per_turn": 0.2,
            "hil_eps_smoothing": 0.001,
            "hil_tau_max_limit_nm": 8.0,
            "torque_limit_nm": 8.0,
        },
        "HEAVY": {
            "closed_position_deg": 0.0,
            "open_position_deg": 360.0,
            "degrees_per_turn": 360.0,
            "hil_b_viscous_nm_s_per_rad": 0.08,
            "hil_tau_c_coulomb_nm": 0.25,
            "hil_k_w_wall_stiffness_nm_per_turn": 25.0,
            "hil_c_w_wall_damping_nm_s_per_turn": 0.4,
            "hil_eps_smoothing": 0.001,
            "hil_tau_max_limit_nm": 6.0,
            "torque_limit_nm": 6.0,
        },
        "INDUSTRIAL": {
            "closed_position_deg": 0.0,
            "open_position_deg": 360.0,
            "degrees_per_turn": 360.0,
            "hil_b_viscous_nm_s_per_rad": 0.12,
            "hil_tau_c_coulomb_nm": 0.4,
            "hil_k_w_wall_stiffness_nm_per_turn": 35.0,
            "hil_c_w_wall_damping_nm_s_per_turn": 0.6,
            "hil_eps_smoothing": 0.001,
            "hil_tau_max_limit_nm": 8.0,
            "torque_limit_nm": 8.0,
        },
    }


PRESETS = _load_generated_firmware_presets() or _fallback_presets()
