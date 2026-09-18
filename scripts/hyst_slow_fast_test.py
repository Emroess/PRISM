#!/usr/bin/env python3
"""
Guided rest / slow / fast test at current robot physics (b=τc=0.8, hyst=0.25).

Live prompts: logs/LEVER_TEST_PROMPT.txt
"""
from __future__ import annotations

import csv
import json
import math
import socket
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

B = 0.8
TC = 0.8
HYST = 0.25
DEG2RAD = math.pi / 180.0


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


def mean(xs: list[float]) -> float:
    return sum(xs) / len(xs) if xs else 0.0


def absmean(xs: list[float]) -> float:
    return sum(abs(x) for x in xs) / len(xs) if xs else 0.0


def stdev(xs: list[float]) -> float:
    if len(xs) < 2:
        return 0.0
    m = mean(xs)
    return math.sqrt(sum((x - m) ** 2 for x in xs) / (len(xs) - 1))


def pp(xs: list[float]) -> float:
    return (max(xs) - min(xs)) if xs else 0.0


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


def arm() -> dict:
    say("=== ARM: robot  b=0.8  τc=0.8  hyst=0.25 ===")
    od = api_get("/odrive")
    say(f"  odrive axis_error=0x{int(od.get('axis_error') or 0):08X} "
        f"state={od.get('axis_state')} Vbus={od.get('bus_voltage')}")
    if int(od.get("axis_error") or 0) != 0:
        api_post("/odrive", {"action": "clear"})
        time.sleep(0.4)
    api_post("/interaction", {"mode": "robot"})
    api_post("/config", {
        "viscous": B, "coulomb": TC, "coulomb_hyst": HYST,
    })
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
        raise SystemExit("FATAL: ODrive error")
    return {"config": cfg, "status": st, "odrive": od}


class Stream:
    def __init__(self, ts: str) -> None:
        self.sock = socket.create_connection((HOST, 8888), timeout=5)
        self.sock.settimeout(0.25)
        self.buf = b""
        self.rows: list[dict] = []
        self.phase = "boot"
        self.ts = ts
        self.jsonl = open(LOG / f"hyst_slowfast_{ts}.jsonl", "w", buffering=1)
        self.csvp = LOG / f"hyst_slowfast_{ts}.csv"
        self.csvf = open(self.csvp, "w", newline="", buffering=1)
        self.fields = [
            "wall_time", "phase", "seq", "enc_seq", "position_deg",
            "omega_rad_s", "omega_pos", "torque_nm", "loop_time_us",
            "status",
        ]
        self.wri = csv.DictWriter(self.csvf, fieldnames=self.fields, extrasaction="ignore")
        self.wri.writeheader()
        self._prev_pos = None
        self._prev_wt = None
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
                    s = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if "position_deg" not in s and "seq" not in s:
                    continue
                wall = time.time()
                try:
                    pos = float(s.get("position_deg") or 0)
                    om = float(s.get("omega_rad_s") or 0)
                    tau = float(s.get("torque_nm") or 0)
                except (TypeError, ValueError):
                    continue
                om_pos = 0.0
                if self._prev_pos is not None and self._prev_wt is not None:
                    dt = wall - self._prev_wt
                    if 0.004 < dt < 0.05:
                        om_pos = ((pos - self._prev_pos) * DEG2RAD) / dt
                self._prev_pos = pos
                self._prev_wt = wall
                row = {
                    "wall_time": f"{wall:.6f}",
                    "phase": self.phase,
                    "seq": s.get("seq"),
                    "enc_seq": s.get("enc_seq"),
                    "position_deg": pos,
                    "omega_rad_s": om,
                    "omega_pos": om_pos,
                    "torque_nm": tau,
                    "loop_time_us": s.get("loop_time_us"),
                    "status": s.get("status"),
                }
                self.jsonl.write(json.dumps(s) + "\n")
                self.wri.writerow(row)
                self.rows.append(dict(
                    wt=wall, phase=self.phase, pos=pos, omega=om,
                    omega_pos=om_pos, tau=tau,
                    status=str(s.get("status") or ""),
                ))

    def close(self) -> None:
        try:
            self.sock.close()
        except OSError:
            pass
        self.jsonl.close()
        self.csvf.close()


