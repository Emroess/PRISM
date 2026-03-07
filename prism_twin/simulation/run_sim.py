import mujoco
import mujoco.viewer
import numpy as np
import time
from pathlib import Path

from scripts.prism_physics import PrismPhysics, PRESETS
from scripts.firmware_faithful_controller import (
    FirmwareFaithfulTorqueController,
    ControllerRuntimeConfig,
)
from simulation.nonidealities import (
    ActuatorNonidealityConfig,
    SensorNonidealityConfig,
    DelayLine,
    quantize,
    first_order_lag_step,
    slew_rate_limit_step,
)

class PrismSim:
    def __init__(
        self,
        model_path,
        preset_name='HEAVY',
        instance_id='prism_01',
        target_kind='sim',
        control_hz=1000.0,
        actuator_nonidealities: ActuatorNonidealityConfig | None = None,
        sensor_nonidealities: SensorNonidealityConfig | None = None,
    ):
        self.model = mujoco.MjModel.from_xml_path(model_path)
        self.data = mujoco.MjData(self.model)
        self.physics = PrismPhysics(PRESETS[preset_name.upper()])
        self.controller = FirmwareFaithfulTorqueController(
            PRESETS[preset_name.upper()],
            runtime=ControllerRuntimeConfig(control_hz=control_hz),
        )
        self.instance_id = str(instance_id)
        self.target_kind = str(target_kind)
        self.preset_name = str(preset_name).upper()
        self.control_hz = float(control_hz)
        self.control_dt = 1.0 / self.control_hz
        self.actuator_cfg = actuator_nonidealities or ActuatorNonidealityConfig()
        self.sensor_cfg = sensor_nonidealities or SensorNonidealityConfig()
        self._actuator_state_nm = 0.0
        self._sensor_pos_delay = DelayLine(self.sensor_cfg.delay_s, self.control_dt)
        self._sensor_vel_delay = DelayLine(self.sensor_cfg.delay_s, self.control_dt)
        self._torque_command_delay = DelayLine(self.actuator_cfg.command_delay_s, self.control_dt)
        
        # Mapping for sensors/actuators
        self.joint_id = mujoco.mj_name2id(self.model, mujoco.mjtObj.mjOBJ_JOINT, 'prism_joint')
        self.actuator_id = mujoco.mj_name2id(self.model, mujoco.mjtObj.mjOBJ_ACTUATOR, 'prism_motor')

    def set_preset(self, preset_name: str) -> None:
        preset_key = str(preset_name).upper()
        if preset_key not in PRESETS:
            raise ValueError(f"Unknown preset: {preset_key}")

        self.physics = PrismPhysics(PRESETS[preset_key])
        self.controller = FirmwareFaithfulTorqueController(
            PRESETS[preset_key],
            runtime=ControllerRuntimeConfig(control_hz=self.control_hz),
        )
        self.preset_name = preset_key
        self._actuator_state_nm = 0.0
        self._sensor_pos_delay = DelayLine(self.sensor_cfg.delay_s, self.control_dt)
        self._sensor_vel_delay = DelayLine(self.sensor_cfg.delay_s, self.control_dt)
        self._torque_command_delay = DelayLine(self.actuator_cfg.command_delay_s, self.control_dt)

    def get_telemetry(self):
        """
        Firmware-compatible status payload keys.
        """
        pos_deg = np.rad2deg(self.data.qpos[self.joint_id])
        vel_rad_s = self.data.qvel[self.joint_id]
        torque_nm = self.data.ctrl[self.actuator_id]
        
        return {
            "pos_deg": float(pos_deg),
            "vel_rad_s": float(vel_rad_s),
            "torque_nm": float(torque_nm),
            "status": 2,
            "target_kind": self.target_kind,
            "target_id": self.instance_id,
            "instance_id": self.instance_id,
            "active_preset": self.preset_name,
            "temp_fet": 0.0,
            "temp_motor": 0.0,
            "bus_voltage": 0.0,
            "safety": {
                "errors": 0,
                "last_error": 0,
                "estops": 0,
            },
            "controller": {
                "quiet_active": bool(self.controller.quiet_active),
                "filtered_torque_nm": float(self.controller.filtered_torque_nm),
                "passivity_energy_j": float(self.controller.passivity_energy_j),
            },
        }

    def set_state(self, pos_deg: float, vel_rad_s: float):
        self.data.qpos[self.joint_id] = np.deg2rad(pos_deg)
        self.data.qvel[self.joint_id] = vel_rad_s

    def _measure_state(self):
        pos_deg = np.rad2deg(self.data.qpos[self.joint_id])
        vel_rad_s = self.data.qvel[self.joint_id]

        if self.sensor_cfg.enabled:
            pos_deg = self._sensor_pos_delay.push_pop(pos_deg)
            vel_rad_s = self._sensor_vel_delay.push_pop(vel_rad_s)
            pos_deg = quantize(pos_deg, self.sensor_cfg.position_quantization_deg)
            vel_rad_s = quantize(vel_rad_s, self.sensor_cfg.velocity_quantization_rad_s)

        return pos_deg, vel_rad_s

    def _apply_actuator_nonidealities(self, torque_nm: float) -> float:
        if not self.actuator_cfg.enabled:
            return torque_nm

        delayed = self._torque_command_delay.push_pop(torque_nm)
        lagged = first_order_lag_step(
            target=delayed,
            state=self._actuator_state_nm,
            dt_s=self.control_dt,
            tau_s=self.actuator_cfg.first_order_lag_tau_s,
        )
        slew_limited = slew_rate_limit_step(
            target=lagged,
            prev=self._actuator_state_nm,
            dt_s=self.control_dt,
            slew_nm_per_s=self.actuator_cfg.slew_rate_nm_per_s,
        )

        if self.actuator_cfg.torque_limit_nm is not None:
            limit = abs(float(self.actuator_cfg.torque_limit_nm))
            slew_limited = max(-limit, min(limit, slew_limited))

        self._actuator_state_nm = slew_limited
        return slew_limited

    def _run_control_tick(self):
        pos_deg, vel_rad_s = self._measure_state()
        torque_nm, _ = self.controller.compute_torque(pos_deg, vel_rad_s)
        torque_nm = self._apply_actuator_nonidealities(torque_nm)
        self.data.ctrl[self.actuator_id] = torque_nm

        target_time = self.data.time + self.control_dt
        while self.data.time < target_time:
            mujoco.mj_step(self.model, self.data)

    def step_control_tick(self):
        self._run_control_tick()

    def run_headless(self, duration_s: float, realtime: bool = False):
        end_time = self.data.time + float(duration_s)
        while self.data.time < end_time:
            step_start = time.time()
            self._run_control_tick()
            if realtime:
                sleep_s = self.control_dt - (time.time() - step_start)
                if sleep_s > 0.0:
                    time.sleep(sleep_s)

    def run(self):
        with mujoco.viewer.launch_passive(self.model, self.data) as viewer:
            while viewer.is_running():
                step_start = time.time()
                self.step_control_tick()

                viewer.sync()

                time_until_next_step = self.control_dt - (time.time() - step_start)
                if time_until_next_step > 0:
                    time.sleep(time_until_next_step)

if __name__ == "__main__":
    model_path = str(Path(__file__).resolve().parents[1] / "models" / "prism_device.xml")
    sim = PrismSim(model_path, preset_name='HEAVY', control_hz=1000.0)
    sim.run()
