#!/usr/bin/env python3
"""
Log PRISM CAN I/O timeline: ODrive encoder RX → haptic compute → torque TX.

Firmware stamps encoder arrival in the FDCAN RX ISR (DWT µs) and torque
send in the TIM6 control ISR. Ethernet stream (1 ms) carries the latched
values. This tool reconstructs rates, jitter, and encoder→torque latency.

Also answers: is C compute the bottleneck, or are we waiting on the next
1 kHz encoder frame?
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import socket
import statistics
import sys
import time
from datetime import datetime
from pathlib import Path
from typing import Optional

import requests

HOST_DEFAULT = "10.0.1.15"
API_KEY = "steve-valve-2025"
STREAM_PORT = 8888
LOG = Path("/home/uw/Documents/PRISM/logs")


def api(host: str, path: str, method: str = "GET", body: Optional[dict] = None,
        timeout: float = 5.0) -> dict:
    url = f"http://{host}:8080/api/v1{path}"
    headers = {"X-API-Key": API_KEY, "Content-Type": "application/json"}
    if method == "GET":
        r = requests.get(url, headers=headers, timeout=timeout)
    else:
        r = requests.post(url, headers=headers, json=body or {}, timeout=timeout)
    r.raise_for_status()
    return r.json()


def pct(xs: list[float], p: float) -> float:
    if not xs:
        return 0.0
    s = sorted(xs)
    i = min(len(s) - 1, max(0, int(round((p / 100.0) * (len(s) - 1)))))
    return float(s[i])


def summarize(xs: list[float]) -> dict:
    if not xs:
        return {"n": 0, "min": 0, "max": 0, "avg": 0, "p50": 0, "p95": 0, "std": 0}
    return {
        "n": len(xs),
        "min": min(xs),
        "max": max(xs),
        "avg": sum(xs) / len(xs),
        "p50": pct(xs, 50),
        "p95": pct(xs, 95),
        "std": statistics.pstdev(xs) if len(xs) > 1 else 0.0,
    }


def main() -> int:
    p = argparse.ArgumentParser(description="Encoder RX → torque TX timeline")
    p.add_argument("--host", default=HOST_DEFAULT)
    p.add_argument("--seconds", type=float, default=8.0)
    p.add_argument("--interval-ms", type=int, default=1,
                   help="Ethernet stream interval (firmware min 1 ms)")
    p.add_argument("--start-valve", action="store_true")
    args = p.parse_args()

    LOG.mkdir(parents=True, exist_ok=True)
    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    jsonl_path = LOG / f"loop_timeline_{ts}.jsonl"
    csv_path = LOG / f"loop_timeline_{ts}.csv"
    report_path = LOG / f"loop_timeline_{ts}.txt"

    print(f"=== loop timeline {ts} ===", flush=True)
    print(f"GET /api/v1/timing (before):", flush=True)
    try:
        print(json.dumps(api(args.host, "/timing"), indent=2), flush=True)
    except Exception as e:
        print(f"  timing endpoint not ready: {e}", flush=True)

    started = False
    if args.start_valve:
        st = api(args.host, "/status")
        if int(st.get("status") or 0) != 2:
            try:
                api(args.host, "/odrive", "POST", {"action": "clear"})
            except Exception:
                pass
            time.sleep(0.3)
            api(args.host, "/control", "POST", {"action": "start"})
            started = True
            print("valve started", flush=True)
        else:
            print("valve already RUNNING", flush=True)

    api(args.host, "/stream", "POST",
        {"action": "start", "interval_ms": args.interval_ms})
    time.sleep(0.2)

    sock = socket.create_connection((args.host, STREAM_PORT), timeout=5)
    sock.settimeout(0.25)
    buf = b""
    rows: list[dict] = []
    t_end = time.time() + args.seconds
    print(f"recording {args.seconds}s @ {args.interval_ms} ms stream…", flush=True)

    with jsonl_path.open("w") as jf:
        while time.time() < t_end:
            try:
                chunk = sock.recv(8192)
            except socket.timeout:
                chunk = b""
            if not chunk:
                continue
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                line = line.strip()
                if not line:
                    continue
                try:
                    sample = json.loads(line)
                except json.JSONDecodeError:
                    continue
                if "enc_seq" not in sample and "seq" not in sample:
                    continue
                sample["wall_time"] = time.time()
                jf.write(json.dumps(sample) + "\n")
                rows.append(sample)

    sock.close()
    try:
        api(args.host, "/stream", "POST", {"action": "stop"})
    except Exception:
        pass
    fw_stats = {}
    try:
        fw_stats = api(args.host, "/timing")
    except Exception:
        pass
    if started:
        try:
            api(args.host, "/control", "POST", {"action": "stop"})
            api(args.host, "/odrive", "POST", {"action": "disable"})
        except Exception:
            pass

    # --- analysis ---
    loops = []
    enc_events = []
    prev = None
    prev_enc = None
    for s in rows:
        try:
            seq = int(s.get("seq") or 0)
            enc = int(s.get("enc_seq") or 0)
            loop_us = float(s.get("loop_time_us") or 0)
            enc_age = float(s.get("enc_age_us") or 0)
            enc_to_tx = float(s.get("enc_to_tx_us") or 0)
            enc_period = float(s.get("enc_period_us") or 0)
            new_enc = bool(s.get("new_enc"))
        except (TypeError, ValueError):
            continue
        wall = float(s.get("wall_time") or 0)
        loops.append(dict(seq=seq, enc=enc, loop_us=loop_us, enc_age=enc_age,
                          enc_to_tx=enc_to_tx, enc_period=enc_period,
                          new_enc=new_enc, wall=wall,
                          pos=s.get("position_deg"), tau=s.get("torque_nm")))
        if prev_enc is None or enc != prev_enc:
            if prev_enc is not None:
                dt_wall = wall - enc_events[-1]["wall"] if enc_events else 0
                dseq = enc - prev_enc
                enc_events.append(dict(
                    enc=enc, wall=wall, period_fw=enc_period,
                    period_wall_s=dt_wall, dseq=dseq,
                    enc_to_tx=enc_to_tx, enc_age=enc_age, loop_us=loop_us,
                ))
            else:
                enc_events.append(dict(
                    enc=enc, wall=wall, period_fw=enc_period,
                    period_wall_s=0, dseq=0,
                    enc_to_tx=enc_to_tx, enc_age=enc_age, loop_us=loop_us,
                ))
            prev_enc = enc
        prev = s

    with csv_path.open("w", newline="") as cf:
        fields = ["wall_time", "seq", "enc_seq", "new_enc", "loop_time_us",
                  "enc_age_us", "enc_to_tx_us", "enc_period_us",
                  "position_deg", "torque_nm", "omega_rad_s"]
        w = csv.DictWriter(cf, fieldnames=fields, extrasaction="ignore")
        w.writeheader()
        for s in rows:
            w.writerow({k: s.get(k) for k in fields})

    loop_us = [x["loop_us"] for x in loops if x["loop_us"] > 0]
    enc_to_tx = [e["enc_to_tx"] for e in enc_events if 0 < e["enc_to_tx"] < 20000]
    enc_period = [e["period_fw"] for e in enc_events if 200 < e["period_fw"] < 5000]
    enc_gaps = [e["dseq"] for e in enc_events if e["dseq"] > 0]
    missed = sum(max(0, d - 1) for d in enc_gaps)

    wall_span = (loops[-1]["wall"] - loops[0]["wall"]) if len(loops) > 1 else 0
    seq_span = (loops[-1]["seq"] - loops[0]["seq"]) if len(loops) > 1 else 0
    enc_span = (loops[-1]["enc"] - loops[0]["enc"]) if len(loops) > 1 else 0
    loop_hz = seq_span / wall_span if wall_span > 0 else 0
    enc_hz = enc_span / wall_span if wall_span > 0 else 0
    stream_hz = (len(loops) - 1) / wall_span if wall_span > 0 else 0

    # How many control loops per encoder frame?
    loops_per_enc = loop_hz / enc_hz if enc_hz > 0 else 0
    design_enc_us = 1e6 / 1000.0
    avg_loop = summarize(loop_us)["avg"]
    avg_to_tx = summarize(enc_to_tx)["avg"]
    headroom_us = design_enc_us - avg_loop
    # If we computed in the RX ISR, latency would be ~compute only.
    # Current architecture: wait until next TIM6 tick after RX (0..loop_period).
    loop_period_us = 1e6 / loop_hz if loop_hz > 0 else 125.0

    lines = []
    def out(s=""):
        print(s, flush=True)
        lines.append(s)

    out(f"samples={len(loops)}  stream≈{stream_hz:.0f} Hz  wall={wall_span:.3f}s")
    out(f"firmware GET /api/v1/timing: {json.dumps(fw_stats)}")
    out()
    out("=== RATES ===")
    out(f"  control loop (seq):     {loop_hz:.1f} Hz   (design {fw_stats.get('loop_hz', '?')} Hz)")
    out(f"  encoder RX (enc_seq):   {enc_hz:.1f} Hz   (ODrive cyclic 1000 Hz)")
    out(f"  torque TX ≈ loop rate:  {loop_hz:.1f} Hz")
    out(f"  loops per encoder:      {loops_per_enc:.2f}")
    out(f"  encoder seq gaps>1:     {missed} missing frames")
    out()
    out("=== CONSISTENCY ===")
    sp = summarize(enc_period)
    out(f"  encoder period (firmware, µs): min {sp['min']:.0f}  avg {sp['avg']:.0f}  "
        f"max {sp['max']:.0f}  p95 {sp['p95']:.0f}  std {sp['std']:.1f}")
    sl = summarize(loop_us)
    out(f"  loop compute+tx (µs):          min {sl['min']:.0f}  avg {sl['avg']:.0f}  "
        f"max {sl['max']:.0f}  p95 {sl['p95']:.0f}")
    stt = summarize(enc_to_tx)
    out(f"  encoder RX → first torque TX:  min {stt['min']:.0f}  avg {stt['avg']:.0f}  "
        f"max {stt['max']:.0f}  p95 {stt['p95']:.0f} µs  n={stt['n']}")
    out()
    out("=== BOTTLENECK ===")
    out(f"  TIM6 period ≈ {loop_period_us:.0f} µs")
    out(f"  C compute+CAN TX uses {sl['avg']:.1f} µs "
        f"({100*sl['avg']/loop_period_us:.1f}% of a loop, "
        f"{100*sl['avg']/design_enc_us:.2f}% of a 1 ms encoder period)")
    out(f"  idle headroom in each 1 ms encoder slot: {headroom_us:.0f} µs")
    if stt["avg"] > 0:
        out(f"  Typical wait: encoder arrives in FDCAN ISR, torque goes out on "
            f"the next TIM6 tick → ~{stt['avg']:.0f} µs (0–{loop_period_us:.0f} µs).")
    if sl["avg"] < 50 and enc_hz < loop_hz * 0.3:
        out("  Compute is NOT the bottleneck. Next torque is ready in tens of µs;")
        out("  the loop then waits for the next ODrive encoder frame (~1 ms).")
        out("  Running the C faster (or moving physics into the RX ISR) would")
        out(f"  cut RX→TX from ~{stt['avg']:.0f} µs toward ~{sl['avg']:.0f} µs,")
        out("  but new position still only arrives at 1 kHz.")
        out("  To shrink the 1 ms hole: raise ODrive encoder_msg_rate (already 1 ms min)")
        out("  or accept 8 kHz torque on a held encoder sample (already the case).")
    out()
    out(f"wrote {jsonl_path}")
    out(f"wrote {csv_path}")
    report_path.write_text("\n".join(lines) + "\n")
    out(f"wrote {report_path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
