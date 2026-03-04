from __future__ import annotations

import argparse
import time
from pathlib import Path

import mujoco
import mujoco.viewer


def main() -> int:
    parser = argparse.ArgumentParser(description="Visualize PRISM MuJoCo model for geometry alignment checks.")
    parser.add_argument(
        "--model",
        default=str(Path(__file__).resolve().parent.parent / "models" / "prism_device.xml"),
        help="Path to MJCF model.",
    )
    parser.add_argument("--realtime", action="store_true", help="Wall-clock pace stepping.")
    parser.add_argument("--show-world-frame", action="store_true", help="Render MuJoCo built-in world frame triad.")
    parser.add_argument("--show-model-triad", action="store_true", help="Show model debug triad geoms/sites (group 4).")
    args = parser.parse_args()

    model = mujoco.MjModel.from_xml_path(args.model)
    data = mujoco.MjData(model)

    with mujoco.viewer.launch_passive(model, data) as viewer:
        viewer.opt.geomgroup[4] = 1 if args.show_model_triad else 0
        viewer.opt.sitegroup[4] = 1 if args.show_model_triad else 0
        if args.show_world_frame:
            viewer.opt.frame = mujoco.mjtFrame.mjFRAME_WORLD
        while viewer.is_running():
            t0 = time.time()
            mujoco.mj_step(model, data)
            viewer.sync()
            if args.realtime:
                dt = model.opt.timestep - (time.time() - t0)
                if dt > 0.0:
                    time.sleep(dt)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
