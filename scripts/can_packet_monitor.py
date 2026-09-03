#!/usr/bin/env python3
"""
Poll PRISM GET /api/v1/can and report CAN-FD packet rate + loss.

ODrive broadcasts encoder estimates at 1 kHz as CAN FD + BRS
(1 Mbps arbitration / 5 Mbps data). Nucleo TX is classic 1 Mbps.
"""
from __future__ import annotations

import argparse
import csv
import json
import sys
import time
from datetime import datetime
from pathlib import Path
from typing import Optional

import requests

HOST_DEFAULT = "10.0.1.15"
API_KEY = "steve-valve-2025"
LOG = Path("/home/uw/Documents/PRISM/logs")

# 8-byte CAN FD+BRS frame on 1M/5M (ISO 11898-1, 11-bit ID, 1 stuff-bit in 5):
# arb ~29 bits @ 1 Mbps, data ~64+CRC/ack ~80 bits @ 5 Mbps → ~45 µs.
# 1000 encoder frames/s ≈ 4.5% of 5 Mbps data phase, not 5e6 packets/s.
EXPECTED_ENCODER_HZ = 1000.0
FD_FRAME_BITS_DATA = 80.0
NOMINAL_FRAME_BITS = 111.0  # classic 8-byte 11-bit approx with stuff


def api(host: str, path: str, method: str = "GET", body: Optional[dict] = None) -> dict:
    url = f"http://{host}:8080/api/v1{path}"
    headers = {"X-API-Key": API_KEY, "Content-Type": "application/json"}
    if method == "GET":
        r = requests.get(url, headers=headers, timeout=2.0)
    else:
        r = requests.post(url, headers=headers, json=body or {}, timeout=5.0)
    r.raise_for_status()
    return r.json()


def snapshot(host: str) -> dict:
    data = api(host, "/can")
    bus = data.get("bus") or {}
    enc = data.get("encoder") or {}
    return {
        "t": time.time(),
        "rx": int(bus.get("rx_count") or 0),
        "tx": int(bus.get("tx_count") or 0),
        "tx_fail": int(bus.get("tx_fail") or 0),
        "lost": int(bus.get("rx_fifo_lost") or 0),
        "full": int(bus.get("rx_fifo_full") or 0),
        "ring": int(bus.get("rx_ring_drop") or 0),
        "proto": int(bus.get("protocol_errors") or 0),
        "bus_off": int(bus.get("bus_off") or 0),
        "err": int(bus.get("error_count") or 0),
        "tec": int(bus.get("tec") or 0),
        "rec": int(bus.get("rec") or 0),
        "cel": int(bus.get("cel") or 0),
        "psr": str(bus.get("psr") or ""),
        "nominal": int(bus.get("nominal_bps") or 1000000),
        "data_bps": int(bus.get("data_bps") or 5000000),
        "enc_seq": int(enc.get("seq") or 0),
        "enc_pos": float(enc.get("position") or 0),
        "raw": data,
    }


def rate_stats(xs: list[float]) -> tuple[float, float, float]:
    if not xs:
        return 0.0, 0.0, 0.0
    return min(xs), max(xs), sum(xs) / len(xs)


def fmt_rate(mn: float, mx: float, av: float, unit: str = "pkt/s") -> str:
    return f"min {mn:.1f}  max {mx:.1f}  avg {av:.1f} {unit}"


