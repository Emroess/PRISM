#!/usr/bin/env python3
"""
Guided slow / fast / still lever test against the live PRISM plant.

Keeps the currently loaded physics + interaction mode. Arms ODrive, starts
the 1 kHz haptic loop, records Ethernet telemetry, then writes analysis.
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
STREAM_PORT = 8888
LOG = Path("/home/uw/Documents/PRISM/logs")
PROMPT = LOG / "LEVER_TEST_PROMPT.txt"
HEADERS = {"X-API-Key": KEY, "Content-Type": "application/json"}

TS = datetime.now().strftime("%Y%m%d_%H%M%S")
CSVP = LOG / f"lever_test_{TS}.csv"
JSONL = LOG / f"lever_test_{TS}.jsonl"
EVP = LOG / f"lever_test_events_{TS}.jsonl"
ANP = LOG / f"lever_test_analysis_{TS}.txt"

FIELDS = [
    "wall_time",
    "timestamp_ms",
    "t_us",
    "loop_time_us",
    "seq",
    "position_turns",
    "position_deg",
    "omega_rad_s",
    "torque_nm",
    "filt_torque_nm",
    "status",
    "passivity_mj",
    "quiet",
    "err",
    "hb_age",
    "data_valid",
    "phase",
]


def say(msg: str) -> None:
    print(msg, flush=True)
    try:
        PROMPT.write_text(msg + "\n")
    except OSError:
        pass


def api_get(path: str, timeout: float = 3.0) -> dict:
    r = requests.get(f"{API}{path}", headers=HEADERS, timeout=timeout)
    r.raise_for_status()
    return r.json()


def api_post(path: str, body: dict, timeout: float = 5.0) -> dict:
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


def arm_plant() -> dict:
    say("=== ARM PLANT (current config, no preset change) ===")
    cfg = api_get("/config")
    st = api_get("/status")
    od = api_get("/odrive")
    inter = api_get("/interaction")
    hitl = api_get("/hitl")
    say(f"  status={st}")
    say(f"  config={cfg}")
    say(f"  interaction={inter}")
    say(f"  hitl={hitl}")
    say(f"  odrive axis_error=0x{int(od.get('axis_error') or 0):08X} "
        f"axis_state={od.get('axis_state')} Vbus={od.get('bus_voltage')}")

    if hitl.get("enabled"):
        raise SystemExit("HITL is enabled — refusing to drive the physical lever")

    say("  odrive clear")
    api_post("/odrive", {"action": "clear"})
    time.sleep(0.4)
    say("  odrive enable")
    try:
        api_post("/odrive", {"action": "enable"})
    except Exception as e:
        say(f"  warn enable: {e}")
    time.sleep(0.8)

    say("  stream start 10 ms")
    api_post("/stream", {"action": "start", "interval_ms": 10})
    time.sleep(0.3)

    st = api_get("/status")
    if int(st.get("status") or 0) == 2:
        say("  valve already RUNNING — leaving it")
    else:
        say("  valve start")
        started = api_post("/control", {"action": "start"})
        say(f"  start resp={started}")
        time.sleep(0.6)

    st = api_get("/status")
    od = api_get("/odrive")
    say(f"  post-start status={st}")
    say(f"  post-start odrive axis_error=0x{int(od.get('axis_error') or 0):08X} "
        f"axis_state={od.get('axis_state')}")
    if int(st.get("status") or 0) != 2:
        raise SystemExit(f"FATAL: valve not RUNNING (status={st.get('status')})")
    return {"config": cfg, "interaction": inter, "status": st, "odrive": od}


class Stream:
    def __init__(self) -> None:
        self.sock = socket.create_connection((HOST, STREAM_PORT), timeout=5)
        self.sock.settimeout(0.25)
        self.buf = b""
        self.rows: list[dict] = []
        self.phase = "boot"
        self.jsonl = open(JSONL, "w", buffering=1)
        self.csvf = open(CSVP, "w", newline="", buffering=1)
        self.writer = csv.DictWriter(self.csvf, fieldnames=FIELDS, extrasaction="ignore")
        self.writer.writeheader()
        # drop partial first line
        end = time.time() + 1.2
        while time.time() < end:
            try:
                chunk = self.sock.recv(4096)
            except socket.timeout:
                break
            if not chunk:
                break
            self.buf += chunk
            if b"\n" in self.buf:
                self.buf = b"\n".join(self.buf.split(b"\n")[1:])
                break

    def drain(self, seconds: float) -> None:
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
                    sample = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if "position_deg" not in sample and "seq" not in sample:
                    continue
                wall = time.time()
                row = {k: sample.get(k) for k in FIELDS if k not in ("wall_time", "phase")}
                row["wall_time"] = f"{wall:.6f}"
                row["phase"] = self.phase
                self.jsonl.write(json.dumps(sample) + "\n")
                self.writer.writerow(row)
                try:
                    self.rows.append(
                        dict(
                            wt=wall,
                            pos=float(sample.get("position_deg") or 0),
                            omega=float(sample.get("omega_rad_s") or 0),
                            tau=float(sample.get("torque_nm") or 0),
                            ftau=float(sample.get("filt_torque_nm") or sample.get("torque_nm") or 0),
                            quiet=bool(sample.get("quiet")),
                            pass_mj=float(sample.get("passivity_mj") or 0),
                            status=str(sample.get("status") or ""),
                            phase=self.phase,
                        )
                    )
                except (TypeError, ValueError):
                    pass

    def close(self) -> None:
        try:
            self.sock.close()
        except OSError:
            pass
        self.jsonl.close()
        self.csvf.close()


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


def zc_rate(xs: list[float]) -> float:
    """Zero-crossings per second, assuming ~100 Hz samples if dt unknown."""
    if len(xs) < 3:
        return 0.0
    n = 0
    prev = xs[0]
    for x in xs[1:]:
        if prev == 0:
            prev = x
            continue
        if x == 0:
            continue
        if (prev > 0) != (x > 0):
            n += 1
        prev = x
    return n


def analyze(rows: list[dict], events: list[dict], meta: dict) -> str:
    cfg = meta["config"]
    b = float(cfg.get("viscous") or 0)
    tc = float(cfg.get("coulomb") or 0)
    kw = float(cfg.get("wall_stiffness") or 0)
    cw = float(cfg.get("wall_damping") or 0)
    closed = float(cfg.get("closed_pos") or 0)
    opened = float(cfg.get("open_pos") or 90)
    mode = (meta.get("interaction") or {}).get("mode", "?")
    dpt = 360.0
    p0 = 0.05
    robot = mode == "robot"

    lines: list[str] = []
    lines.append(f"LEVER MOVE TEST {TS}")
    lines.append(f"stream={CSVP.name}  N={len(rows)}")
    lines.append(
        f"mode={mode}  b={b}  tc={tc}  kw={kw}  cw={cw}  travel=[{closed},{opened}]"
    )
    lines.append("")
    hdr = (
        f"{'phase':<14} {'N':>5} {'pos_pp':>8} {'pos_mn':>7} {'|ω|mn':>8} "
        f"{'ω_pp':>8} {'ω_zc':>5} {'|τ|mn':>8} {'τ_std':>8} {'|τ|mx':>8} "
        f"{'q%':>5} {'wall%':>6} flag"
    )
    lines.append(hdr)

    def wall_pen(pos_deg: float) -> float:
        th = pos_deg / dpt
        off = closed / dpt
        on = opened / dpt
        left = min(0.0, th - off)
        right = max(0.0, th - on)
        return left + right

    def predict_tau(pos: float, omega: float, quiet: bool) -> tuple[float, float, float, float]:
        pen = wall_pen(pos)
        in_wall = abs(pen) >= 1e-6
        b_use = 0.0 if (quiet or (in_wall and not robot)) else b
        tc_use = 0.0 if (quiet or in_wall) else tc
        if robot:
            # hard sign, full Coulomb, no quiet
            b_use = 0.0 if False else (b if not in_wall or robot else 0.0)
            if in_wall:
                tc_use = 0.0
            else:
                tc_use = tc
            cscale = 1.0
            visc = -b_use * max(-12.0, min(12.0, omega))
            fric = -tc_use * (1.0 if omega > 0 else (-1.0 if omega < 0 else 0.0))
        else:
            visc = -b_use * max(-12.0, min(12.0, omega))
            fric = 0.0  # human Coulomb is speed-scheduled; skip exact
        # soft sat
        fs = visc + fric
        free_cap = 2.43
        a = abs(fs)
        if a > 1e-9:
            fs = fs * (free_cap / (free_cap + a))
        # wall
        tau_w = 0.0
        if in_wall:
            pen_abs = abs(pen)
            pen_soft = pen / (1.0 + pen_abs / p0)
            k_eff = kw
            c_eff = 0.0 if robot else cw
            omega_turns = (omega * (180.0 / math.pi)) / dpt
            tau_w = (-k_eff * pen_soft) + (-c_eff * omega_turns)
            if robot and tc > 0 and abs(tau_w) < tc:
                tau_w = -tc if pen_soft >= 0 else tc
            tau_w = max(-2.5, min(2.5, tau_w))
        return visc, fric, tau_w, fs + tau_w

    for e in events:
        segs = [r for r in rows if e["t0"] <= r["wt"] <= e["t1"]]
        if not segs:
            lines.append(f"{e['phase']:<14} NO DATA")
            continue
        pos = [r["pos"] for r in segs]
        om = [r["omega"] for r in segs]
        tau = [r["tau"] for r in segs]
        q = sum(1 for r in segs if r["quiet"]) / len(segs)
        wall_frac = sum(1 for r in segs if abs(wall_pen(r["pos"])) >= 1e-6) / len(segs)
        moved = pp(pos) > 3.0 or absmean(om) > 0.15
        last = [r for r in segs if r["wt"] >= e["t1"] - 1.0] or segs[-20:]
        still_ok = absmean([r["omega"] for r in last]) < 0.12 and pp([r["pos"] for r in last]) < 2.0
        if e.get("expect") == "still":
            flag = "STILL" if still_ok else "MOVING"
        elif e.get("expect") == "slow":
            flag = "SLOW" if moved and absmean(om) < 2.5 else ("FAST?" if moved else "MISS")
        elif e.get("expect") == "fast":
            flag = "FAST" if moved and max(abs(x) for x in om) > 1.5 else ("SLOW?" if moved else "MISS")
        else:
            flag = "OK" if moved else "no-move"
        line = (
            f"{e['phase']:<14} {len(segs):5d} {pp(pos):8.2f} {mean(pos):7.1f} "
            f"{absmean(om):8.3f} {pp(om):8.3f} {zc_rate(om):5.0f} "
            f"{absmean(tau):8.3f} {stdev(tau):8.3f} {max(abs(x) for x in tau):8.3f} "
            f"{100*q:4.0f}% {100*wall_frac:5.0f}% {flag}"
        )
        lines.append(line)
        e["stats"] = dict(
            n=len(segs),
            pos_pp=pp(pos),
            pos_mean=mean(pos),
            w_abs=absmean(om),
            w_pp=pp(om),
            w_zc=zc_rate(om),
            t_abs=absmean(tau),
            t_std=stdev(tau),
            t_max=max(abs(x) for x in tau),
            quiet=q,
            wall=wall_frac,
            flag=flag,
        )

        # 1-second bins
        t0 = e["t0"]
        for sec in range(0, int(e["t1"] - t0) + 1):
            binr = [r for r in segs if sec <= r["wt"] - t0 < sec + 1]
            if not binr:
                continue
            lines.append(
                f"    +{sec:02d}s |ω|={absmean([r['omega'] for r in binr]):.3f} "
                f"ω=[{min(x['omega'] for x in binr):+.2f},{max(x['omega'] for x in binr):+.2f}] "
                f"|τ|={absmean([r['tau'] for r in binr]):.3f} "
                f"pos=[{min(x['pos'] for x in binr):.1f},{max(x['pos'] for x in binr):.1f}] "
                f"q%={100*sum(1 for r in binr if r['quiet'])/len(binr):.0f} "
                f"wall%={100*sum(1 for r in binr if abs(wall_pen(r['pos']))>=1e-6)/len(binr):.0f}"
            )

        # torque vs predicted, and sign-flip chatter
        tau_err = []
        visc_m, fric_m, wall_m = [], [], []
        for r in segs:
            v, f, w, pred = predict_tau(r["pos"], r["omega"], r["quiet"])
            tau_err.append(r["tau"] - pred)
            visc_m.append(abs(v))
            fric_m.append(abs(f))
            wall_m.append(abs(w))
        lines.append(
            f"    terms |visc|={mean(visc_m):.3f} |coulomb|={mean(fric_m):.3f} "
            f"|wall|={mean(wall_m):.3f}  τ-pred_err mn={mean(tau_err):+.3f} "
            f"std={stdev(tau_err):.3f}"
        )
        # torque vs omega correlation (resistive should be negative)
        if len(om) > 5:
            mo, mt = mean(om), mean(tau)
            num = sum((o - mo) * (t - mt) for o, t in zip(om, tau))
            den = math.sqrt(sum((o - mo) ** 2 for o in om) * sum((t - mt) ** 2 for t in tau))
            corr = num / den if den > 1e-9 else 0.0
            same_sign = sum(1 for o, t in zip(om, tau) if o * t > 0) / len(om)
            lines.append(
                f"    corr(ω,τ)={corr:+.3f}  active_same_sign={100*same_sign:.0f}%  "
                f"(resistive → corr negative, same_sign ~0)"
            )

    # rest chatter diagnostic
    lines.append("")
    lines.append("=== REST CHATTER (still phases) ===")
    stills = [e for e in events if e.get("expect") == "still"]
    for e in stills:
        stt = e.get("stats") or {}
        lines.append(
            f"  {e['phase']}: |τ|={stt.get('t_abs', 0):.3f}  τ_std={stt.get('t_std', 0):.3f}  "
            f"ω_zc={stt.get('w_zc', 0):.0f}  quiet={100*stt.get('quiet', 0):.0f}%  "
            f"|ω|={stt.get('w_abs', 0):.3f}"
        )
        if robot and stt.get("t_abs", 0) > 0.15 and stt.get("quiet", 1) < 0.5:
            lines.append(
                "    → Robot-mode Coulomb is ON at rest (no quiet gate, hard sign(ω)). "
                f"Expected |τ| ≈ τc = {tc:.2f} Nm whenever ω noise is non-zero."
            )

    lines.append("")
    lines.append("=== SLOW vs FAST ===")
    slow = next((e for e in events if e.get("expect") == "slow"), None)
    fast = next((e for e in events if e.get("expect") == "fast"), None)
    if slow and fast and "stats" in slow and "stats" in fast:
        s, f = slow["stats"], fast["stats"]
        lines.append(
            f"  slow |ω|={s['w_abs']:.3f} |τ|={s['t_abs']:.3f} τ_std={s['t_std']:.3f} pos_pp={s['pos_pp']:.1f}"
        )
        lines.append(
            f"  fast |ω|={f['w_abs']:.3f} |τ|={f['t_abs']:.3f} τ_std={f['t_std']:.3f} pos_pp={f['pos_pp']:.1f}"
        )
        # viscous should grow with speed; coulomb is speed-independent in robot mode
        lines.append(
            f"  viscous design: τ_v ≈ b·ω  → slow {b*s['w_abs']:.3f} Nm, fast {b*f['w_abs']:.3f} Nm"
        )
        lines.append(
            f"  coulomb design: τ_c = {tc:.3f} Nm constant in robot mode (hard sign)"
        )
        if s["t_std"] > 0.08 and s["w_abs"] < 1.5:
            lines.append(
                "  → Slow-speed torque is noisy/grindy: Coulomb hard-sign is flipping with ω."
            )
        if f["t_abs"] < s["t_abs"] * 0.8 and f["w_abs"] > s["w_abs"] * 1.5:
            lines.append(
                "  → Fast |τ| did not grow with speed as much as viscous predicts "
                "(soft free-space sat and/or passivity tank clipping)."
            )

    lines.append("")
    lines.append("=== INTERPRETATION ===")
    if robot:
        lines.append(
            "Interaction is ROBOT: quiet gate, Coulomb speed schedule, ε smoothing, "
            "output torque LPF, and residual settle are all stripped."
        )
        lines.append(
            "At rest the law is τ ≈ −τc·sign(ω). Encoder noise makes sign(ω) chatter "
            f"so the motor fights itself at about ±{tc:.2f} Nm."
        )
        lines.append(
            "Slow motion feels grindy because Coulomb is 100% even at low speed "
            "(no deadband / no ε). Fast motion is viscous-dominant (τ ≈ −bω − τc) "
            "then soft-saturated."
        )
        lines.append(
            f"Walls: robot stiffness-only −k·pen (k={kw:.0f} Nm/turn) floored at τc, "
            "no wall damper. A stiff k with almost no damping rings at the 0°/90° stops."
        )
    else:
        lines.append(
            "Interaction is HUMAN: quiet gate zeros free-space at rest; Coulomb ramps "
            "in with speed; walls keep spring+damper with exit-kill."
        )
    return "\n".join(lines) + "\n"


def main() -> int:
    LOG.mkdir(parents=True, exist_ok=True)
    (LOG / "ACTIVE_STREAM.txt").write_text(str(CSVP) + "\n")

    meta = arm_plant()
    stream = Stream()
    events: list[dict] = []

    def hold(phase: str, seconds: float, note: str, expect: str, human: bool, action_s: float = 0) -> None:
        stream.phase = phase
        if human:
            banner(
                f"YOUR TURN — {note}",
                f"Countdown, then MOVE for ~{action_s}s.",
                "Stay in the mid-range (do not slam the end stops).",
            )
            countdown(5)
            say(f"    >>> GO NOW — {note}")
        else:
            banner(f"HANDS OFF — {note}", "Do not touch the lever.")
        t0 = time.time()
        events.append(dict(phase=phase, t0=t0, human=human, note=note, expect=expect, action_s=action_s))
        while time.time() - t0 < seconds:
            stream.drain(0.1)
        events[-1]["t1"] = time.time()
        say(f"    === end {phase} ===")

    banner(
        "GET TO THE QUARTER-TURN LEVER NOW",
        "Protocol: STILL → SLOW sweep → STILL → FAST sweep → LEAVE IT STILL.",
        "Range is 0–90° from the pose at valve_start. Stay mid-range.",
        "First still window starts after this countdown.",
        "If you are not at the lever yet: WALK THERE NOW.",
    )
    countdown(18)

    hold("R0_still", 8, "leave the lever still, hands off", expect="still", human=False)
    hold(
        "M1_slow",
        12,
        "SLOWLY sweep the lever back and forth in mid-range (about 2–4 seconds per pass)",
        expect="slow",
        human=True,
        action_s=10,
    )
    hold("R1_still", 7, "let go and leave it wherever it is", expect="still", human=False)
    hold(
        "M2_fast",
        10,
        "QUICKLY sweep the lever back and forth in mid-range (fast shakes, not end-stop slams)",
        expect="fast",
        human=True,
        action_s=8,
    )
    hold("R2_still", 8, "let go completely — leave it still in place", expect="still", human=False)

    say("\n>>> STOP")
    try:
        api_post("/control", {"action": "stop"})
    except Exception as e:
        say(f"  stop warn: {e}")
    time.sleep(0.3)
    try:
        api_post("/odrive", {"action": "disable"})
    except Exception as e:
        say(f"  disable warn: {e}")
    try:
        api_post("/stream", {"action": "stop"})
    except Exception as e:
        say(f"  stream stop warn: {e}")
    stream.drain(0.3)
    stream.close()

    with open(EVP, "w") as f:
        for e in events:
            dump = {k: v for k, v in e.items() if k != "stats"}
            f.write(json.dumps(dump) + "\n")

    summary = analyze(stream.rows, events, meta)
    ANP.write_text(summary)
    say("\n=== RESULTS ===")
    say(summary)
    say(f"wrote {ANP}")
    say("DONE")
    return 0


if __name__ == "__main__":
    sys.exit(main())
