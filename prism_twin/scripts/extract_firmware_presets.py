from __future__ import annotations

import json
import re
from pathlib import Path


PRESET_KEYS = ["LIGHT", "MEDIUM", "HEAVY", "INDUSTRIAL"]


def parse_default_presets(c_source: str) -> dict[str, dict[str, float | str]]:
    marker = "static const struct preset_params default_presets"
    start = c_source.find(marker)
    if start < 0:
        raise RuntimeError("Could not find default_presets in valve_nvm.c")

    block_start = c_source.find("{", start)
    block_end = c_source.find("};", block_start)
    if block_start < 0 or block_end < 0:
        raise RuntimeError("Could not isolate default_presets block")

    block = c_source[block_start:block_end]

    chunks = re.findall(
        r"/\*\s*VALVE_PRESET_([A-Z_]+)\s*\*/\s*\{(.*?)\}\s*,?",
        block,
        flags=re.S,
    )
    if len(chunks) < 4:
        raise RuntimeError("Expected 4 preset entries in default_presets")

    parsed: dict[str, dict[str, float | str]] = {}
    for label, body in chunks:
        name_match = re.search(r'\.name\s*=\s*"([^"]+)"', body)
        if not name_match:
            raise RuntimeError(f"Missing .name for preset {label}")

        def get_float(field: str) -> float:
            m = re.search(rf"\.{field}\s*=\s*([-+]?\d*\.?\d+(?:e[-+]?\d+)?)f", body)
            if not m:
                raise RuntimeError(f"Missing .{field} for preset {label}")
            return float(m.group(1))

        parsed[label] = {
            "name": name_match.group(1),
            "torque_limit_nm": get_float("torque_limit_nm"),
            "default_travel_deg": get_float("default_travel_deg"),
            "hil_b_viscous_nm_s_per_rad": get_float("hil_b_viscous_nm_s_per_rad"),
            "hil_tau_c_coulomb_nm": get_float("hil_tau_c_coulomb_nm"),
            "hil_k_w_wall_stiffness_nm_per_turn": get_float("hil_k_w_wall_stiffness_nm_per_turn"),
            "hil_c_w_wall_damping_nm_s_per_turn": get_float("hil_c_w_wall_damping_nm_s_per_turn"),
            "hil_eps_smoothing": get_float("hil_eps_smoothing"),
        }

    for key in PRESET_KEYS:
        if key not in parsed:
            raise RuntimeError(f"Preset {key} missing from extracted firmware defaults")

    return {key: parsed[key] for key in PRESET_KEYS}


def build_twin_preset(fw: dict[str, float | str]) -> dict[str, float]:
    default_travel = float(fw["default_travel_deg"])
    torque_limit = float(fw["torque_limit_nm"])
    return {
        "closed_position_deg": 0.0,
        "open_position_deg": default_travel,
        "degrees_per_turn": 360.0,
        "hil_b_viscous_nm_s_per_rad": float(fw["hil_b_viscous_nm_s_per_rad"]),
        "hil_tau_c_coulomb_nm": float(fw["hil_tau_c_coulomb_nm"]),
        "hil_k_w_wall_stiffness_nm_per_turn": float(fw["hil_k_w_wall_stiffness_nm_per_turn"]),
        "hil_c_w_wall_damping_nm_s_per_turn": float(fw["hil_c_w_wall_damping_nm_s_per_turn"]),
        "hil_eps_smoothing": float(fw["hil_eps_smoothing"]),
        "hil_tau_max_limit_nm": torque_limit,
        "torque_limit_nm": torque_limit,
    }


def main() -> None:
    script_dir = Path(__file__).resolve().parent
    workspace_root = script_dir.parent.parent
    firmware_path = workspace_root / "PRISM_source" / "firmware" / "src" / "valve" / "valve_nvm.c"
    output_path = script_dir.parent / "config" / "generated_firmware_presets.json"

    c_source = firmware_path.read_text(encoding="utf-8")
    firmware_defaults = parse_default_presets(c_source)

    payload = {
        "source": "PRISM_source/firmware/src/valve/valve_nvm.c",
        "generated_at": "2026-03-04",
        "presets": firmware_defaults,
        "twin_hil_config": {
            key: build_twin_preset(value)
            for key, value in firmware_defaults.items()
        },
    }

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {output_path}")


if __name__ == "__main__":
    main()