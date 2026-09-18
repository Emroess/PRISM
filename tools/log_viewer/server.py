#!/usr/bin/env python3
"""Localhost PRISM-LOG viewer.

    python3 tools/log_viewer/server.py
    python3 tools/log_viewer/server.py --port 8742 --open

Binds 127.0.0.1 only. Opens existing .log / .log.gz / stream JSONL / CSV from
the repo `logs/` directory, bundled samples, or a drag-dropped upload.
"""

from __future__ import annotations

import argparse
import json
import mimetypes
import os
import sys
import threading
import uuid
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from typing import Any, Dict, List, Optional, Tuple
from urllib.parse import parse_qs, unquote, urlparse

HERE = os.path.dirname(os.path.abspath(__file__))
STATIC = os.path.join(HERE, "static")
SAMPLES = os.path.join(HERE, "samples")
REPO_ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
LOGS_DIR = os.path.join(REPO_ROOT, "logs")

sys.path.insert(0, HERE)
from prism_log import (  # noqa: E402
    CHANNELS,
    PrismLog,
    parse_bytes,
    parse_log,
    series_bundle,
    write_demo,
)
from record import (  # noqa: E402
    DEFAULT_API_KEY,
    DEFAULT_DEVICE_IP,
    _start_stream,
    map_stream_packet,
    probe_device,
)

MAX_UPLOAD = 80 * 1024 * 1024  # 80 MiB
LOG_EXTS = (".log", ".log.gz", ".jsonl", ".csv", ".json")

_sessions: Dict[str, Any] = {}
_sessions_lock = threading.Lock()
_active_id: Optional[str] = None
_live: Dict[str, Any] = {
    "stop": threading.Event(),
    "ready": threading.Event(),
    "thread": None,
    "session_id": None,
    "error": None,
    "ip": None,
    "n": 0,
    "connected": False,
}


def _is_log(name: str) -> bool:
    lower = name.lower()
    return any(lower.endswith(ext) for ext in LOG_EXTS)


def _list_dir(folder: str, origin: str) -> List[Dict[str, Any]]:
    out: List[Dict[str, Any]] = []
    if not os.path.isdir(folder):
        return out
    try:
        names = os.listdir(folder)
    except OSError:
        return out
    for name in sorted(names):
        if name.startswith("."):
            continue
        if not _is_log(name):
            continue
        path = os.path.join(folder, name)
        try:
            st = os.stat(path)
        except OSError:
            continue
        out.append(
            {
                "name": name,
                "path": path,
                "origin": origin,
                "bytes": st.st_size,
                "mtime": st.st_mtime,
            }
        )
    return out


def list_files() -> List[Dict[str, Any]]:
    files = _list_dir(SAMPLES, "samples") + _list_dir(LOGS_DIR, "logs")
    files.sort(key=lambda f: (0 if f["origin"] == "samples" else 1, -f["mtime"]))
    return files


def _ensure_demo() -> str:
    os.makedirs(SAMPLES, exist_ok=True)
    path = os.path.join(SAMPLES, "demo_quarter_turn.log")
    if not os.path.isfile(path) or os.path.getsize(path) < 1000:
        write_demo(path)
    return path


def _put_session(log: Any, label: str) -> str:
    global _active_id
    sid = uuid.uuid4().hex[:12]
    with _sessions_lock:
        _sessions[sid] = {"id": sid, "label": label, "log": log}
        _active_id = sid
        # Keep at most 4 parsed logs in memory
        if len(_sessions) > 4:
            oldest = next(k for k in _sessions if k != sid)
            _sessions.pop(oldest, None)
    return sid


def _get_session(sid: Optional[str] = None) -> Optional[Dict[str, Any]]:
    with _sessions_lock:
        if sid and sid in _sessions:
            return _sessions[sid]
        if _active_id and _active_id in _sessions:
            return _sessions[_active_id]
        return None


def load_path(path: str) -> Dict[str, Any]:
    if not os.path.isfile(path):
        raise FileNotFoundError(path)
    # Stay inside the repo or samples / tmp uploads
    log = parse_log(path)
    sid = _put_session(log, os.path.basename(path))
    meta = log.to_meta()
    meta["session_id"] = sid
    meta["label"] = os.path.basename(path)
    return meta


def _session_meta(sess: Dict[str, Any]) -> Dict[str, Any]:
    log = sess["log"]
    with log.lock:
        meta = log.to_meta()
    meta["session_id"] = sess["id"]
    meta["label"] = sess["label"]
    return meta