def score(seg: list[dict], hyst: float) -> dict:
    if len(seg) < 5:
        return dict(n=len(seg))
    pos = [r["pos"] for r in seg]
    om = [r["omega"] for r in seg]
    op = [r["omega_pos"] for r in seg]
    tau = [r["tau"] for r in seg]
    dt = max(1e-3, seg[-1]["wt"] - seg[0]["wt"])
    zc_om = zc_count(om)
    zc_op = zc_count(op)
    zc_tau = zc_count(tau)
    in_hyst = sum(1 for x in op if abs(x) < hyst) / len(op)
    # moving samples: |Δθ| over ~100 ms windows via |ω_pos|
    moving = [r for r in seg if abs(r["omega_pos"]) >= 0.15]
    buzzing = [r for r in seg if abs(r["omega_pos"]) < 0.15]
    # high-frequency |ω_pos| (detrend by 5-sample mean)
    hf = []
    for i, x in enumerate(op):
        lo = max(0, i - 2)
        hi = min(len(op), i + 3)
        hf.append(x - mean(op[lo:hi]))
    # expected hysteretic torque (no sat): -(tc + b*|ω|) * sign_latched
    # compare τ std vs that model using ω_pos
    return dict(
        n=len(seg), dt=dt,
        pos_pp=pp(pos), pos_mn=mean(pos),
        w_abs=absmean(om), w_pp=pp(om), w_zc_s=zc_om / dt,
        wp_abs=absmean(op), wp_pp=pp(op), wp_zc_s=zc_op / dt,
        wp_std=stdev(op), wp_hf_std=stdev(hf),
        in_hyst=in_hyst,
        t_abs=absmean(tau), t_std=stdev(tau), t_pp=pp(tau),
        t_max=max(abs(x) for x in tau), t_zc_s=zc_tau / dt,
        move_frac=len(moving) / len(seg),
        buzz_frac=len(buzzing) / len(seg),
        move_wp_zc_s=(zc_count([r["omega_pos"] for r in moving]) /
                      max(1e-3, (moving[-1]["wt"] - moving[0]["wt"]))
                      if len(moving) > 5 else 0.0),
        visc_ripple=B * stdev([abs(x) for x in op]),
        err=any(r["status"] == "ERROR" for r in seg),
    )


def fmt_score(name: str, s: dict) -> str:
    if s.get("n", 0) < 5:
        return f"{name:<10} N={s.get('n', 0)}  (too few samples)"
    return (
        f"{name:<10} N={s['n']:<5} dt={s['dt']:.1f}s  "
        f"pos_pp={s['pos_pp']:.1f}°  pos_mn={s['pos_mn']:.1f}°  "
        f"|ω_pos|={s['wp_abs']:.2f}  ω_pos_pp={s['wp_pp']:.2f}  "
        f"ω_pos_zc/s={s['wp_zc_s']:.1f}  in_hyst={100 * s['in_hyst']:.0f}%  "
        f"|τ|={s['t_abs']:.2f}  τ_std={s['t_std']:.2f}  τ_pp={s['t_pp']:.2f}  "
        f"τ_zc/s={s['t_zc_s']:.1f}  move={100 * s['move_frac']:.0f}%  "
        f"visc_ripple≈{s['visc_ripple']:.2f}Nm"
    )