def main() -> int:
    p = argparse.ArgumentParser(description="CAN-FD packet rate / loss monitor")
    p.add_argument("--host", default=HOST_DEFAULT)
    p.add_argument("--seconds", type=float, default=15.0)
    p.add_argument("--interval", type=float, default=0.05, help="poll period s")
    p.add_argument("--start-valve", action="store_true",
                   help="enable closed-loop so 1 kHz torque TX is included")
    args = p.parse_args()

    LOG.mkdir(parents=True, exist_ok=True)
    ts = datetime.now().strftime("%Y%m%d_%H%M%S")
    csvp = LOG / f"can_monitor_{ts}.csv"
    jsonp = LOG / f"can_monitor_{ts}.json"

    print(f"=== CAN-FD packet monitor {ts} ===", flush=True)
    print(f"host={args.host}  duration={args.seconds}s  dt={args.interval}s", flush=True)

    first = snapshot(args.host)
    print(
        f"link nominal={first['nominal']} bps  data/BRS={first['data_bps']} bps",
        flush=True,
    )
    print(
        f"start rx={first['rx']} tx={first['tx']} lost={first['lost']} "
        f"enc_seq={first['enc_seq']} TEC/REC={first['tec']}/{first['rec']} "
        f"CEL={first['cel']} PSR={first['psr']}",
        flush=True,
    )

    started_valve = False
    if args.start_valve:
        try:
            api(args.host, "/odrive", "POST", {"action": "clear"})
            time.sleep(0.3)
            st = api(args.host, "/status")
            if int(st.get("status") or 0) != 2:
                api(args.host, "/control", "POST", {"action": "start"})
                started_valve = True
                print("valve started (1 kHz torque TX on)", flush=True)
            else:
                print("valve already RUNNING", flush=True)
        except Exception as e:
            print(f"WARN: could not start valve: {e}", flush=True)

    samples: list[dict] = []
    t_end = time.time() + args.seconds
    prev = snapshot(args.host)
    time.sleep(args.interval)

    while time.time() < t_end:
        try:
            cur = snapshot(args.host)
        except Exception as e:
            print(f"poll error: {e}", flush=True)
            time.sleep(args.interval)
            continue
        dt = cur["t"] - prev["t"]
        if dt <= 0:
            time.sleep(args.interval)
            continue
        rec = {
            "t": cur["t"],
            "dt": dt,
            "rx_hz": (cur["rx"] - prev["rx"]) / dt,
            "tx_hz": (cur["tx"] - prev["tx"]) / dt,
            "enc_hz": (cur["enc_seq"] - prev["enc_seq"]) / dt,
            "lost_d": cur["lost"] - prev["lost"],
            "full_d": cur["full"] - prev["full"],
            "ring_d": cur["ring"] - prev["ring"],
            "proto_d": cur["proto"] - prev["proto"],
            "tx_fail_d": cur["tx_fail"] - prev["tx_fail"],
            "bus_off_d": cur["bus_off"] - prev["bus_off"],
            **{k: cur[k] for k in (
                "rx", "tx", "lost", "full", "ring", "proto", "tx_fail",
                "bus_off", "tec", "rec", "cel", "enc_seq", "enc_pos",
            )},
        }
        samples.append(rec)
        prev = cur
        time.sleep(args.interval)

    if started_valve:
        try:
            api(args.host, "/control", "POST", {"action": "stop"})
            api(args.host, "/odrive", "POST", {"action": "disable"})
            print("valve stopped", flush=True)
        except Exception as e:
            print(f"WARN stop: {e}", flush=True)

    if not samples:
        print("FATAL: no samples", file=sys.stderr)
        return 2

    rx_rates = [s["rx_hz"] for s in samples]
    tx_rates = [s["tx_hz"] for s in samples]
    enc_rates = [s["enc_hz"] for s in samples]
    lost = sum(s["lost_d"] for s in samples)
    full = sum(s["full_d"] for s in samples)
    ring = sum(s["ring_d"] for s in samples)
    proto = sum(s["proto_d"] for s in samples)
    tx_fail = sum(s["tx_fail_d"] for s in samples)
    bus_off = sum(s["bus_off_d"] for s in samples)
    elapsed = samples[-1]["t"] - samples[0]["t"] + samples[0]["dt"]
    rx_total = samples[-1]["rx"] - (samples[0]["rx"] - int(round(samples[0]["rx_hz"] * samples[0]["dt"])))
    # simpler: last-first over wall time of first-to-last
    wall = samples[-1]["t"] - samples[0]["t"]
    if wall <= 0:
        wall = elapsed
    rx_span = samples[-1]["rx"] - samples[0]["rx"]
    tx_span = samples[-1]["tx"] - samples[0]["tx"]
    enc_span = samples[-1]["enc_seq"] - samples[0]["enc_seq"]
    rx_avg_span = rx_span / wall if wall else 0.0
    enc_avg_span = enc_span / wall if wall else 0.0
    enc_expect = EXPECTED_ENCODER_HZ * wall
    enc_loss = max(0.0, enc_expect - enc_span)
    enc_loss_pct = 100.0 * enc_loss / enc_expect if enc_expect else 0.0

    data_bps = first["data_bps"] or 5_000_000
    # RX is FD+BRS (encoder etc). Utilization vs 5 Mbps data phase.
    rx_mbps = rx_avg_span * FD_FRAME_BITS_DATA
    rx_util = 100.0 * rx_mbps / data_bps if data_bps else 0.0
    tx_mbps = (tx_span / wall) * NOMINAL_FRAME_BITS if wall else 0.0

    rx_mn, rx_mx, rx_av = rate_stats(rx_rates)
    tx_mn, tx_mx, tx_av = rate_stats(tx_rates)
    en_mn, en_mx, en_av = rate_stats(enc_rates)

    last = samples[-1]
    report = {
        "ts": ts,
        "host": args.host,
        "seconds": args.seconds,
        "interval_s": args.interval,
        "n_samples": len(samples),
        "nominal_bps": first["nominal"],
        "data_bps": data_bps,
        "rx_pkt_s": {"min": rx_mn, "max": rx_mx, "avg": rx_av, "span_avg": rx_avg_span},
        "tx_pkt_s": {"min": tx_mn, "max": tx_mx, "avg": tx_av},
        "encoder_pkt_s": {"min": en_mn, "max": en_mx, "avg": en_av, "expected": EXPECTED_ENCODER_HZ},
        "loss": {
            "rx_fifo_lost": lost,
            "rx_fifo_full": full,
            "rx_ring_drop": ring,
            "protocol_errors": proto,
            "tx_fail": tx_fail,
            "bus_off": bus_off,
            "encoder_missing_vs_1khz": enc_loss,
            "encoder_loss_pct": enc_loss_pct,
            "tec": last["tec"],
            "rec": last["rec"],
            "cel": last["cel"],
        },
        "util": {
            "rx_bits_s": rx_mbps,
            "rx_data_phase_pct": rx_util,
            "tx_bits_s": tx_mbps,
        },
    }

    fieldnames = [
        "t", "dt", "rx_hz", "tx_hz", "enc_hz", "lost_d", "full_d", "ring_d",
        "proto_d", "tx_fail_d", "bus_off_d", "rx", "tx", "lost", "enc_seq",
        "tec", "rec", "cel",
    ]
    with csvp.open("w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=fieldnames, extrasaction="ignore")
        w.writeheader()
        for s in samples:
            w.writerow(s)
    jsonp.write_text(json.dumps(report, indent=2) + "\n")

    print("", flush=True)
    print("=== PACKET RATE (1 Mbps arb / 5 Mbps CAN-FD data) ===", flush=True)
    print(f"  RX frames:     {fmt_rate(rx_mn, rx_mx, rx_av)}", flush=True)
    print(f"  TX frames:     {fmt_rate(tx_mn, tx_mx, tx_av)}", flush=True)
    print(f"  Encoder 0x09:  {fmt_rate(en_mn, en_mx, en_av)}  (expect {EXPECTED_ENCODER_HZ:.0f})", flush=True)
    print("", flush=True)
    print("=== LOSS / ERRORS (over window) ===", flush=True)
    print(f"  RX FIFO lost (hardware drop): {lost}", flush=True)
    print(f"  RX FIFO full events:          {full}", flush=True)
    print(f"  RX software-ring overwrite:   {ring}  (callback still delivered)", flush=True)
    print(f"  TX queue fail / timeout:      {tx_fail}", flush=True)
    print(f"  Protocol errors:              {proto}", flush=True)
    print(f"  Bus-off events:               {bus_off}", flush=True)
    print(f"  Encoder vs 1 kHz expected:    missing {enc_loss:.0f} frames ({enc_loss_pct:.2f}%)", flush=True)
    print(f"  TEC/REC/CEL (end):            {last['tec']} / {last['rec']} / {last['cel']}", flush=True)
    print("", flush=True)
    print("=== 5 Mbps DATA-PHASE LOAD (RX FD frames) ===", flush=True)
    print(f"  ~{rx_mbps/1e3:.1f} kbit/s  ({rx_util:.2f}% of {data_bps/1e6:.0f} Mbps)", flush=True)
    print(f"  TX classic ~{tx_mbps/1e3:.1f} kbit/s on 1 Mbps arbitration", flush=True)
    print("", flush=True)
    if lost > 0 or enc_loss_pct > 2.0 or last["tec"] > 0 or last["rec"] > 0 or bus_off:
        print("RESULT: packet loss or bus errors detected", flush=True)
        verdict = 1
    else:
        print("RESULT: no hardware FIFO loss; encoder rate matches 1 kHz", flush=True)
        verdict = 0
    print(f"wrote {csvp}", flush=True)
    print(f"wrote {jsonp}", flush=True)
    return verdict


if __name__ == "__main__":
    sys.exit(main())