def tail_session(sess: Dict[str, Any]) -> Dict[str, Any]:
    log: PrismLog = sess["log"]
    n_before = len(log.samples)
    grew = 0
    if log.path and not log.live:
        grew = log.tail_file()
        if grew < 0:
            # truncated/replaced — full reload
            reloaded = parse_log(log.path)
            with _sessions_lock:
                sess["log"] = reloaded
            log = reloaded
            grew = len(log.samples) - n_before
    t1 = log.samples[-1]["t_s"] if log.samples else 0.0
    t0 = log.samples[0]["t_s"] if log.samples else 0.0
    ch = (log.stats or {}).get("channels") or {}
    slim = {k: ch[k] for k in ("pos_deg", "vel_rad_s", "tau_nm", "power_w") if k in ch}
    return {
        "n_new": max(0, grew),
        "n_samples": len(log.samples),
        "n_events": len(log.events),
        "t0": t0,
        "t1": t1,
        "live": log.live,
        "error": _live.get("error") if sess["id"] == _live.get("session_id") else None,
        "stats": {
            "samples": (log.stats or {}).get("samples", len(log.samples)),
            "duration_s": (log.stats or {}).get("duration_s", t1 - t0),
            "sample_rate_hz": (log.stats or {}).get("sample_rate_hz"),
            "t0": t0,
            "t1": t1,
            "channels": slim,
        },
    }


def _live_loop(ip: str, port: int, sid: str, interval_ms: int, rest: bool, api_key: str) -> None:
    import socket
    import time

    sess = _get_session(sid)
    if not sess:
        _live["error"] = "session disappeared"
        return
    log: PrismLog = sess["log"]
    if rest:
        probe_err = probe_device(ip, api_key)
        if probe_err:
            _live["error"] = probe_err
            _live["ready"].set()
            return
        _start_stream(ip, api_key, interval_ms)
    try:
        sock = socket.create_connection((ip, port), timeout=5.0)
    except OSError as exc:
        _live["error"] = f"TCP {ip}:{port} failed ({exc}). Is streaming enabled on the device?"
        _live["ready"].set()
        return
    _live["connected"] = True
    _live["ready"].set()
    sock.settimeout(0.5)
    buf = b""
    extra_holder: Dict[str, Any] = {}
    poller = None
    if rest:
        from record import RestPoller

        poller = RestPoller(ip, api_key)
        poller.start()
        time.sleep(0.2)
        if poller.config:
            with log.lock:
                log.ingest_obj(poller.config if poller.config.get("rec") else {**poller.config, "rec": "config"})
    try:
        while not _live["stop"].is_set():
            try:
                chunk = sock.recv(4096)
            except socket.timeout:
                continue
            except OSError as exc:
                _live["error"] = str(exc)
                break
            if not chunk:
                _live["error"] = "device closed the stream"
                break
            buf += chunk
            extra = poller.snapshot() if poller else extra_holder
            n_add = 0
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
                    continue
                row = map_stream_packet(pkt, extra)
                with log.lock:
                    if log.ingest_obj(row):
                        n_add += 1
            if n_add:
                with log.lock:
                    log.refresh_stats()
                _live["n"] = len(log.samples)
    finally:
        try:
            sock.close()
        except OSError:
            pass
        if poller:
            poller.stop()


def start_live(ip: str, port: int = 8888, interval_ms: int = 20, rest: bool = True, api_key: str = DEFAULT_API_KEY) -> Dict[str, Any]:
    stop_live()
    log = PrismLog()
    log.live = True
    log.format = "stream-jsonl"
    log.header = {
        "rec": "header",
        "format": "PRISM-LOG",
        "version": "1.0",
        "schema_level": "stream",
        "title": f"Live {ip}",
        "device": {"name": "PRISM", "ip": ip},
        "source": {"kind": "live", "path": f"tcp://{ip}:{port}"},
    }
    sid = _put_session(log, f"live:{ip}")
    _live["stop"] = threading.Event()
    _live["ready"] = threading.Event()
    _live["error"] = None
    _live["connected"] = False
    _live["ip"] = ip
    _live["session_id"] = sid
    _live["n"] = 0
    th = threading.Thread(
        target=_live_loop,
        args=(ip, port, sid, interval_ms, rest, api_key),
        name="prism-live",
        daemon=True,
    )
    _live["thread"] = th
    th.start()
    _live["ready"].wait(timeout=8.0)
    if _live.get("error"):
        raise RuntimeError(_live["error"])
    if not _live.get("connected"):
        raise RuntimeError(f"Timed out connecting to {ip}:{port}")
    meta = _session_meta(_get_session(sid))  # type: ignore[arg-type]
    meta["live"] = True
    return meta


def stop_live() -> None:
    _live["stop"].set()
    th = _live.get("thread")
    if th and th.is_alive():
        th.join(timeout=1.5)
    _live["thread"] = None