def analyze(rows: list[dict], events: list[dict], meta: dict) -> str:
    lines: list[str] = []
    cfg = meta["config"]
    lines.append("HYST SLOW/FAST TEST")
    lines.append(
        f"robot  b={cfg.get('viscous')}  τc={cfg.get('coulomb')}  "
        f"hyst={cfg.get('coulomb_hyst')}  kw={cfg.get('wall_stiffness')}"
    )
    lines.append(f"N={len(rows)}")
    lines.append("")
    scored = {}
    for e in events:
        seg = [r for r in rows if e["t0"] <= r["wt"] <= e["t1"]]
        s = score(seg, HYST)
        scored[e["phase"]] = s
        lines.append(fmt_score(e["phase"], s))
        if s.get("n", 0) >= 5:
            lines.append(
                f"{'':10} stream_ω |ω|={s['w_abs']:.2f} pp={s['w_pp']:.2f} "
                f"zc/s={s['w_zc_s']:.1f}  (stream drops minus sign for |x|<1)"
            )
    lines.append("")
    rest = scored.get("rest") or {}
    slow = scored.get("slow") or {}
    fast = scored.get("fast") or {}

    lines.append("=== WHAT THE NUMBERS MEAN ===")
    if rest.get("n", 0) >= 5:
        lines.append(
            f"REST: pos_pp={rest['pos_pp']:.2f}°  ω_pos_zc/s={rest['wp_zc_s']:.1f}  "
            f"|τ|={rest['t_abs']:.2f}. "
            "A still lever with zc/s>10 and |τ| near τc is Coulomb hunt, not rest."
        )
    if slow.get("n", 0) >= 5:
        lines.append(
            f"SLOW: travel {slow['pos_pp']:.1f}°  in_hyst_band={100 * slow['in_hyst']:.0f}%  "
            f"ω_pos_zc/s={slow['wp_zc_s']:.1f} while move_frac={100 * slow['move_frac']:.0f}%."
        )
        if slow["wp_zc_s"] > 8 and slow["pos_pp"] > 10:
            lines.append(
                "  → Sign(ω) is still flipping during a real slow stroke. "
                f"hyst={HYST} rad/s is smaller than slow-motion |ω| noise "
                f"(ω_pos_pp={slow['wp_pp']:.2f}). Dir cannot stay latched."
            )
        if slow["in_hyst"] > 0.4 and slow["pos_pp"] > 10:
            lines.append(
                "  → A large fraction of slow samples sit inside the hyst band "
                "even though the lever is travelling. The Schmitt is living in "
                "its hold region on noisy ω, then breaking on noise spikes."
            )
        if slow["visc_ripple"] > 0.15:
            lines.append(
                f"  → Even with latched sign, |ω| chatter makes viscous ripple "
                f"~{slow['visc_ripple']:.2f} Nm (b·std|ω|). That is grind on top of DC Coulomb."
            )
        if slow["t_std"] > 0.25 and slow["pos_pp"] > 10:
            lines.append(
                f"  → Torque is not a steady drag (τ_std={slow['t_std']:.2f}, "
                f"τ_pp={slow['t_pp']:.2f}). Hand feels vibration, not friction."
            )
    if fast.get("n", 0) >= 5:
        lines.append(
            f"FAST: travel {fast['pos_pp']:.1f}°  |ω_pos|={fast['wp_abs']:.2f}  "
            f"ω_pos_zc/s={fast['wp_zc_s']:.1f}  |τ|={fast['t_abs']:.2f}."
        )
        if fast["wp_zc_s"] < slow.get("wp_zc_s", 0) * 0.5 and fast["pos_pp"] > 20:
            lines.append(
                "  → Fast is cleaner than slow (fewer reversals, more travel). "
                "The bug is the slow-speed ω sign / |ω| noise, not the 8 kHz loop."
            )
    lines.append("")
    lines.append("=== RECOMMENDATION (filled by this run's scores) ===")
    # decision tree
    slow_flip = slow.get("wp_zc_s", 0) > 8 and slow.get("pos_pp", 0) > 8
    slow_ripple = slow.get("visc_ripple", 0) > 0.15
    rest_hunt = rest.get("wp_zc_s", 0) > 10 and rest.get("pos_pp", 0) < 8
    if slow_flip:
        lines.append(
            "Primary: slow-stroke Coulomb/viscous still follow noisy ω. "
            "Raising hyst would latch more but a slow reverse then feels wrong-way "
            "until |ω| exceeds the new band. Best fix is NOT a bigger valve_hyst."
        )
        lines.append(
            "Best fix: stick/slide on POSITION. Below a definitely-moving threshold, "
            "freeze θ and apply clamp(-k·Δθ, ±τ_static). Once sliding, hold kinetic "
            "dir from breakaway (or from Δθ over 20–40 ms), not from CAN ω. Restick "
            "only when position is still. Viscous uses the same latched dir · |Δθ/dt|."
        )
    elif slow_ripple:
        lines.append(
            "Dir is mostly latched, but |ω| chatter still AM-modulates viscous. "
            "Smooth the speed used for −b|ω| (boxcar Δθ over 20–40 ms), keep "
            "Coulomb as DC −τc·dir. Do not add a torque LPF (that delays the loop)."
        )
    elif rest_hunt:
        lines.append(
            "Motion may be OK; rest still hunts. Stick-spring at rest is the fix "
            "for that; hyst cannot hold a pose."
        )
    else:
        lines.append(
            "If slow travel is large and zc/s is low, vibration may be mechanical "
            "or 1 kHz encoder stair-steps. Then use Δθ-window ω for viscous only."
        )
    lines.append(
        "Do not: more torque LPF, more ε, or a zero-torque deadband (ice at reverse). "
        "Do not raise hyst past ~0.5 just to hide slow chatter — it fights intended reverse."
    )
    return "\n".join(lines) + "\n"


