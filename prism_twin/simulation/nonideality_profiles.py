from __future__ import annotations

from pathlib import Path
import json

from nonidealities import ActuatorNonidealityConfig, SensorNonidealityConfig


def profiles_path(root_dir: Path | None = None) -> Path:
    if root_dir is None:
        root_dir = Path(__file__).resolve().parents[1]
    return root_dir / "config" / "nonideality_profiles.json"


def load_profile(profile_name: str | None = None, root_dir: Path | None = None) -> tuple[ActuatorNonidealityConfig, SensorNonidealityConfig, str]:
    payload = json.loads(profiles_path(root_dir).read_text(encoding="utf-8"))
    profiles = payload.get("profiles", {})
    selected = profile_name or payload.get("default_profile", "ideal")
    if selected not in profiles:
        raise ValueError(f"Unknown nonideality profile: {selected}")

    raw = profiles[selected]
    act = raw.get("actuator", {})
    sen = raw.get("sensor", {})
    return (
        ActuatorNonidealityConfig(
            enabled=bool(act.get("enabled", False)),
            first_order_lag_tau_s=float(act.get("first_order_lag_tau_s", 0.0)),
            slew_rate_nm_per_s=float(act.get("slew_rate_nm_per_s", 0.0)),
            command_delay_s=float(act.get("command_delay_s", 0.0)),
            torque_limit_nm=act.get("torque_limit_nm", None),
        ),
        SensorNonidealityConfig(
            enabled=bool(sen.get("enabled", False)),
            position_quantization_deg=float(sen.get("position_quantization_deg", 0.0)),
            velocity_quantization_rad_s=float(sen.get("velocity_quantization_rad_s", 0.0)),
            delay_s=float(sen.get("delay_s", 0.0)),
        ),
        selected,
    )