class Handler(BaseHTTPRequestHandler):
    server_version = "PRISMLogViewer/1.0"

    def log_message(self, fmt: str, *args: Any) -> None:
        sys.stderr.write("%s - %s\n" % (self.address_string(), fmt % args))

    def _send(self, code: int, body: bytes, content_type: str, extra: Optional[Dict[str, str]] = None) -> None:
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        if extra:
            for k, v in extra.items():
                self.send_header(k, v)
        self.end_headers()
        self.wfile.write(body)

    def _json(self, obj: Any, code: int = 200) -> None:
        body = json.dumps(obj, default=str, separators=(",", ":")).encode("utf-8")
        self._send(code, body, "application/json; charset=utf-8")

    def _err(self, code: int, msg: str) -> None:
        self._json({"error": msg}, code)

    def do_GET(self) -> None:
        parsed = urlparse(self.path)
        path = unquote(parsed.path)
        qs = parse_qs(parsed.query)

        if path in ("/", "/index.html"):
            return self._static("index.html")
        if path.startswith("/static/"):
            return self._static(path[len("/static/") :])
        # also allow /app.js etc from the static dir
        rel = path.lstrip("/")
        if rel in ("app.js", "style.css"):
            return self._static(rel)

        if path == "/api/catalog":
            return self._json({"channels": CHANNELS})
        if path == "/api/files":
            return self._json({"files": list_files()})
        if path == "/api/health":
            return self._json({"ok": True})
        if path == "/api/device_defaults":
            return self._json({
                "ip": DEFAULT_DEVICE_IP,
                "api_key": DEFAULT_API_KEY,
                "port": 8888,
            })

        if path == "/api/load":
            target = (qs.get("path") or [None])[0]
            if not target:
                return self._err(400, "missing path")
            target = os.path.abspath(target)
            allowed = (
                os.path.abspath(SAMPLES),
                os.path.abspath(LOGS_DIR),
            )
            if not any(target == a or target.startswith(a + os.sep) for a in allowed):
                return self._err(403, "path not allowed")
            try:
                return self._json(load_path(target))
            except Exception as exc:
                return self._err(400, str(exc))

        if path == "/api/demo":
            p = _ensure_demo()
            return self._json(load_path(p))

        if path == "/api/meta":
            sess = _get_session((qs.get("id") or [None])[0])
            if not sess:
                return self._err(404, "no session")
            return self._json(_session_meta(sess))

        if path == "/api/events":
            sess = _get_session((qs.get("id") or [None])[0])
            if not sess:
                return self._err(404, "no session")
            with sess["log"].lock:
                events = list(sess["log"].events)
            return self._json({"events": events})

        if path == "/api/tail":
            sess = _get_session((qs.get("id") or [None])[0])
            if not sess:
                return self._err(404, "no session")
            return self._json(tail_session(sess))

        if path == "/api/live/status":
            return self._json(
                {
                    "active": bool(_live.get("thread") and _live["thread"].is_alive()),
                    "session_id": _live.get("session_id"),
                    "ip": _live.get("ip"),
                    "error": _live.get("error"),
                    "n": _live.get("n", 0),
                }
            )

        if path == "/api/series":
            sess = _get_session((qs.get("id") or [None])[0])
            if not sess:
                return self._err(404, "no session")
            keys = [k for k in (qs.get("keys") or [""])[0].split(",") if k]
            if not keys:
                keys = sess["log"].to_meta().get("defaults") or ["pos_deg", "vel_rad_s", "tau_nm"]
            t0 = float(qs["t0"][0]) if "t0" in qs else None
            t1 = float(qs["t1"][0]) if "t1" in qs else None
            n = int((qs.get("n") or ["4000"])[0])
            n = max(64, min(n, 20000))
            with sess["log"].lock:
                series = series_bundle(sess["log"].samples, keys, t0, t1, n)
            return self._json({"series": series})

        if path == "/api/sample_at":
            sess = _get_session((qs.get("id") or [None])[0])
            if not sess:
                return self._err(404, "no session")
            try:
                t = float((qs.get("t") or ["0"])[0])
            except ValueError:
                return self._err(400, "bad t")
            with sess["log"].lock:
                samples = sess["log"].samples
                if not samples:
                    return self._json({"sample": None})
                lo, hi = 0, len(samples) - 1
                while lo < hi:
                    mid = (lo + hi) // 2
                    if samples[mid].get("t_s", 0) < t:
                        lo = mid + 1
                    else:
                        hi = mid
                i = lo
                if i > 0 and abs(samples[i - 1].get("t_s", 0) - t) < abs(samples[i].get("t_s", 0) - t):
                    i = i - 1
                sample = dict(samples[i])
            return self._json({"index": i, "sample": sample})

        return self._err(404, "not found")

    def do_POST(self) -> None:
        parsed = urlparse(self.path)
        path = parsed.path
        length = int(self.headers.get("Content-Length") or 0)
        if length > MAX_UPLOAD:
            return self._err(413, "file too large")
        body = self.rfile.read(length) if length else b""

        if path == "/api/live/start":
            try:
                payload = json.loads(body.decode("utf-8") or "{}")
            except json.JSONDecodeError:
                return self._err(400, "invalid json")
            ip = (payload.get("ip") or "").strip()
            if not ip:
                return self._err(400, "missing ip")
            port = int(payload.get("port") or 8888)
            interval_ms = int(payload.get("interval_ms") or 10)
            rest = bool(payload.get("rest", True))
            api_key = payload.get("api_key") or DEFAULT_API_KEY
            try:
                meta = start_live(ip, port=port, interval_ms=interval_ms, rest=rest, api_key=api_key)
            except Exception as exc:
                return self._err(400, str(exc))
            return self._json(meta)

        if path == "/api/live/stop":
            stop_live()
            return self._json({"ok": True})

        if path == "/api/upload":
            ctype = self.headers.get("Content-Type") or ""
            filename = "upload.log"
            data = body
            if "multipart/form-data" in ctype:
                filename, data = _parse_multipart(ctype, body)
            else:
                filename = self.headers.get("X-Filename") or filename
            if not data:
                return self._err(400, "empty upload")
            try:
                log = parse_bytes(data, filename)
            except Exception as exc:
                return self._err(400, str(exc))
            sid = _put_session(log, filename)
            meta = log.to_meta()
            meta["session_id"] = sid
            meta["label"] = filename
            return self._json(meta)

        return self._err(404, "not found")

    def _static(self, name: str) -> None:
        # prevent path escape
        name = name.lstrip("/").replace("\\", "/")
        if ".." in name.split("/"):
            return self._err(403, "bad path")
        path = os.path.join(STATIC, name)
        if not os.path.isfile(path):
            return self._err(404, "not found")
        ctype = mimetypes.guess_type(path)[0] or "application/octet-stream"
        if name.endswith(".js"):
            ctype = "application/javascript; charset=utf-8"
        elif name.endswith(".css"):
            ctype = "text/css; charset=utf-8"
        elif name.endswith(".html"):
            ctype = "text/html; charset=utf-8"
        with open(path, "rb") as fh:
            data = fh.read()
        self._send(200, data, ctype)

    def do_HEAD(self) -> None:
        self.do_GET()