def main() -> int:
    LOG.mkdir(parents=True, exist_ok=True)
    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    meta = arm()
    stream = Stream(ts)
    events: list[dict] = []

    def hold(phase: str, seconds: float, note: str, human: bool) -> None:
        stream.phase = phase
        if human:
            banner(f"YOUR TURN — {note}",
                   "Stay MID-RANGE. Do not slam 0°/90°.")
            countdown(5)
            say(f"    >>> GO NOW — {note}")
        else:
            banner(f"HANDS OFF — {note}", "Do not touch the lever.")
        t0 = time.time()
        events.append(dict(phase=phase, t0=t0, human=human, note=note))
        while time.time() - t0 < seconds:
            stream.drain(0.1)
        events[-1]["t1"] = time.time()
        od = api_get("/odrive")
        vs = api_get("/status")
        say(f"    === end {phase}  odrive_err=0x{int(od.get('axis_error') or 0):X} "
            f"valve={vs.get('status')} ===")

    banner(
        "GET TO THE QUARTER-TURN LEVER NOW",
        "ROBOT  viscous=0.8  coulomb=0.8  hyst=0.25",
        "Protocol: HANDS OFF rest → SLOW mid-range → FASTER mid-range.",
        "Do not slam the end stops.",
    )
    countdown(15)

    hold("rest", 6, "leave still, hands off", human=False)
    hold("slow", 12,
         "SLOWLY sweep back and forth in MID-RANGE (~2–4 s per pass, 12 s)",
         human=True)
    hold("fast", 8,
         "FASTER mid-range sweeps (not a slam, not into the stops)",
         human=True)

    say("\n>>> STOP — hands off")
    try:
        api_post("/control", {"action": "stop"})
    except Exception as e:
        say(f"  stop warn: {e}")
    time.sleep(0.2)
    try:
        api_post("/odrive", {"action": "disable"})
    except Exception as e:
        say(f"  disable warn: {e}")
    try:
        api_post("/stream", {"action": "stop"})
    except Exception as e:
        say(f"  stream stop warn: {e}")
    stream.drain(0.2)
    stream.close()

    summary = analyze(stream.rows, events, meta)
    anp = LOG / f"hyst_slowfast_analysis_{ts}.txt"
    anp.write_text(summary)
    (LOG / "ACTIVE_STREAM.txt").write_text(str(stream.csvp) + "\n")
    say("\n=== RESULTS ===")
    say(summary)
    say(f"wrote {anp}")
    say("DONE")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
