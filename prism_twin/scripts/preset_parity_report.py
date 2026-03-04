from __future__ import annotations

import json
from pathlib import Path


def expected_twin_config(fw: dict) -> dict[str, float]:
    torque_limit = float(fw["torque_limit_nm"])
    return {
        "closed_position_deg": 0.0,
        "open_position_deg": float(fw["default_travel_deg"]),
        "degrees_per_turn": 360.0,
        "hil_b_viscous_nm_s_per_rad": float(fw["hil_b_viscous_nm_s_per_rad"]),
        "hil_tau_c_coulomb_nm": float(fw["hil_tau_c_coulomb_nm"]),
        "hil_k_w_wall_stiffness_nm_per_turn": float(fw["hil_k_w_wall_stiffness_nm_per_turn"]),
        "hil_c_w_wall_damping_nm_s_per_turn": float(fw["hil_c_w_wall_damping_nm_s_per_turn"]),
        "hil_eps_smoothing": float(fw["hil_eps_smoothing"]),
        "hil_tau_max_limit_nm": torque_limit,
        "torque_limit_nm": torque_limit,
    }


def compare_dict(a: dict, b: dict, tol: float = 1e-9) -> list[str]:
    issues: list[str] = []
    for key in sorted(set(a.keys()) | set(b.keys())):
        if key not in a:
            issues.append(f"missing_in_expected:{key}")
            continue
        if key not in b:
            issues.append(f"missing_in_actual:{key}")
            continue
        va = a[key]
        vb = b[key]
        if isinstance(va, (int, float)) and isinstance(vb, (int, float)):
            if abs(float(va) - float(vb)) > tol:
                issues.append(f"value_mismatch:{key}:expected={va}:actual={vb}")
        else:
            if va != vb:
                issues.append(f"value_mismatch:{key}:expected={va}:actual={vb}")
    return issues


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    config_path = root / "config" / "generated_firmware_presets.json"
    report_path = root / "reports" / "preset_parity_report.json"

    payload = json.loads(config_path.read_text(encoding="utf-8"))
    firmware_presets = payload.get("presets", {})
    twin_presets = payload.get("twin_hil_config", {})

    results = []
    all_ok = True
    for preset in ["LIGHT", "MEDIUM", "HEAVY", "INDUSTRIAL"]:
        fw = firmware_presets.get(preset)
        twin = twin_presets.get(preset)
        if fw is None or twin is None:
            all_ok = False
            results.append({
                "preset": preset,
                "ok": False,
                "issues": ["missing_preset_entry"],
            })
            continue

        expected = expected_twin_config(fw)
        issues = compare_dict(expected, twin)
        ok = len(issues) == 0
        all_ok = all_ok and ok
        results.append({
            "preset": preset,
            "ok": ok,
            "issues": issues,
        })

    out = {
        "source": str(config_path),
        "all_ok": all_ok,
        "results": results,
    }
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(out, indent=2) + "\n", encoding="utf-8")

    print(json.dumps({"all_ok": all_ok, "report": str(report_path)}, indent=2))
    return 0 if all_ok else 1


if __name__ == "__main__":
    raise SystemExit(main())