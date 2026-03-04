from __future__ import annotations

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
SIM_DIR = ROOT / "simulation"
if str(SIM_DIR) not in sys.path:
    sys.path.insert(0, str(SIM_DIR))

from handle_catalog import load_handle_catalog, validate_handle_assets


def main() -> int:
    catalog, default_id = load_handle_catalog()
    print(f"Loaded {len(catalog)} handles. default_handle_id={default_id}")

    all_errors: list[str] = []
    for handle_id, handle in catalog.items():
        errors = validate_handle_assets(handle)
        if errors:
            all_errors.extend([f"{handle_id}:{err}" for err in errors])

    if all_errors:
        print("Handle catalog validation FAILED")
        for err in all_errors:
            print(err)
        return 1

    print("Handle catalog validation PASSED")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
