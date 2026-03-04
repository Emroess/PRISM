from __future__ import annotations

import argparse
import json
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

import mujoco
import mujoco.viewer

from instance_manager import PrismRuntimeManager


class TwinApiHandler(BaseHTTPRequestHandler):
    manager: PrismRuntimeManager | None = None

    def _send_json(self, status: int, payload: dict) -> None:
        body = json.dumps(payload).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def _read_json(self) -> dict:
        content_length = int(self.headers.get("Content-Length", "0"))
        if content_length <= 0:
            return {}
        raw = self.rfile.read(content_length)
        return json.loads(raw.decode("utf-8"))

    def _instance_id(self, query: dict, body: dict) -> str:
        if "instance_id" in query and len(query["instance_id"]) > 0:
            return query["instance_id"][0]
        if "instance_id" in body:
            return str(body["instance_id"])
        return "prism_01"

    def _manager(self) -> PrismRuntimeManager:
        if self.manager is None:
            raise RuntimeError("Manager not initialized")
        return self.manager

    def _send_html(self, status: int, html_text: str) -> None:
        body = html_text.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self) -> None:
        mgr = self._manager()
        parsed = urlparse(self.path)
        query = parse_qs(parsed.query)

        try:
            if parsed.path == "/":
                ui_path = Path(__file__).resolve().parent / "sim_helper_ui.html"
                self._send_html(200, ui_path.read_text(encoding="utf-8"))
                return

            if parsed.path == "/api/v1/status":
                instance_id = self._instance_id(query, {})
                mgr.ensure_instance(instance_id)
                self._send_json(200, mgr.get_status(instance_id))
                return

            if parsed.path == "/api/v1/config":
                instance_id = self._instance_id(query, {})
                mgr.ensure_instance(instance_id)
                self._send_json(200, mgr.get_config(instance_id))
                return

            if parsed.path == "/api/v1/instances":
                self._send_json(200, {"instances": mgr.list_instances()})
                return

            if parsed.path == "/api/v1/handles":
                self._send_json(200, {"handles": mgr.available_handles()})
                return

            if parsed.path == "/api/v1/presets":
                self._send_json(200, {"presets": mgr.available_presets()})
                return

            self._send_json(404, {"status": "error", "error": "not_found"})
        except Exception as exc:
            self._send_json(500, {"status": "error", "error": str(exc)})

    def do_POST(self) -> None:
        mgr = self._manager()
        parsed = urlparse(self.path)
        query = parse_qs(parsed.query)
        body = self._read_json()

        try:
            if parsed.path == "/api/v1/instances":
                instance_id = self._instance_id(query, body)
                handle_id = body.get("handle_id")
                preset = body.get("preset")
                if instance_id in {item["instance_id"] for item in mgr.list_instances()}:
                    self._send_json(200, {"status": "ok", "instance": mgr.get_config(instance_id)})
                    return
                instance = mgr.create_instance(instance_id=instance_id, handle_id=handle_id, preset=preset)
                self._send_json(
                    201,
                    {
                        "status": "ok",
                        "instance": {
                            "instance_id": instance.instance_id,
                            "handle_id": instance.handle_id,
                            "preset": instance.preset,
                            "target_kind": "sim",
                            "target_id": instance.instance_id,
                        },
                    },
                )
                return

            if parsed.path == "/api/v1/control":
                instance_id = self._instance_id(query, body)
                mgr.ensure_instance(instance_id)
                action = str(body.get("action", "step"))
                ticks = int(body.get("ticks", 1))

                if action == "step":
                    mgr.step_all(ticks=max(1, ticks))
                    self._send_json(200, {"status": "ok", "instance_id": instance_id})
                    return
                if action == "pause":
                    mgr.stop()
                    self._send_json(200, {"status": "ok", "action": "pause"})
                    return
                if action == "resume":
                    mgr.start(realtime=True)
                    self._send_json(200, {"status": "ok", "action": "resume"})
                    return

                self._send_json(400, {"status": "error", "error": "unsupported_action"})
                return

            if parsed.path == "/api/v1/handle/select":
                instance_id = self._instance_id(query, body)
                handle_id = str(body.get("handle_id", ""))
                if not handle_id:
                    self._send_json(400, {"status": "error", "error": "missing_handle_id"})
                    return
                mgr.ensure_instance(instance_id)
                result = mgr.swap_handle(instance_id, handle_id)
                self._send_json(200, {"status": "ok", "result": result})
                return

            if parsed.path == "/api/v1/preset/select":
                instance_id = self._instance_id(query, body)
                preset = str(body.get("preset", ""))
                if not preset:
                    self._send_json(400, {"status": "error", "error": "missing_preset"})
                    return
                mgr.ensure_instance(instance_id)
                result = mgr.set_preset(instance_id, preset)
                self._send_json(200, {"status": "ok", "result": result})
                return

            if parsed.path == "/api/v1/interaction/set_position":
                instance_id = self._instance_id(query, body)
                position_deg = float(body.get("position_deg", 0.0))
                vel_rad_s = float(body.get("vel_rad_s", 0.0))
                mgr.ensure_instance(instance_id)
                result = mgr.set_joint_position(instance_id, position_deg, vel_rad_s)
                self._send_json(200, {"status": "ok", "result": result})
                return

            self._send_json(404, {"status": "error", "error": "not_found"})
        except ValueError as exc:
            self._send_json(400, {"status": "error", "error": str(exc)})
        except Exception as exc:
            self._send_json(500, {"status": "error", "error": str(exc)})


def main() -> int:
    parser = argparse.ArgumentParser(description="PRISM twin REST server (instance-scoped simulation API)")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8081)
    parser.add_argument("--start-loop", action="store_true", help="Start background realtime stepping loop")
    parser.add_argument(
        "--with-viewer",
        action="store_true",
        help="Run native MuJoCo viewer attached to the same runtime used by the helper UI/API.",
    )
    parser.add_argument(
        "--viewer-instance",
        default="prism_01",
        help="Instance ID to render when --with-viewer is enabled.",
    )
    parser.add_argument("--show-world-frame", action="store_true", help="Render MuJoCo built-in world frame triad.")
    parser.add_argument(
        "--show-model-triad",
        action="store_true",
        help="Show model debug triad geoms/sites (group 4) in native viewer.",
    )
    args = parser.parse_args()

    root_dir = Path(__file__).resolve().parents[1]
    mgr = PrismRuntimeManager(root_dir=root_dir)
    mgr.ensure_instance("prism_01")
    start_loop = args.start_loop or args.with_viewer
    if start_loop:
        mgr.start(realtime=True)

    TwinApiHandler.manager = mgr
    server = ThreadingHTTPServer((args.host, args.port), TwinApiHandler)
    print(f"Twin REST server listening on http://{args.host}:{args.port}")
    try:
        if args.with_viewer:
            server_thread = threading.Thread(target=server.serve_forever, daemon=True)
            server_thread.start()

            mgr.ensure_instance(args.viewer_instance)
            model, data = mgr.get_viewer_state(args.viewer_instance)
            with mujoco.viewer.launch_passive(model, data) as viewer:
                viewer.opt.geomgroup[4] = 1 if args.show_model_triad else 0
                viewer.opt.sitegroup[4] = 1 if args.show_model_triad else 0
                if args.show_world_frame:
                    viewer.opt.frame = mujoco.mjtFrame.mjFRAME_WORLD

                while viewer.is_running():
                    mgr.sync_viewer(viewer)
                    time.sleep(0.01)
        else:
            server.serve_forever()
    finally:
        mgr.stop()
        server.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
