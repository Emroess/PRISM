#!/usr/bin/env python3
"""
Guided robot-mode Coulomb hysteresis sweep at b=τc=0.8.

For each hyst value: rest (hands off), slow mid-range sweep, faster sweep.
Logs Ethernet telemetry and scores vibration (ω sign-flips, |τ|, pos travel).
"""
from __future__ import annotations

import csv
import json
import math
import socket
import sys
import time
from datetime import datetime
from pathlib import Path

import requests

HOST = "10.0.1.15"
API = f"http://{HOST}:8080/api/v1"
KEY = "steve-valve-2025"
HEADERS = {"X-API-Key": KEY, "Content-Type": "application/json"}
LOG = Path("/home/uw/Documents/PRISM/logs")
PROMPT = LOG / "LEVER_TEST_PROMPT.txt"

HYST_VALUES = [0.0, 0.08, 0.25, 0.50, 1.00]
B = 0.8
TC = 0.8


def say(msg: str) -> None:
    print(msg, flush=True)
    try:
        PROMPT.write_text(msg + "\n")
    except OSError:
        pass


def api_get(path: str) -> dict:
    r = requests.get(f"{API}{path}", headers=HEADERS, timeout=3)
    r.raise_for_status()
    return r.json()


def api_post(path: str, body: dict, timeout: float = 6.0) -> dict:
    r = requests.post(f"{API}{path}", headers=HEADERS, json=body, timeout=timeout)
    if r.status_code >= 400:
        raise RuntimeError(f"POST {path} -> {r.status_code} {r.text}")
    return r.json()


def banner(title: str, *lines: str) -> None:
    say("\n" + "!" * 72)
    say(f">>> {title}")
    for ln in lines:
        say(f"    {ln}")
    say("!" * 72)


def countdown(seconds: int) -> None:
    for i in range(seconds, 0, -1):
        say(f"    {i}...")
        time.sleep(1)


def zc_count(xs: list[float]) -> int:
    n = 0
    prev = 0.0
    started = False
    for x in xs:
        if not started:
            if x != 0:
                prev = x
                started = True
            continue
        if x == 0:
            continue
        if (prev > 0) != (x > 0):
            n += 1
        prev = x
    return n


def absmean(xs: list[float]) -> float:
    return sum(abs(x) for x in xs) / len(xs) if xs else 0.0


def mean(xs: list[float]) -> float:
    return sum(xs) / len(xs) if xs else 0.0


def stdev(xs: list[float]) -> float:
    if len(xs) < 2:
        return 0.0
    m = mean(xs)
    return math.sqrt(sum((x - m) ** 2 for x in xs) / (len(xs) - 1))


def pp(xs: list[float]) -> float:
    return (max(xs) - min(xs)) if xs else 0.0


def arm() -> None:
    say("=== ARM: robot mode, b=0.8, τc=0.8 ===")
    od = api_get("/odrive")
    say(f"  odrive axis_error=0x{int(od.get('axis_error') or 0):08X} "
        f"state={od.get('axis_state')} Vbus={od.get('bus_voltage')}")
    if int(od.get("axis_error") or 0) != 0:
        say("  clearing ODrive errors")
        api_post("/odrive", {"action": "clear"})
        time.sleep(0.4)
    api_post("/interaction", {"mode": "robot"})
    api_post("/config", {"viscous": B, "coulomb": TC, "coulomb_hyst": 0.0})
    api_post("/stream", {"action": "start", "interval_ms": 10})
    time.sleep(0.3)
    st = api_get("/status")
    if int(st.get("status") or 0) != 2:
        say("  valve start")
        api_post("/control", {"action": "start"})
        time.sleep(0.7)
    st = api_get("/status")
    od = api_get("/odrive")
    cfg = api_get("/config")
    say(f"  status={st}")
    say(f"  config={cfg}")
    say(f"  odrive axis_error=0x{int(od.get('axis_error') or 0):08X} "
        f"state={od.get('axis_state')}")
    if int(st.get("status") or 0) != 2:
        raise SystemExit("FATAL: valve not RUNNING")
    if int(od.get("axis_error") or 0) != 0:
        raise SystemExit("FATAL: ODrive still in error — fix MISSING_ESTIMATE/spinout first")