def _parse_multipart(ctype: str, body: bytes) -> Tuple[str, bytes]:
    bound = None
    for part in ctype.split(";"):
        part = part.strip()
        if part.startswith("boundary="):
            bound = part.split("=", 1)[1].strip().strip('"')
    if not bound:
        return "upload.log", body
    token = b"--" + bound.encode("ascii", "ignore")
    chunks = body.split(token)
    filename = "upload.log"
    data = b""
    for chunk in chunks:
        if not chunk or chunk in (b"--", b"--\r\n"):
            continue
        if chunk.startswith(b"\r\n"):
            chunk = chunk[2:]
        if b"\r\n\r\n" not in chunk:
            continue
        head, payload = chunk.split(b"\r\n\r\n", 1)
        if payload.endswith(b"\r\n"):
            payload = payload[:-2]
        if payload.endswith(b"--"):
            payload = payload[:-2]
        header_txt = head.decode("utf-8", "replace")
        if "filename=" in header_txt:
            # filename="foo.log"
            for line in header_txt.split("\r\n"):
                if "filename=" in line:
                    fn = line.split("filename=", 1)[1].strip().strip('"')
                    if fn:
                        filename = os.path.basename(fn)
            data = payload
            break
    return filename, data


def main() -> None:
    ap = argparse.ArgumentParser(description="PRISM log viewer (localhost)")
    ap.add_argument("--host", default="127.0.0.1")
    ap.add_argument("--port", type=int, default=8742)
    ap.add_argument("--open", action="store_true", help="Open a browser tab")
    ap.add_argument("--file", help="Preload this log")
    args = ap.parse_args()

    os.makedirs(SAMPLES, exist_ok=True)
    _ensure_demo()
    if args.file:
        load_path(os.path.abspath(args.file))

    httpd = ThreadingHTTPServer((args.host, args.port), Handler)
    url = f"http://{args.host}:{args.port}/"
    print(f"PRISM log viewer  {url}")
    print("  samples:", SAMPLES)
    print("  logs:   ", LOGS_DIR)
    print("  Ctrl-C to stop")
    if args.open:
        threading.Timer(0.4, lambda: webbrowser.open(url)).start()
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nbye")
        httpd.server_close()


if __name__ == "__main__":
    main()
