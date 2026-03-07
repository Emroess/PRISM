from __future__ import annotations

import argparse
import json
from dataclasses import dataclass
from pathlib import Path
from typing import Callable

import numpy as np

SCRIPT_DIR = Path(__file__).resolve().parent
ROOT_DIR = SCRIPT_DIR.parent

from simulation.run_sim import PrismSim
from simulation.nonidealities import ActuatorNonidealityConfig, SensorNonidealityConfig
from scripts.firmware_faithful_controller import FirmwareFaithfulTorqueController, ControllerRuntimeConfig


@dataclass
class ValidationSample:
    time_s: float
    ref_torque_nm: float
    pos_deg: float
    vel_rad_s: float
    torque_nm: float


@dataclass
class ValidationResult:
    profile_name: str
    preset: str
    metrics: dict[str, float]
    pass_fail: dict[str, bool]
    passed: bool
    sample_count: int
    notes: str


def step_profile(t_s: float) -> float:
    if t_s < 0.5:
        return 0.0
    if t_s < 1.5:
        return 1.0
    return -1.0


def chirp_profile(t_s: float, duration_s: float = 2.0) -> float:
    f0 = 0.25
    f1 = 4.0
    k = (f1 - f0) / max(duration_s, 1e-9)
    phase = 2.0 * np.pi * (f0 * t_s + 0.5 * k * t_s * t_s)
    return float(np.sin(phase))


def reversal_profile(t_s: float) -> tuple[float, float]:
    omega = 2.0 * np.sin(2.0 * np.pi * 0.8 * t_s)
    pos_deg = 45.0 * np.sin(2.0 * np.pi * 0.4 * t_s)
    return pos_deg, omega


def wall_impact_profile(t_s: float, open_position_deg: float) -> tuple[float, float]:
    if t_s < 0.8:
        pos = (open_position_deg + 20.0) * (t_s / 0.8)
        vel = np.deg2rad((open_position_deg + 20.0) / 0.8)
        return float(pos), float(vel)
    pos = open_position_deg + 20.0
    vel = 0.0
    return float(pos), float(vel)


def _load_thresholds() -> dict:
    threshold_path = ROOT_DIR / "config" / "validation_thresholds.json"
    with threshold_path.open("r", encoding="utf-8") as f:
        return json.load(f)


def _resolve_thresholds(config: dict, preset: str) -> dict[str, float]:
    global_thresholds = dict(config.get("global", {}))
    preset_overrides = dict(config.get("presets", {}).get(preset, {}))
    global_thresholds.update(preset_overrides)
    return global_thresholds


def _compute_torque_metrics(samples: list[ValidationSample]) -> dict[str, float]:
    if not samples:
        return {
            "torque_rmse_nm": 0.0,
            "torque_peak_error_nm": 0.0,
            "settling_time_s": 0.0,
            "overshoot_nm": 0.0,
            "chatter_count": 0.0,
            "phase_lag_s": 0.0,
        }

    time = np.array([s.time_s for s in samples], dtype=float)
    ref_torque = np.array([s.ref_torque_nm for s in samples], dtype=float)
    torque = np.array([s.torque_nm for s in samples], dtype=float)
    vel = np.array([s.vel_rad_s for s in samples], dtype=float)

    error = torque - ref_torque
    rmse = float(np.sqrt(np.mean(error * error)))
    peak_err = float(np.max(np.abs(error)))

    command_peak = float(np.max(np.abs(ref_torque))) if ref_torque.size else 0.0
    ref_peak = float(np.max(np.abs(ref_torque))) if ref_torque.size else 0.0
    if ref_peak < 1e-9:
        overshoot = 0.0
    else:
        overshoot = float(max(0.0, np.max(np.abs(torque)) - ref_peak))

    settle_band = max(0.05 * max(ref_peak, 1e-9), 0.02)
    settling_time = float(time[-1])
    for idx in range(len(error)):
        if np.all(np.abs(error[idx:]) <= settle_band):
            settling_time = float(time[idx])
            break

    low_vel = np.abs(vel) < np.deg2rad(2.0)
    sign = np.sign(torque)
    chatter = 0
    for idx in range(1, len(sign)):
        if low_vel[idx] and low_vel[idx - 1] and sign[idx] != sign[idx - 1]:
            if abs(torque[idx]) > 0.01 or abs(torque[idx - 1]) > 0.01:
                chatter += 1

    if ref_torque.size > 3 and np.any(np.abs(ref_torque) > 1e-6):
        centered_cmd = ref_torque - np.mean(ref_torque)
        centered_torque = torque - np.mean(torque)
        corr = np.correlate(centered_torque, centered_cmd, mode="full")
        lag_idx = int(np.argmax(corr)) - (len(centered_cmd) - 1)
        dt = float(time[1] - time[0]) if len(time) > 1 else 0.0
        phase_lag_s = abs(float(lag_idx) * dt)
    else:
        phase_lag_s = 0.0

    return {
        "torque_rmse_nm": rmse,
        "torque_peak_error_nm": peak_err,
        "settling_time_s": settling_time,
        "overshoot_nm": overshoot,
        "chatter_count": float(chatter),
        "phase_lag_s": phase_lag_s,
    }