class Stream:
    def __init__(self) -> None:
        self.sock = socket.create_connection((HOST, 8888), timeout=5)
        self.sock.settimeout(0.25)
        self.buf = b""
        self.rows: list[dict] = []
        self.phase = "boot"
        ts = datetime.now().strftime("%Y%m%d_%H%M%S")
        self.ts = ts
        self.jsonl = open(LOG / f"hyst_sweep_{ts}.jsonl", "w", buffering=1)
        self.csvp = LOG / f"hyst_sweep_{ts}.csv"
        self.csvf = open(self.csvp, "w", newline="", buffering=1)
        self.fields = [
            "wall_time", "phase", "hyst", "seq", "enc_seq", "position_deg",
            "omega_rad_s", "torque_nm", "quiet", "loop_time_us", "enc_to_tx_us",
            "new_enc", "status",
        ]
        self.wri = csv.DictWriter(self.csvf, fieldnames=self.fields, extrasaction="ignore")
        self.wri.writeheader()
        end = time.time() + 1.0
        while time.time() < end:
            try:
                c = self.sock.recv(4096)
            except socket.timeout:
                break
            if not c:
                break
            self.buf += c
            if b"\n" in self.buf:
                self.buf = b"\n".join(self.buf.split(b"\n")[1:])
                break

    def drain(self, seconds: float, hyst: float) -> None:
        t_end = time.time() + seconds
        while time.time() < t_end:
            try:
                chunk = self.sock.recv(8192)
            except socket.timeout:
                chunk = b""
            if chunk:
                self.buf += chunk
            while b"\n" in self.buf:
                line, self.buf = self.buf.split(b"\n", 1)
                line = line.strip()
                if not line:
                    continue
                try:
                    s = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if "position_deg" not in s and "seq" not in s:
                    continue
                wall = time.time()
                row = {
                    "wall_time": f"{wall:.6f}",
                    "phase": self.phase,
                    "hyst": hyst,
                    "seq": s.get("seq"),
                    "enc_seq": s.get("enc_seq"),
                    "position_deg": s.get("position_deg"),
                    "omega_rad_s": s.get("omega_rad_s"),
                    "torque_nm": s.get("torque_nm"),
                    "quiet": s.get("quiet"),
                    "loop_time_us": s.get("loop_time_us"),
                    "enc_to_tx_us": s.get("enc_to_tx_us"),
                    "new_enc": s.get("new_enc"),
                    "status": s.get("status"),
                }
                self.jsonl.write(json.dumps(s) + "\n")
                self.wri.writerow(row)
                try:
                    self.rows.append(dict(
                        wt=wall, phase=self.phase, hyst=hyst,
                        pos=float(s.get("position_deg") or 0),
                        omega=float(s.get("omega_rad_s") or 0),
                        tau=float(s.get("torque_nm") or 0),
                        quiet=bool(s.get("quiet")),
                        status=str(s.get("status") or ""),
                    ))
                except (TypeError, ValueError):
                    pass

    def close(self) -> None:
        try:
            self.sock.close()
        except OSError:
            pass
        self.jsonl.close()
        self.csvf.close()


def score_seg(seg: list[dict]) -> dict:
    if not seg:
        return dict(n=0)
    pos = [r["pos"] for r in seg]
    om = [r["omega"] for r in seg]
    tau = [r["tau"] for r in seg]
    dt = max(1e-3, seg[-1]["wt"] - seg[0]["wt"])
    zc = zc_count(om)
    zc_hz = zc / dt
    pos_pp = pp(pos)
    t_abs = absmean(tau)
    t_std = stdev(tau)
    w_abs = absmean(om)
    # buzz: lots of reversals, little travel, torque still large
    buzz = zc_hz * t_abs / (1.0 + pos_pp)
    return dict(
        n=len(seg), dt=dt, pos_pp=pos_pp, pos_mn=mean(pos),
        w_abs=w_abs, w_pp=pp(om), zc=zc, zc_hz=zc_hz,
        t_abs=t_abs, t_std=t_std, t_max=max(abs(x) for x in tau),
        buzz=buzz,
        err=any(r["status"] == "ERROR" for r in seg),
    )


