#!/usr/bin/env python3
"""Record a PRISM-LOG/1.0 file from the live TCP stream (and optional REST).

    python3 tools/log_viewer/record.py --ip 192.168.1.100 -o capture.log
    python3 tools/log_viewer/record.py --ip 192.168.1.100 --rest --duration 30

Without --rest, the file is schema_level=stream (everything the firmware
currently emits on port 8888). With --rest, config / ODrive / timing /
interaction are polled at ~10 Hz and merged onto the nearest sample.
"""

from __future__ import annotations

import argparse
import json
import os
import socket
import sys
import threading
import time
from datetime import datetime, timezone
from typing import Any, Dict, Optional
from urllib.error import HTTPError, URLError
from urllib.request import Request, urlopen

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
from prism_log import MAGIC, STREAM_MAP, _norm_status, _utc_now  # noqa: E402

DEFAULT_API_KEY = "steve-valve-2025"
DEFAULT_DEVICE_IP = "10.0.1.15"


def _rest(ip: str, path: str, api_key: str, timeout: float = 0.4) -> Optional[Dict[str, Any]]:
    url = f"http://{ip}:8080{path}"
    req = Request(url, headers={"X-API-Key": api_key})
    try:
        with urlopen(req, timeout=timeout) as resp:
            return json.loads(resp.read().decode("utf-8"))
    except (URLError, TimeoutError, json.JSONDecodeError, OSError):
        return None


def probe_device(ip: str, api_key: str, timeout: float = 2.0) -> str:
    """GET /api/v1/status. Returns an error string, or '' if the device accepted the key."""
    url = f"http://{ip}:8080/api/v1/status"
    req = Request(url, headers={"X-API-Key": api_key})
    try:
        with urlopen(req, timeout=timeout) as resp:
            code = getattr(resp, "status", 200)
            resp.read()
            if code == 401 or code == 403:
                return "API key rejected (401). Check the password / X-API-Key."
            if code >= 400:
                return f"REST /status returned HTTP {code}"
            return ""
    except HTTPError as exc:
        if exc.code in (401, 403):
            return "API key rejected. Use the device X-API-Key (default steve-valve-2025)."
        return f"REST /status returned HTTP {exc.code}"
    except Exception as exc:
        reason = str(exc)
        if "401" in reason or "Unauthorized" in reason:
            return "API key rejected. Use the device X-API-Key (default steve-valve-2025)."
        if "timed out" in reason.lower() or "timeout" in type(exc).__name__.lower():
            return f"No REST response from {ip}:8080 (wrong IP, or device off the LAN)."
        if "Name or service" in reason or "Errno 113" in reason or "Network is unreachable" in reason:
            return f"Cannot reach {ip}: {reason}"
        return f"Cannot reach {ip}:8080 ({exc})"


