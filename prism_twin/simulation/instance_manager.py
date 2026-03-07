from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path
import threading
import time

from run_sim import PrismSim
from run_sim import PRESETS
from handle_catalog import (
    HandleDefinition,
    build_model_for_handle,
    load_handle_catalog,
    validate_handle_assets,
)


@dataclass
class PrismInstance:
    instance_id: str
    handle_id: str
    preset: str
    model_path: Path
    sim: PrismSim
    prism_mount_pos: tuple[float, float, float]
    prism_mount_quat: tuple[float, float, float, float]


class PrismRuntimeManager:
    DEFAULT_INSTANCE_POSES = {
        "prism_01": ((0.0, 0.0, 0.0), (1.0, 0.0, 0.0, 0.0)),
        "prism_02": ((0.0, 0.0, 0.3), (0.70710678, 0.70710678, 0.0, 0.0)),
    }

    def __init__(
        self,
        root_dir: Path | None = None,
        control_hz: float = 1000.0,
        composed_model_path: Path | None = None,
    ):
        self.root_dir = root_dir or Path(__file__).resolve().parents[1]
        self.base_model_path = self.root_dir / "models" / "prism_device.xml"
        self.composed_model_path = Path(composed_model_path).resolve() if composed_model_path else None
        self.generated_models_dir = self.root_dir / "models"
        self.control_hz = float(control_hz)
        self.default_preset = "HEAVY"
        self._lock = threading.RLock()
        self._instances: dict[str, PrismInstance] = {}
        self._running = False
        self._loop_thread: threading.Thread | None = None
        self._created_at_s = time.time()

        self.handle_catalog, self.default_handle_id = load_handle_catalog()

    def create_or_get_instance(
        self,
        instance_id: str,
        handle_id: str | None = None,
        preset: str | None = None,
        prism_mount_pos: tuple[float, float, float] | None = None,
        prism_mount_quat: tuple[float, float, float, float] | None = None,
    ) -> tuple[PrismInstance, bool]:
        with self._lock:
            if instance_id in self._instances:
                return self._instances[instance_id], False
            created = self.create_instance(
                instance_id=instance_id,
                handle_id=handle_id,
                preset=preset,
                prism_mount_pos=prism_mount_pos,
                prism_mount_quat=prism_mount_quat,
            )
            return created, True

    def _model_path_for_instance(self, instance_id: str) -> Path:
        return self.generated_models_dir / f"prism_device_{instance_id}.xml"

    def is_composed_mode(self) -> bool:
        return self.composed_model_path is not None

    def _create_sim(self, instance_id: str, model_path: Path, preset: str) -> PrismSim:
        return PrismSim(
            str(model_path),
            preset_name=preset,
            instance_id=instance_id,
            target_kind="sim",
            control_hz=self.control_hz,
        )

    def create_instance(
        self,
        instance_id: str,
        handle_id: str | None = None,
        preset: str | None = None,
        prism_mount_pos: tuple[float, float, float] | None = None,
        prism_mount_quat: tuple[float, float, float, float] | None = None,
    ) -> PrismInstance:
        instance_id = str(instance_id)
        handle_id = handle_id or self.default_handle_id
        preset = (preset or self.default_preset).upper()

        with self._lock:
            if instance_id in self._instances:
                raise ValueError(f"Instance already exists: {instance_id}")
            if self.is_composed_mode() and instance_id != "prism_01":
                raise ValueError("Composed-scene mode currently supports only instance_id='prism_01'")

            if not self.is_composed_mode() and handle_id not in self.handle_catalog:
                raise ValueError(f"Unknown handle_id: {handle_id}")

            if not self.is_composed_mode():
                handle = self.handle_catalog[handle_id]
                errors = validate_handle_assets(handle)
                if errors:
                    raise ValueError(";".join(errors))

            default_pos, default_quat = self.DEFAULT_INSTANCE_POSES.get(
                instance_id,
                ((0.0, 0.0, 0.0), (1.0, 0.0, 0.0, 0.0)),
            )
            prism_mount_pos = prism_mount_pos or default_pos
            prism_mount_quat = prism_mount_quat or default_quat

            if self.is_composed_mode():
                model_path = self.composed_model_path
                handle_id = "composed_scene"
            else:
                model_path = self._model_path_for_instance(instance_id)
                build_model_for_handle(
                    self.base_model_path,
                    model_path,
                    handle,
                    prism_mount_pos=prism_mount_pos,
                    prism_mount_quat=prism_mount_quat,
                )

            sim = self._create_sim(instance_id, model_path, preset)
            instance = PrismInstance(
                instance_id=instance_id,
                handle_id=handle_id,
                preset=preset,
                model_path=model_path,
                sim=sim,
                prism_mount_pos=prism_mount_pos,
                prism_mount_quat=prism_mount_quat,
            )
            self._instances[instance_id] = instance
            return instance

    def get_instance(self, instance_id: str) -> PrismInstance:
        with self._lock:
            if instance_id not in self._instances:
                raise KeyError(instance_id)
            return self._instances[instance_id]

    def ensure_instance(self, instance_id: str = "prism_01") -> PrismInstance:
        with self._lock:
            if instance_id in self._instances:
                return self._instances[instance_id]
        return self.create_instance(instance_id)

    def list_instances(self) -> list[dict]:
        with self._lock:
            return [
                {
                    "instance_id": item.instance_id,
                    "handle_id": item.handle_id,
                    "preset": item.preset,
                    "target_kind": "sim",
                    "target_id": item.instance_id,
                    "prism_mount_pos": list(item.prism_mount_pos),
                    "prism_mount_quat": list(item.prism_mount_quat),
                }
                for item in self._instances.values()
            ]

    def step_all(self, ticks: int = 1) -> None:
        ticks = max(1, int(ticks))
        with self._lock:
            for _ in range(ticks):
                for instance in self._instances.values():
                    instance.sim.step_control_tick()

    def get_status(self, instance_id: str) -> dict:
        with self._lock:
            inst = self.get_instance(instance_id)
            payload = inst.sim.get_telemetry()
            payload["running"] = bool(self._running)
            return payload

    def get_config(self, instance_id: str) -> dict:
        with self._lock:
            inst = self.get_instance(instance_id)
            preset = inst.sim.controller.preset_config
            return {
                "target_kind": "sim",
                "target_id": inst.instance_id,
                "instance_id": inst.instance_id,
                "handle_id": inst.handle_id,
                "preset": inst.preset,
                "prism_mount_pos": list(inst.prism_mount_pos),
                "prism_mount_quat": list(inst.prism_mount_quat),
                "closed_pos": float(preset.get("closed_position_deg", 0.0)),
                "open_pos": float(preset.get("open_position_deg", 0.0)),
                "viscous": float(preset.get("hil_b_viscous_nm_s_per_rad", 0.0)),
                "coulomb": float(preset.get("hil_tau_c_coulomb_nm", 0.0)),
                "wall_stiffness": float(preset.get("hil_k_w_wall_stiffness_nm_per_turn", 0.0)),
                "wall_damping": float(preset.get("hil_c_w_wall_damping_nm_s_per_turn", 0.0)),
                "smoothing": float(preset.get("hil_eps_smoothing", 0.0)),
                "torque_limit": float(preset.get("torque_limit_nm", 0.0)),
            }

    def swap_handle(self, instance_id: str, handle_id: str) -> dict:
        if self.is_composed_mode():
            raise ValueError("Handle swapping is disabled in composed-scene mode")
        with self._lock:
            if handle_id not in self.handle_catalog:
                raise ValueError(f"Unknown handle_id: {handle_id}")
            inst = self.get_instance(instance_id)
            handle = self.handle_catalog[handle_id]
            errors = validate_handle_assets(handle)
            if errors:
                raise ValueError(";".join(errors))

            model_path = self._model_path_for_instance(instance_id)
            build_model_for_handle(
                self.base_model_path,
                model_path,
                handle,
                prism_mount_pos=inst.prism_mount_pos,
                prism_mount_quat=inst.prism_mount_quat,
            )

            old_sim = inst.sim
            sim = self._create_sim(instance_id, model_path, inst.preset)
            telem = old_sim.get_telemetry()
            sim.set_state(telem["pos_deg"], telem["vel_rad_s"])

            inst.sim = sim
            inst.handle_id = handle_id
            inst.model_path = model_path

        return {
            "instance_id": instance_id,
            "handle_id": handle_id,
            "target_kind": "sim",
            "target_id": instance_id,
        }

    def set_joint_position(self, instance_id: str, position_deg: float, vel_rad_s: float = 0.0) -> dict:
        with self._lock:
            inst = self.get_instance(instance_id)
            inst.sim.set_state(float(position_deg), float(vel_rad_s))
            if not self._running:
                inst.sim.step_control_tick()
            return {
                "instance_id": instance_id,
                "target_kind": "sim",
                "target_id": instance_id,
                "position_deg": float(position_deg),
                "vel_rad_s": float(vel_rad_s),
            }

    def get_viewer_state(self, instance_id: str = "prism_01"):
        with self._lock:
            inst = self.get_instance(instance_id)
            return inst.sim.model, inst.sim.data

    def sync_viewer(self, viewer) -> None:
        with self._lock:
            viewer.sync()

    def available_handles(self) -> list[dict]:
        if self.is_composed_mode():
            return []
        output = []
        for handle in self.handle_catalog.values():
            output.append(
                {
                    "handle_id": handle.handle_id,
                    "mesh_folder": handle.mesh_folder,
                    "mesh_files": list(handle.mesh_files),
                    "units": handle.units,
                    "cad_axis_to_model_axis": handle.cad_axis_to_model_axis,
                    "z_offset_mm": handle.z_offset_mm,
                    "rigid_with_shaft": handle.rigid_with_shaft,
                }
            )
        return output

    def available_presets(self) -> list[str]:
        return sorted(PRESETS.keys())

    def set_preset(self, instance_id: str, preset: str) -> dict:
        preset_name = str(preset).upper()
        if preset_name not in PRESETS:
            raise ValueError(f"Unknown preset: {preset_name}")

        with self._lock:
            inst = self.get_instance(instance_id)
            inst.sim.set_preset(preset_name)
            inst.preset = preset_name

            return {
                "instance_id": inst.instance_id,
                "target_kind": "sim",
                "target_id": inst.instance_id,
                "preset": inst.preset,
            }

    def _loop(self, realtime: bool) -> None:
        dt = 1.0 / self.control_hz
        while self._running:
            t0 = time.time()
            self.step_all(ticks=1)
            if realtime:
                sleep_s = dt - (time.time() - t0)
                if sleep_s > 0.0:
                    time.sleep(sleep_s)

    def start(self, realtime: bool = True) -> None:
        with self._lock:
            if self._running:
                return
            self._running = True
            self._loop_thread = threading.Thread(target=self._loop, args=(realtime,), daemon=True)
            self._loop_thread.start()

    def stop(self) -> None:
        with self._lock:
            self._running = False
            thread = self._loop_thread
            self._loop_thread = None
        if thread is not None:
            thread.join(timeout=1.0)

    def is_running(self) -> bool:
        with self._lock:
            return bool(self._running)

    def runtime_summary(self) -> dict:
        with self._lock:
            return {
                "running": bool(self._running),
                "control_hz": float(self.control_hz),
                "uptime_s": float(time.time() - self._created_at_s),
                "composed_mode": self.is_composed_mode(),
                "composed_model_path": str(self.composed_model_path) if self.composed_model_path else "",
                "instance_count": len(self._instances),
                "instance_ids": sorted(self._instances.keys()),
            }
