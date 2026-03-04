from __future__ import annotations

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
SIM_DIR = ROOT / "simulation"
if str(SIM_DIR) not in sys.path:
    sys.path.insert(0, str(SIM_DIR))

from run_sim import PrismSim


REQUIRED_TOP_LEVEL = {
    "pos_deg": float,
    "vel_rad_s": float,
    "torque_nm": float,
    "status": int,
    "temp_fet": float,
    "temp_motor": float,
    "bus_voltage": float,
    "safety": dict,
}

REQUIRED_SAFETY = {
    "errors": int,
    "last_error": int,
    "estops": int,
}


def _type_ok(value, expected_type) -> bool:
    if expected_type is float:
        return isinstance(value, (float, int))
    return isinstance(value, expected_type)


def main() -> int:
    model_path = ROOT / "models" / "prism_device.xml"
    sim = PrismSim(str(model_path), preset_name="HEAVY", control_hz=1000.0)
    sim.step_control_tick()
    payload = sim.get_telemetry()

    errors: list[str] = []
    for key, expected in REQUIRED_TOP_LEVEL.items():
        if key not in payload:
            errors.append(f"missing_key:{key}")
            continue
        if not _type_ok(payload[key], expected):
            errors.append(f"type_mismatch:{key}:expected={expected.__name__}:actual={type(payload[key]).__name__}")

    if "safety" in payload and isinstance(payload["safety"], dict):
        for key, expected in REQUIRED_SAFETY.items():
            if key not in payload["safety"]:
                errors.append(f"missing_safety_key:{key}")
                continue
            if not _type_ok(payload["safety"][key], expected):
                errors.append(
                    f"type_mismatch:safety.{key}:expected={expected.__name__}:actual={type(payload['safety'][key]).__name__}"
                )

    if errors:
        print("Telemetry schema check FAILED")
        for err in errors:
            print(err)
        return 1

    print("Telemetry schema check PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())