def _start_stream(ip: str, api_key: str, interval_ms: int) -> None:
    body = json.dumps({"action": "start", "interval_ms": interval_ms}).encode()
    req = Request(
        f"http://{ip}:8080/api/v1/stream",
        data=body,
        headers={"X-API-Key": api_key, "Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urlopen(req, timeout=2.0) as resp:
            resp.read()
    except Exception as exc:
        print(f"warning: could not start stream via REST ({exc}); connecting anyway", file=sys.stderr)


class RestPoller:
    def __init__(self, ip: str, api_key: str) -> None:
        self.ip = ip
        self.api_key = api_key
        self.lock = threading.Lock()
        self.latest: Dict[str, Any] = {}
        self.config: Optional[Dict[str, Any]] = None
        self.header_bits: Dict[str, Any] = {}
        self._stop = threading.Event()
        self._thread: Optional[threading.Thread] = None

    def start(self) -> None:
        self._thread = threading.Thread(target=self._run, name="rest-poll", daemon=True)
        self._thread.start()

    def stop(self) -> None:
        self._stop.set()

    def snapshot(self) -> Dict[str, Any]:
        with self.lock:
            return dict(self.latest)

    def _run(self) -> None:
        first = True
        while not self._stop.is_set():
            cfg = _rest(self.ip, "/api/v1/config", self.api_key)
            odrive = _rest(self.ip, "/api/v1/odrive", self.api_key)
            timing = _rest(self.ip, "/api/v1/timing", self.api_key)
            inter = _rest(self.ip, "/api/v1/interaction", self.api_key)
            hitl = _rest(self.ip, "/api/v1/hitl", self.api_key)
            merged: Dict[str, Any] = {}
            if odrive:
                tel = odrive.get("telemetry") or odrive
                enc = odrive.get("encoder") or {}
                if "bus_voltage" in tel:
                    merged["bus_v"] = tel.get("bus_voltage")
                    merged["bus_i"] = tel.get("bus_current")
                    merged["temp_fet_c"] = tel.get("fet_temp")
                    merged["temp_motor_c"] = tel.get("motor_temp")
                else:
                    merged["bus_v"] = odrive.get("bus_voltage")
                    merged["bus_i"] = odrive.get("bus_current")
                    merged["temp_fet_c"] = odrive.get("fet_temp")
                    merged["temp_motor_c"] = odrive.get("motor_temp")
                merged["axis_state"] = odrive.get("axis_state")
                merged["axis_error"] = odrive.get("axis_error")
                if enc.get("position") is not None:
                    merged["vel_odrive_rad_s"] = None  # filled if we have scale
            if timing:
                loop = timing.get("loop_us") or {}
                if "last" in loop:
                    merged["loop_time_us"] = loop.get("last")
            if inter:
                mode = inter.get("mode") or inter.get("interaction")
                if mode:
                    merged["interaction"] = str(mode).lower()
            if hitl:
                merged["hitl_connected"] = bool(hitl.get("connected") or hitl.get("client_connected"))
            with self.lock:
                self.latest = {k: v for k, v in merged.items() if v is not None}
                if cfg and first:
                    self.config = {
                        "t_s": 0.0,
                        "physics": {
                            "viscous_nm_s_per_rad": cfg.get("viscous"),
                            "coulomb_nm": cfg.get("coulomb"),
                            "wall_stiffness_nm_per_turn": cfg.get("wall_stiffness"),
                            "wall_damping_nm_s_per_turn": cfg.get("wall_damping"),
                            "smoothing_eps_rad_s": cfg.get("smoothing"),
                            "torque_limit_nm": cfg.get("torque_limit"),
                            "closed_position_deg": cfg.get("closed_position") or cfg.get("closed_pos"),
                            "open_position_deg": cfg.get("open_position") or cfg.get("open_pos"),
                            "degrees_per_turn": cfg.get("degrees_per_turn"),
                        },
                    }
                    first = False
            self._stop.wait(0.1)


def map_stream_packet(pkt: Dict[str, Any], extra: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
    row: Dict[str, Any] = {"rec": "sample"}
    for k, v in pkt.items():
        dest = STREAM_MAP.get(k)
        if dest is None:
            continue
        if dest == "status":
            row["status"] = _norm_status(v)
        else:
            row[dest] = v
    if "t_us" in row and "t_s" not in row:
        try:
            row["t_s"] = float(row["t_us"]) / 1e6
        except (TypeError, ValueError):
            pass
    if extra:
        for k, v in extra.items():
            if v is not None and k not in row:
                row[k] = v
    row["host_unix_s"] = time.time()
    return row


def record(
    ip: str,
    output: str,
    *,
    port: int = 8888,
    interval_ms: int = 10,
    duration: Optional[float] = None,
    rest: bool = False,
    api_key: str = DEFAULT_API_KEY,
    title: Optional[str] = None,
) -> None:
    _start_stream(ip, api_key, interval_ms)
    poller: Optional[RestPoller] = None
    if rest:
        poller = RestPoller(ip, api_key)
        poller.start()
        time.sleep(0.25)

    sock = socket.create_connection((ip, port), timeout=5.0)
    sock.settimeout(1.0)
    buf = b""
    n = 0
    t_start = time.time()
    os.makedirs(os.path.dirname(os.path.abspath(output)) or ".", exist_ok=True)

    header = {
        "rec": "header",
        "format": "PRISM-LOG",
        "version": "1.0",
        "schema_level": "full" if rest else "stream",
        "created_utc": _utc_now(),
        "created_unix_s": t_start,
        "session_id": datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ-capture"),
        "title": title or f"PRISM capture {ip}",
        "device": {"name": "PRISM", "ip": ip, "motor_controller": "ODrive S1"},
        "timebase": {
            "control_loop_hz": 8000,
            "encoder_broadcast_hz": 1000,
            "stream_hz": 1000.0 / interval_ms if interval_ms else None,
        },
        "source": {"kind": "stream", "path": f"tcp://{ip}:{port}", "recorder": "tools/log_viewer/record.py"},
    }

    print(f"recording {ip}:{port} → {output}  (Ctrl-C to stop)")
    with open(output, "w", encoding="utf-8") as fh:
        fh.write(MAGIC + "\n")
        fh.write(json.dumps(header, separators=(",", ":")) + "\n")
        if poller and poller.config:
            cfg = dict(poller.config)
            cfg["rec"] = "config"
            fh.write(json.dumps(cfg, separators=(",", ":")) + "\n")
        fh.write(json.dumps({"rec": "event", "t_s": 0, "kind": "session_start", "level": "info"}) + "\n")
        fh.flush()
        try:
            while True:
                if duration is not None and (time.time() - t_start) >= duration:
                    break
                try:
                    chunk = sock.recv(4096)
                except socket.timeout:
                    continue
                if not chunk:
                    print("connection closed")
                    break
                buf += chunk
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        pkt = json.loads(line.decode("utf-8"))
                    except (json.JSONDecodeError, UnicodeDecodeError):
                        continue
                    if pkt.get("stream"):
                        continue  # hello
                    extra = poller.snapshot() if poller else None
                    row = map_stream_packet(pkt, extra)
                    fh.write(json.dumps(row, separators=(",", ":")) + "\n")
                    n += 1
                    if n % 200 == 0:
                        fh.flush()
                        elapsed = time.time() - t_start
                        print(f"\r  {n} samples  {elapsed:.1f}s", end="", flush=True)
        except KeyboardInterrupt:
            print("\nstopping")
        finally:
            fh.write(
                json.dumps(
                    {
                        "rec": "event",
                        "t_s": time.time() - t_start,
                        "kind": "session_stop",
                        "level": "info",
                    }
                )
                + "\n"
            )
            fh.write(
                json.dumps(
                    {
                        "rec": "footer",
                        "samples": n,
                        "duration_s": time.time() - t_start,
                    }
                )
                + "\n"
            )
            fh.flush()
    sock.close()
    if poller:
        poller.stop()
    print(f"\nwrote {n} samples → {output}")


def main() -> None:
    ap = argparse.ArgumentParser(description="Record PRISM-LOG from TCP stream")
    ap.add_argument("--ip", required=True, help="PRISM device IP")
    ap.add_argument("-o", "--output", default=None)
    ap.add_argument("--port", type=int, default=8888)
    ap.add_argument("--interval-ms", type=int, default=10, help="Stream interval (REST start)")
    ap.add_argument("--duration", type=float, default=None, help="Seconds (default: until Ctrl-C)")
    ap.add_argument("--rest", action="store_true", help="Poll REST for motor/config")
    ap.add_argument("--api-key", default=DEFAULT_API_KEY)
    ap.add_argument("--title", default=None)
    args = ap.parse_args()
    out = args.output or datetime.now(timezone.utc).strftime("prism_%Y%m%dT%H%M%SZ_capture.log")
    record(
        args.ip,
        out,
        port=args.port,
        interval_ms=args.interval_ms,
        duration=args.duration,
        rest=args.rest,
        api_key=args.api_key,
        title=args.title,
    )


if __name__ == "__main__":
    main()
