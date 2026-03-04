from __future__ import annotations

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
SIM_DIR = ROOT / "simulation"
if str(SIM_DIR) not in sys.path:
    sys.path.insert(0, str(SIM_DIR))

from instance_manager import PrismRuntimeManager


def main() -> int:
    mgr = PrismRuntimeManager(root_dir=ROOT)
    mgr.ensure_instance("prism_01")
    mgr.create_instance("prism_02")

    mgr.set_joint_position("prism_01", 45.0, 0.0)
    mgr.set_joint_position("prism_02", -30.0, 0.0)
    mgr.step_all(5)

    s1 = mgr.get_status("prism_01")
    s2 = mgr.get_status("prism_02")

    errors: list[str] = []
    if s1.get("target_id") != "prism_01":
        errors.append("wrong_target_id_prism_01")
    if s2.get("target_id") != "prism_02":
        errors.append("wrong_target_id_prism_02")

    if abs(float(s1.get("pos_deg", 0.0)) - float(s2.get("pos_deg", 0.0))) < 1e-3:
        errors.append("positions_not_isolated")

    if errors:
        print("Instance isolation FAILED")
        for err in errors:
            print(err)
        return 1

    print("Instance isolation PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
