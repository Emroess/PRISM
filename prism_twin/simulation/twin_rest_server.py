from __future__ import annotations

import argparse
import json
import threading
import time
import uuid
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from urllib.parse import parse_qs, urlparse

import mujoco
import mujoco.viewer

from instance_manager import PrismRuntimeManager


class TwinApiHandler(BaseHTTPRequestHandler):
    manager: PrismRuntimeManager | None = None
    runtime_started_at_s: float = 0.0
    request_count: int = 0
    session_bindings: dict[str, dict] = {}
    session_lock = threading.RLock()
    session_ttl_s: float = 900.0
    session_max_count: int = 64

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
        session_id = self._session_id(query, body)
        if session_id:
            with self.session_lock:
                session = self.session_bindings.get(session_id)
                if session is not None:
                    session["last_seen_s"] = time.time()
                    return str(session["instance_id"])
        return "prism_01"

    def _session_id(self, query: dict, body: dict) -> str | None:
        if "session_id" in query and len(query["session_id"]) > 0:
            return str(query["session_id"][0])
        if "session_id" in body and body["session_id"]:
            return str(body["session_id"])
        return None

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

    def _bump_request_count(self) -> int:
        with self.session_lock:
            self.request_count += 1
            return self.request_count

    def _prune_sessions(self) -> dict:
        now_s = time.time()
        with self.session_lock:
            before = len(self.session_bindings)
            expired_ids = [
                sid
                for sid, item in self.session_bindings.items()
                if (now_s - float(item.get("last_seen_s", now_s))) > self.session_ttl_s
            ]
            for sid in expired_ids:
                self.session_bindings.pop(sid, None)

            evicted = 0
            if len(self.session_bindings) > self.session_max_count:
                ranked = sorted(
                    self.session_bindings.items(),
                    key=lambda kv: float(kv[1].get("last_seen_s", now_s)),
                )
                overflow = len(self.session_bindings) - self.session_max_count
                for sid, _ in ranked[:overflow]:
                    self.session_bindings.pop(sid, None)
                    evicted += 1

            after = len(self.session_bindings)
        return {
            "before": before,
            "after": after,
            "expired": len(expired_ids),
            "evicted": evicted,
        }

    def _runtime_payload(self, mgr: PrismRuntimeManager) -> dict:
        summary = mgr.runtime_summary()
        with self.session_lock:
            sessions = [
                {
                    "session_id": sid,
                    "instance_id": item["instance_id"],
                    "client_id": item.get("client_id", ""),
                    "created_at_s": float(item["created_at_s"]),
                    "last_seen_s": float(item["last_seen_s"]),
                }
                for sid, item in self.session_bindings.items()
            ]
            req_count = int(self.request_count)
        return {
            "status": "ok",
            "uptime_s": float(time.time() - self.runtime_started_at_s),
            "requests": req_count,
            "session_policy": {
                "ttl_s": float(self.session_ttl_s),
                "max_count": int(self.session_max_count),
            },
            "manager": summary,
            "instances": mgr.list_instances(),
            "sessions": sessions,
        }

    def do_GET(self) -> None:
        self._bump_request_count()
        self._prune_sessions()
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

            if parsed.path == "/api/v1/health":
                runtime = mgr.runtime_summary()
                session_stats = self._prune_sessions()
                self._send_json(
                    200,
                    {
                        "status": "ok",
                        "uptime_s": float(time.time() - self.runtime_started_at_s),
                        "running": bool(runtime["running"]),
                        "instance_count": int(runtime["instance_count"]),
                        "session_count": int(session_stats["after"]),
                    },
                )
                return

            if parsed.path == "/api/v1/runtime":
                self._send_json(200, self._runtime_payload(mgr))
                return

            self._send_json(404, {"status": "error", "error": "not_found"})
        except Exception as exc:
            self._send_json(500, {"status": "error", "error": str(exc)})

    def do_POST(self) -> None:
        self._bump_request_count()
        self._prune_sessions()
        mgr = self._manager()
        parsed = urlparse(self.path)
        query = parse_qs(parsed.query)
        body = self._read_json()

        try:
            if parsed.path == "/api/v1/instances":
                instance_id = self._instance_id(query, body)
                handle_id = body.get("handle_id")
                preset = body.get("preset")
                instance, created = mgr.create_or_get_instance(instance_id=instance_id, handle_id=handle_id, preset=preset)
                self._send_json(
                    201 if created else 200,
                    {
                        "status": "ok",
                        "created": bool(created),
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

            if parsed.path == "/api/v1/session/bind":
                instance_id = self._instance_id(query, body)
                create_if_missing = bool(body.get("create_if_missing", True))
                if create_if_missing:
                    instance, created = mgr.create_or_get_instance(instance_id=instance_id)
                else:
                    instance = mgr.get_instance(instance_id)
                    created = False

                session_id = self._session_id(query, body) or str(uuid.uuid4())
                now_s = time.time()
                client_id = str(body.get("client_id", ""))
                with self.session_lock:
                    existing = self.session_bindings.get(session_id)
                    created_session = existing is None
                    if existing is None:
                        self.session_bindings[session_id] = {
                            "instance_id": instance.instance_id,
                            "client_id": client_id,
                            "created_at_s": now_s,
                            "last_seen_s": now_s,
                        }
                    else:
                        existing["instance_id"] = instance.instance_id
                        existing["client_id"] = client_id or existing.get("client_id", "")
                        existing["last_seen_s"] = now_s

                self._send_json(
                    200,
                    {
                        "status": "ok",
                        "created_instance": bool(created),
                        "created_session": bool(created_session),
                        "session_policy": {
                            "ttl_s": float(self.session_ttl_s),
                            "max_count": int(self.session_max_count),
                        },
                        "session": {
                            "session_id": session_id,
                            "instance_id": instance.instance_id,
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
    parser.add_argument(
        "--composed-model",
        default="",
        help="Optional path to an existing composed MuJoCo model to attach PRISM runtime without model regeneration.",
    )
    parser.add_argument("--start-loop", action="store_true", help="Start background realtime stepping loop")
    parser.add_argument(
        "--session-ttl-s",
        type=float,
        default=900.0,
        help="Session inactivity TTL in seconds before automatic expiration.",
    )
    parser.add_argument(
        "--session-max-count",
        type=int,
        default=64,
        help="Maximum number of active sessions before LRU-style eviction by last_seen.",
    )
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
    composed_model_path = Path(args.composed_model).resolve() if str(args.composed_model).strip() else None
    mgr = PrismRuntimeManager(root_dir=root_dir, composed_model_path=composed_model_path)
    mgr.ensure_instance("prism_01")
    start_loop = args.start_loop or args.with_viewer
    if start_loop:
        mgr.start(realtime=True)

    TwinApiHandler.manager = mgr
    TwinApiHandler.runtime_started_at_s = time.time()
    TwinApiHandler.request_count = 0
    TwinApiHandler.session_bindings = {}
    TwinApiHandler.session_ttl_s = max(1.0, float(args.session_ttl_s))
    TwinApiHandler.session_max_count = max(1, int(args.session_max_count))
    server = ThreadingHTTPServer((args.host, args.port), TwinApiHandler)
    mode = "composed" if composed_model_path else "standalone"
    print(f"Twin REST server listening on http://{args.host}:{args.port} (mode={mode})")
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