def main() -> int:
    LOG.mkdir(parents=True, exist_ok=True)
    arm()
    stream = Stream()
    events: list[dict] = []

    def hold(phase: str, hyst: float, seconds: float, note: str, human: bool) -> None:
        stream.phase = f"h{hyst:g}_{phase}"
        if human:
            banner(
                f"HYST={hyst:g}  YOUR TURN — {note}",
                "Stay MID-RANGE. Do not slam 0°/90° (0.8/0.8 can spinout).",
            )
            countdown(5)
            say(f"    >>> GO NOW — {note}")
        else:
            banner(f"HYST={hyst:g}  HANDS OFF — {note}", "Do not touch the lever.")
        t0 = time.time()
        events.append(dict(phase=stream.phase, hyst=hyst, kind=phase,
                           t0=t0, human=human, note=note))
        while time.time() - t0 < seconds:
            stream.drain(0.1, hyst)
            st = None
            # cheap error peek every ~1s via last stream status
        events[-1]["t1"] = time.time()
        od = api_get("/odrive")
        vs = api_get("/status")
        events[-1]["axis_error"] = int(od.get("axis_error") or 0)
        events[-1]["valve_status"] = int(vs.get("status") or 0)
        say(f"    === end {stream.phase}  odrive_err=0x{events[-1]['axis_error']:X} "
            f"valve={vs.get('status')} ===")
        if events[-1]["axis_error"] != 0 or events[-1]["valve_status"] == 3:
            say("    !! FAULT — clearing and restarting valve for next window")
            try:
                api_post("/control", {"action": "stop"})
            except Exception:
                pass
            time.sleep(0.3)
            try:
                api_post("/odrive", {"action": "clear"})
                time.sleep(0.4)
                api_post("/control", {"action": "start"})
                time.sleep(0.6)
            except Exception as e:
                say(f"    restart failed: {e}")

    banner(
        "GET TO THE QUARTER-TURN LEVER NOW",
        "ROBOT mode, viscous=0.8, coulomb=0.8 (STRONG).",
        "Sweep hyst = 0, 0.08, 0.25, 0.50, 1.00 rad/s.",
        "Each value: HANDS OFF rest → SLOW mid-range → FASTER mid-range.",
        "Do not slam end stops. If it trips, keep hands off until the next prompt.",
    )
    countdown(15)

    for hyst in HYST_VALUES:
        say(f"\n##### setting coulomb_hyst={hyst} #####")
        api_post("/config", {"coulomb_hyst": hyst})
        time.sleep(0.15)
        hold("rest", hyst, 5, "leave still, hands off — we measure rest buzz", human=False)
        hold("slow", hyst, 10,
             "SLOWLY sweep back and forth in MID-RANGE (~2–4 s per pass, 10 s)",
             human=True)
        hold("fast", hyst, 7,
             "FASTER mid-range sweeps (not a slam, not into the stops)",
             human=True)

    say("\n>>> STOP")
    try:
        api_post("/control", {"action": "stop"})
        api_post("/odrive", {"action": "disable"})
        api_post("/stream", {"action": "stop"})
    except Exception as e:
        say(f"stop warn: {e}")
    stream.drain(0.3, HYST_VALUES[-1])
    stream.close()

    # --- analysis ---
    lines = []
    def out(s=""):
        print(s, flush=True)
        lines.append(s)

    out(f"HYST SWEEP  robot  b={B}  τc={TC}  stream={stream.csvp.name}  N={len(stream.rows)}")
    out(f"{'phase':<22} {'N':>5} {'pos_pp':>8} {'|ω|':>7} {'ω_zc/s':>8} "
        f"{'|τ|':>7} {'τ_std':>7} {'buzz':>8}  flag")
    table = []
    for e in events:
        seg = [r for r in stream.rows if e["t0"] <= r["wt"] <= e["t1"]]
        sc = score_seg(seg)
        e["stats"] = sc
        flag = "FAULT" if e.get("axis_error") or sc.get("err") else ""
        if sc.get("n", 0) == 0:
            line = f"{e['phase']:<22} NO DATA"
        else:
            line = (f"{e['phase']:<22} {sc['n']:5d} {sc['pos_pp']:8.2f} "
                    f"{sc['w_abs']:7.3f} {sc['zc_hz']:8.2f} "
                    f"{sc['t_abs']:7.3f} {sc['t_std']:7.3f} {sc['buzz']:8.3f}  {flag}")
        out(line)
        table.append((e, sc))

    out()
    out("=== REST buzz (hands off) vs hyst — lower is quieter ===")
    out(f"{'hyst':>8} {'pos_pp':>8} {'ω_zc/s':>8} {'|τ|':>8} {'buzz':>8}")
    for e, sc in table:
        if e["kind"] != "rest" or sc.get("n", 0) == 0:
            continue
        out(f"{e['hyst']:8.2f} {sc['pos_pp']:8.2f} {sc['zc_hz']:8.2f} "
            f"{sc['t_abs']:8.3f} {sc['buzz']:8.3f}")

    out()
    out("=== SLOW motion vs hyst ===")
    out(f"{'hyst':>8} {'pos_pp':>8} {'ω_zc/s':>8} {'|τ|':>8} {'τ_std':>8} {'buzz':>8}")
    for e, sc in table:
        if e["kind"] != "slow" or sc.get("n", 0) == 0:
            continue
        out(f"{e['hyst']:8.2f} {sc['pos_pp']:8.2f} {sc['zc_hz']:8.2f} "
            f"{sc['t_abs']:8.3f} {sc['t_std']:8.3f} {sc['buzz']:8.3f}")

    out()
    out("=== FAST motion vs hyst ===")
    out(f"{'hyst':>8} {'pos_pp':>8} {'ω_zc/s':>8} {'|τ|':>8} {'τ_std':>8} {'buzz':>8}")
    for e, sc in table:
        if e["kind"] != "fast" or sc.get("n", 0) == 0:
            continue
        out(f"{e['hyst']:8.2f} {sc['pos_pp']:8.2f} {sc['zc_hz']:8.2f} "
            f"{sc['t_abs']:8.3f} {sc['t_std']:8.3f} {sc['buzz']:8.3f}")

    out()
    out("buzz ≈ (ω zero-crossings/s) × |τ| / (1+pos_pp). Rest: high buzz + tiny pos_pp = chatter.")
    out("Robot 0.8/0.8 has no quiet gate; hyst=0 is the baseline telegraph.")

    anp = LOG / f"hyst_sweep_analysis_{stream.ts}.txt"
    anp.write_text("\n".join(lines) + "\n")
    say(f"wrote {anp}")
    say("DONE")
    return 0


if __name__ == "__main__":
    sys.exit(main())