def _evaluate_thresholds(metrics: dict[str, float], thresholds: dict[str, float]) -> tuple[dict[str, bool], bool]:
    checks = {
        "torque_rmse_nm": metrics["torque_rmse_nm"] <= thresholds["torque_rmse_nm_max"],
        "torque_peak_error_nm": metrics["torque_peak_error_nm"] <= thresholds["torque_peak_error_nm_max"],
        "settling_time_s": metrics["settling_time_s"] <= thresholds["settling_time_s_max"],
        "overshoot_nm": metrics["overshoot_nm"] <= thresholds["overshoot_nm_max"],
        "chatter_count": metrics["chatter_count"] <= thresholds["chatter_count_max"],
        "phase_lag_s": metrics["phase_lag_s"] <= thresholds["phase_lag_s_max"],
    }
    return checks, all(checks.values())


def run_profile(
    sim,
    preset: str,
    profile_name: str,
    profile_fn: Callable[[float], tuple[float, float]],
    duration_s: float = 2.0,
) -> ValidationResult:
    samples: list[ValidationSample] = []
    ref_controller = FirmwareFaithfulTorqueController(
        dict(sim.controller.preset_config),
        runtime=ControllerRuntimeConfig(control_hz=sim.control_hz),
    )
    t = 0.0
    dt = sim.control_dt

    while t < duration_s:
        pos_cmd_deg, vel_cmd_rad_s = profile_fn(t)
        sim.set_state(pos_cmd_deg, vel_cmd_rad_s)

        pos_deg, vel_rad_s = sim._measure_state()
        ref_torque, _ = ref_controller.compute_torque(pos_deg, vel_rad_s)

        sim.step_control_tick()
        telem = sim.get_telemetry()
        samples.append(
            ValidationSample(
                time_s=t,
                ref_torque_nm=float(ref_torque),
                pos_deg=float(telem["pos_deg"]),
                vel_rad_s=float(telem["vel_rad_s"]),
                torque_nm=float(telem["torque_nm"]),
            )
        )
        t += dt

    metrics = _compute_torque_metrics(samples)
    thresholds_cfg = _load_thresholds()
    thresholds = _resolve_thresholds(thresholds_cfg, preset)
    checks, passed = _evaluate_thresholds(metrics, thresholds)

    return ValidationResult(
        profile_name=profile_name,
        preset=preset,
        metrics=metrics,
        pass_fail=checks,
        passed=passed,
        sample_count=len(samples),
        notes="Deterministic profile evaluation complete.",
    )


def run_validation_suite(model_path: Path, preset: str, duration_s: float) -> list[ValidationResult]:
    actuator_cfg = ActuatorNonidealityConfig(enabled=False)
    sensor_cfg = SensorNonidealityConfig(enabled=False)
    sim = PrismSim(
        str(model_path),
        preset_name=preset,
        control_hz=1000.0,
        actuator_nonidealities=actuator_cfg,
        sensor_nonidealities=sensor_cfg,
    )

    results = [
        run_profile(
            sim,
            preset=preset,
            profile_name="reversal",
            profile_fn=reversal_profile,
            duration_s=duration_s,
        ),
        run_profile(
            sim,
            preset=preset,
            profile_name="chirp",
            profile_fn=lambda t: (
                30.0 * chirp_profile(t, duration_s=duration_s),
                2.0 * chirp_profile(t, duration_s=duration_s),
            ),
            duration_s=duration_s,
        ),
        run_profile(
            sim,
            preset=preset,
            profile_name="wall_impact",
            profile_fn=lambda t: wall_impact_profile(
                t,
                open_position_deg=float(sim.controller.preset_config["open_position_deg"]),
            ),
            duration_s=duration_s,
        ),
    ]
    return results


def write_report(results: list[ValidationResult], output_path: Path) -> dict:
    payload = {
        "summary": {
            "total": len(results),
            "passed": sum(1 for r in results if r.passed),
            "failed": sum(1 for r in results if not r.passed),
        },
        "results": [
            {
                "preset": r.preset,
                "profile": r.profile_name,
                "sample_count": r.sample_count,
                "passed": r.passed,
                "metrics": r.metrics,
                "checks": r.pass_fail,
                "notes": r.notes,
            }
            for r in results
        ],
    }
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    return payload


def main() -> int:
    parser = argparse.ArgumentParser(description="Run PRISM twin validation suite.")
    parser.add_argument("--preset", default="HEAVY", choices=["LIGHT", "MEDIUM", "HEAVY", "INDUSTRIAL"])
    parser.add_argument("--duration", type=float, default=2.0)
    parser.add_argument(
        "--output",
        default=str(ROOT_DIR / "reports" / "validation_report.json"),
        help="Path to JSON report output",
    )
    args = parser.parse_args()

    model_path = ROOT_DIR / "models" / "prism_device.xml"
    results = run_validation_suite(model_path=model_path, preset=args.preset, duration_s=args.duration)
    report = write_report(results, Path(args.output))

    print(json.dumps(report["summary"], indent=2))
    return 0 if report["summary"]["failed"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
