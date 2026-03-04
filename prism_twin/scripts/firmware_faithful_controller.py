from __future__ import annotations

from dataclasses import dataclass
import math

from prism_physics import PrismPhysics


@dataclass
class ControllerRuntimeConfig:
    control_hz: float = 1000.0
    torque_filter_cutoff_hz: float = 400.0
    passivity_energy_cap_j: float = 2.0
    quiet_enter_rad_s: float = math.radians(1.0)
    quiet_exit_rad_s: float = math.radians(2.0)


class FirmwareFaithfulTorqueController:
    def __init__(self, preset_config: dict, runtime: ControllerRuntimeConfig | None = None):
        self.preset_config = dict(preset_config)
        self.runtime = runtime or ControllerRuntimeConfig()
        self.physics = PrismPhysics(self.preset_config)

        self.filtered_torque_nm = 0.0
        self.previous_torque_nm = 0.0
        self.passivity_energy_j = 0.0
        self.quiet_active = False

    def _update_quiet_gate(self, omega_rad_s: float) -> None:
        abs_omega = abs(float(omega_rad_s))
        if self.quiet_active:
            if abs_omega >= self.runtime.quiet_exit_rad_s:
                self.quiet_active = False
        else:
            if abs_omega <= self.runtime.quiet_enter_rad_s:
                self.quiet_active = True

    @staticmethod
    def _clamp(value: float, limit: float) -> float:
        if limit <= 0.0:
            return value
        return max(-limit, min(limit, value))

    @staticmethod
    def _lowpass_simple(input_value: float, previous_output: float, cutoff_freq_hz: float, sample_rate_hz: float) -> float:
        if cutoff_freq_hz <= 0.0 or sample_rate_hz <= 0.0:
            return input_value

        nyquist = sample_rate_hz * 0.5
        cutoff = min(cutoff_freq_hz, nyquist * 0.9)
        dt = 1.0 / sample_rate_hz
        alpha = 2.0 * math.pi * cutoff * dt
        alpha = max(0.0, min(1.0, alpha))
        return alpha * input_value + (1.0 - alpha) * previous_output

    def compute_torque(self, position_deg: float, omega_rad_s: float) -> tuple[float, float]:
        self._update_quiet_gate(omega_rad_s)

        raw_torque_nm = 0.0
        if not self.quiet_active:
            raw_torque_nm = self.physics.compute_torque(position_deg, omega_rad_s)

        torque_limit = float(self.preset_config.get("torque_limit_nm", self.preset_config.get("hil_tau_max_limit_nm", 0.0)))
        clamped = self._clamp(raw_torque_nm, torque_limit)

        filtered = self._lowpass_simple(
            input_value=clamped,
            previous_output=self.filtered_torque_nm,
            cutoff_freq_hz=self.runtime.torque_filter_cutoff_hz,
            sample_rate_hz=self.runtime.control_hz,
        )

        dt_s = 1.0 / self.runtime.control_hz
        power_w = filtered * omega_rad_s
        delta_energy = power_w * dt_s

        if power_w <= 0.0:
            self.passivity_energy_j += delta_energy
            min_energy = -abs(self.runtime.passivity_energy_cap_j)
            if self.passivity_energy_j < min_energy:
                self.passivity_energy_j = min_energy
        else:
            available_energy = -self.passivity_energy_j
            max_allowed_power = available_energy / dt_s if dt_s > 0.0 else 0.0
            if max_allowed_power <= 0.0:
                filtered = 0.0
            elif power_w > max_allowed_power and abs(omega_rad_s) > 1e-9:
                filtered = max_allowed_power / omega_rad_s

            delta_energy = filtered * omega_rad_s * dt_s
            self.passivity_energy_j += delta_energy

        if self.passivity_energy_j > 0.0:
            self.passivity_energy_j = 0.0

        self.filtered_torque_nm = filtered
        self.previous_torque_nm = filtered
        return filtered, raw_torque_nm
