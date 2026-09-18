#!/usr/bin/env python3
"""PRISM-LOG/1.0 reader, normalizer, stats, and demo generator.

See docs/log/prism-log-standard.md for the canonical field list.
"""

from __future__ import annotations

import gzip
import io
import json
import math
import os
import random
import threading
from collections import OrderedDict
from datetime import datetime, timezone
from typing import Any, Dict, Iterable, List, Optional, Sequence, Tuple

PI = math.pi
DEG2RAD = PI / 180.0
RAD2DEG = 180.0 / PI
KT_DEFAULT = 0.083  # N·m / A  (ODrive S1, firmware ODRIVE_TORQUE_CONSTANT_NM_PER_A)
MAGIC = "# PRISM-LOG/1.0"

# ---------------------------------------------------------------------------
# Channel catalog (machine-readable mirror of the spec)
# ---------------------------------------------------------------------------

Channel = Dict[str, Any]


def _ch(
    key: str,
    group: str,
    unit: str,
    label: str,
    color: str,
    *,
    default: bool = False,
    derived: bool = False,
    kind: str = "float",
) -> Channel:
    return {
        "key": key,
        "group": group,
        "unit": unit,
        "label": label,
        "color": color,
        "default": default,
        "derived": derived,
        "kind": kind,
    }


CHANNELS: List[Channel] = [
    # Time
    _ch("t_s", "time", "s", "Time", "#94a3b8", kind="time"),
    _ch("t_us", "time", "µs", "Device time", "#94a3b8", kind="int"),
    _ch("timestamp_ms", "time", "ms", "Systick", "#94a3b8", kind="int"),
    _ch("host_unix_s", "time", "s", "Host time", "#94a3b8"),
    _ch("dt_s", "time", "s", "Sample period", "#94a3b8", derived=True),
    _ch("seq", "time", "1", "Sequence", "#94a3b8", kind="int"),
    # Kinematics — default view
    _ch("pos_deg", "kinematics", "deg", "Position", "#3ee0c9", default=True),
    _ch("pos_rad", "kinematics", "rad", "Position", "#2dd4bf", derived=True),
    _ch("pos_turns", "kinematics", "turn", "Encoder turns", "#14b8a6"),
    _ch("pos_norm", "kinematics", "1", "Normalized travel", "#5eead4", derived=True),
    _ch("pos_counts", "kinematics", "count", "Encoder counts", "#99f6e4", kind="int"),
    _ch("vel_rad_s", "kinematics", "rad/s", "Velocity", "#f5c14a", default=True),
    _ch("vel_deg_s", "kinematics", "deg/s", "Velocity", "#fbbf24", derived=True),
    _ch("vel_turns_s", "kinematics", "turn/s", "Velocity", "#f59e0b", derived=True),
    _ch("vel_raw_rad_s", "kinematics", "rad/s", "Raw Δθ velocity", "#d97706"),
    _ch("vel_odrive_rad_s", "kinematics", "rad/s", "ODrive velocity", "#b45309"),
    _ch("acc_rad_s2", "kinematics", "rad/s²", "Acceleration", "#fdba74"),
    _ch("acc_deg_s2", "kinematics", "deg/s²", "Acceleration", "#fed7aa", derived=True),
    # Torque — default view
    _ch("tau_nm", "torque", "N·m", "Torque", "#ff6b8a", default=True),
    _ch("tau_filt_nm", "torque", "N·m", "Filtered torque", "#fb7185"),
    _ch("tau_physics_nm", "torque", "N·m", "Physics torque", "#f43f5e"),
    _ch("tau_clamped_nm", "torque", "N·m", "Clamped torque", "#e11d48"),
    _ch("tau_viscous_nm", "torque", "N·m", "Viscous", "#c084fc"),
    _ch("tau_coulomb_nm", "torque", "N·m", "Coulomb", "#a78bfa"),
    _ch("tau_wall_nm", "torque", "N·m", "Wall", "#818cf8"),
    _ch("tau_wall_spring_nm", "torque", "N·m", "Wall spring", "#6366f1"),
    _ch("tau_wall_damp_nm", "torque", "N·m", "Wall damper", "#4f46e5"),
    _ch("dtau_dt_nm_s", "torque", "N·m/s", "Torque rate", "#e879f9", derived=True),
    _ch("force_handle_n", "torque", "N", "Handle force", "#f0abfc", derived=True),
    _ch("iq_from_tau_a", "torque", "A", "Iq from torque", "#f5d0fe", derived=True),
    # Physics
    _ch("wall_pen_turns", "physics", "turn", "Wall penetration", "#fb923c", derived=True),
    _ch("wall_contact", "physics", "1", "Wall contact", "#f97316", kind="bool", derived=True),
    _ch("quiet", "physics", "1", "Quiet gate", "#64748b", kind="bool"),
    _ch("settle_residual", "physics", "1", "Settle blank", "#475569", kind="bool"),
    _ch("coulomb_dir", "physics", "1", "Coulomb direction", "#38bdf8"),
    _ch("coulomb_scale", "physics", "1", "Coulomb scale", "#0ea5e9"),
    _ch("passivity_j", "physics", "J", "Passivity tank", "#22d3ee", derived=True),
    _ch("passivity_mj", "physics", "mJ", "Passivity tank", "#06b6d4"),
    _ch("power_w", "physics", "W", "Mechanical power", "#a78bfa", default=True, derived=True),
    _ch("power_passive_w", "physics", "W", "Dissipated power", "#8b5cf6", derived=True),
    _ch("energy_mech_j", "physics", "J", "Mechanical work", "#7c3aed", derived=True),
    _ch("energy_dissipated_j", "physics", "J", "Energy dissipated", "#6d28d9", derived=True),
    # Motor
    _ch("iq_set_a", "motor", "A", "Iq setpoint", "#4ade80"),
    _ch("iq_meas_a", "motor", "A", "Iq measured", "#22c55e"),
    _ch("bus_v", "motor", "V", "Bus voltage", "#38bdf8"),
    _ch("bus_i", "motor", "A", "Bus current", "#2dd4bf"),
    _ch("p_elec_w", "motor", "W", "Electrical power", "#34d399", derived=True),
    _ch("axis_state", "motor", "1", "Axis state", "#86efac", kind="int"),
    _ch("axis_error", "motor", "1", "Axis error", "#f87171", kind="int"),
    # Thermal
    _ch("temp_fet_c", "thermal", "°C", "FET temperature", "#fb923c"),
    _ch("temp_motor_c", "thermal", "°C", "Motor temperature", "#f97316"),
    _ch("temp_fet_peak_c", "thermal", "°C", "Peak FET", "#ea580c"),
    _ch("temp_motor_peak_c", "thermal", "°C", "Peak motor", "#c2410c"),
    # Timing
    _ch("loop_time_us", "timing", "µs", "Loop time", "#e2e8f0", kind="int"),
    _ch("enc_seq", "timing", "1", "Encoder seq", "#cbd5e1", kind="int"),
    _ch("enc_age_us", "timing", "µs", "Encoder age", "#94a3b8", kind="int"),
    _ch("enc_to_tx_us", "timing", "µs", "Enc→TX latency", "#64748b", kind="int"),
    _ch("enc_period_us", "timing", "µs", "Encoder period", "#475569", kind="int"),
    _ch("new_enc", "timing", "1", "New encoder", "#334155", kind="bool"),
    _ch("hb_age_ms", "timing", "ms", "Heartbeat age", "#1e293b", kind="int"),
    # CAN
    _ch("can_tx", "can", "1", "CAN TX", "#67e8f9", kind="int"),
    _ch("can_rx", "can", "1", "CAN RX", "#22d3ee", kind="int"),
    _ch("can_err", "can", "1", "CAN errors", "#f87171", kind="int"),
    _ch("can_tec", "can", "1", "TEC", "#fbbf24", kind="int"),
    _ch("can_rec", "can", "1", "REC", "#f59e0b", kind="int"),
    # Safety / flags
    _ch("data_valid", "flags", "1", "Data valid", "#4ade80", kind="bool"),
    _ch("estop", "flags", "1", "E-stop", "#ef4444", kind="bool"),
    _ch("last_error", "flags", "1", "Last error", "#f87171", kind="int"),
    # HITL
    _ch("hitl_tau_nm", "hitl", "N·m", "HITL torque", "#f472b6"),
    _ch("hitl_pos_deg", "hitl", "deg", "HITL position", "#ec4899"),
    _ch("hitl_vel_rad_s", "hitl", "rad/s", "HITL velocity", "#db2777"),
    _ch("hitl_rtt_us", "hitl", "µs", "HITL RTT", "#be185d", kind="int"),
    _ch("sim_alpha_rad_s2", "hitl", "rad/s²", "Sim acceleration", "#9d174d"),
]

CHANNEL_BY_KEY: Dict[str, Channel] = {c["key"]: c for c in CHANNELS}

GROUP_ORDER = [
    "kinematics",
    "torque",
    "physics",
    "motor",
    "thermal",
    "timing",
    "can",
    "flags",
    "hitl",
    "time",
]

GROUP_LABELS = {
    "kinematics": "Kinematics",
    "torque": "Torque & force",
    "physics": "Haptic physics",
    "motor": "Motor electrical",
    "thermal": "Thermal",
    "timing": "Timing & latency",
    "can": "CAN bus",
    "flags": "Safety / flags",
    "hitl": "HITL / Isaac Sim",
    "time": "Timebase",
}

STREAM_MAP = {
    "timestamp_ms": "timestamp_ms",
    "t_us": "t_us",
    "loop_time_us": "loop_time_us",
    "seq": "seq",
    "position_turns": "pos_turns",
    "position_deg": "pos_deg",
    "omega_rad_s": "vel_rad_s",
    "torque_nm": "tau_nm",
    "filt_torque_nm": "tau_filt_nm",
    "passivity_mj": "passivity_mj",
    "quiet": "quiet",
    "err": "last_error",
    "hb_age": "hb_age_ms",
    "enc_seq": "enc_seq",
    "enc_age_us": "enc_age_us",
    "enc_to_tx_us": "enc_to_tx_us",
    "enc_period_us": "enc_period_us",
    "new_enc": "new_enc",
    "data_valid": "data_valid",
    "wall_time": "host_unix_s",
    # already-normalized aliases
    "pos_deg": "pos_deg",
    "pos_turns": "pos_turns",
    "vel_rad_s": "vel_rad_s",
    "tau_nm": "tau_nm",
    "tau_filt_nm": "tau_filt_nm",
    "passivity_j": "passivity_j",
    "last_error": "last_error",
    "hb_age_ms": "hb_age_ms",
    "host_unix_s": "host_unix_s",
    "t_s": "t_s",
}

BOOL_KEYS = {c["key"] for c in CHANNELS if c["kind"] == "bool"}
INT_KEYS = {c["key"] for c in CHANNELS if c["kind"] == "int"}


# ---------------------------------------------------------------------------
# IO helpers
# ---------------------------------------------------------------------------

def _open_text(path: str):
    if path.endswith(".gz"):
        return io.TextIOWrapper(gzip.open(path, "rb"), encoding="utf-8")
    return open(path, "r", encoding="utf-8")


def _as_float(v: Any) -> Optional[float]:
    if v is None or v is False:
        if v is False:
            return 0.0
        return None
    if v is True:
        return 1.0
    try:
        x = float(v)
    except (TypeError, ValueError):
        return None
    if math.isnan(x) or math.isinf(x):
        return None
    return x


def _as_bool(v: Any) -> Optional[bool]:
    if v is None:
        return None
    if isinstance(v, bool):
        return v
    if isinstance(v, (int, float)):
        return bool(v)
    if isinstance(v, str):
        s = v.strip().lower()
        if s in ("true", "1", "yes", "running"):
            return True
        if s in ("false", "0", "no"):
            return False
    return None


def _norm_status(v: Any) -> Optional[str]:
    if v is None:
        return None
    if isinstance(v, int):
        return {0: "idle", 1: "initializing", 2: "running", 3: "error"}.get(v, str(v))
    s = str(v).strip().lower()
    if s in ("idle", "initializing", "running", "error", "no_data"):
        return s
    return s


def _utc_now() -> str:
    return datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3] + "Z"


# ---------------------------------------------------------------------------
# Sample normalization
# ---------------------------------------------------------------------------

def normalize_sample(raw: Dict[str, Any], config: Optional[Dict[str, Any]] = None) -> Dict[str, Any]:
    """Map a raw JSON object (PRISM-LOG sample or legacy stream packet) to spec keys."""
    src = raw
    if raw.get("rec") == "sample":
        src = {k: v for k, v in raw.items() if k != "rec"}

    out: Dict[str, Any] = {}
    for k, v in src.items():
        dest = STREAM_MAP.get(k, k)
        if dest == "status":
            out["status"] = _norm_status(v)
            continue
        if dest in BOOL_KEYS:
            b = _as_bool(v)
            if b is not None:
                out[dest] = b
            continue
        if dest in ("wall_side", "output_mode", "interaction", "status"):
            if v is not None:
                out[dest] = str(v).lower()
            continue
        fv = _as_float(v)
        if fv is None:
            continue
        if dest in INT_KEYS:
            out[dest] = int(fv)
        else:
            out[dest] = fv

    # Time
    if "t_s" not in out:
        if "t_us" in out:
            out["t_s"] = float(out["t_us"]) / 1e6
        elif "timestamp_ms" in out:
            out["t_s"] = float(out["timestamp_ms"]) / 1e3
        elif "host_unix_s" in out:
            out["t_s"] = float(out["host_unix_s"])

    # Kinematic dual units
    if "pos_deg" in out and "pos_rad" not in out:
        out["pos_rad"] = out["pos_deg"] * DEG2RAD
    if "pos_rad" in out and "pos_deg" not in out:
        out["pos_deg"] = out["pos_rad"] * RAD2DEG
    if "vel_rad_s" in out and "vel_deg_s" not in out:
        out["vel_deg_s"] = out["vel_rad_s"] * RAD2DEG
    if "vel_deg_s" in out and "vel_rad_s" not in out:
        out["vel_rad_s"] = out["vel_deg_s"] * DEG2RAD
        out["vel_deg_s"] = out["vel_rad_s"] * RAD2DEG

    physics = (config or {}).get("physics") or {}
    dpt = _as_float(physics.get("degrees_per_turn")) or 360.0
    closed = _as_float(physics.get("closed_position_deg"))
    opened = _as_float(physics.get("open_position_deg"))

    if "vel_rad_s" in out and "vel_turns_s" not in out and dpt:
        out["vel_turns_s"] = (out["vel_rad_s"] * RAD2DEG) / dpt
    if "pos_deg" in out and "pos_turns" not in out and dpt:
        zero = _as_float(physics.get("encoder_zero_turns")) or 0.0
        out["pos_turns"] = out["pos_deg"] / dpt + zero
    if "pos_deg" in out and closed is not None and opened is not None and opened != closed:
        out["pos_norm"] = (out["pos_deg"] - closed) / (opened - closed)

    if "acc_rad_s2" in out and "acc_deg_s2" not in out:
        out["acc_deg_s2"] = out["acc_rad_s2"] * RAD2DEG

    # Energy aliases
    if "passivity_mj" in out and "passivity_j" not in out:
        out["passivity_j"] = out["passivity_mj"] / 1000.0
    if "passivity_j" in out and "passivity_mj" not in out:
        out["passivity_mj"] = out["passivity_j"] * 1000.0

    # Mechanical power
    if "tau_nm" in out and "vel_rad_s" in out:
        out["power_w"] = out["tau_nm"] * out["vel_rad_s"]
        out["power_passive_w"] = min(0.0, out["power_w"])

    kt = _as_float((config or {}).get("kt")) or KT_DEFAULT
    if "tau_nm" in out:
        out["iq_from_tau_a"] = out["tau_nm"] / kt if kt else None
    radius = _as_float(((config or {}).get("plant") or {}).get("handle_radius_m"))
    if radius and "tau_nm" in out:
        out["force_handle_n"] = out["tau_nm"] / radius

    if "bus_v" in out and "bus_i" in out:
        out["p_elec_w"] = out["bus_v"] * out["bus_i"]

    # Wall from kinematics + config when not logged
    if "wall_pen_turns" not in out and "pos_deg" in out and dpt and closed is not None and opened is not None:
        theta = out["pos_deg"] / dpt
        th_off = closed / dpt
        th_on = opened / dpt
        pen = min(0.0, theta - th_off) + max(0.0, theta - th_on)
        out["wall_pen_turns"] = pen
        out["wall_contact"] = abs(pen) >= 1e-6
        if pen < -1e-6:
            out["wall_side"] = "closed"
        elif pen > 1e-6:
            out["wall_side"] = "open"
        else:
            out["wall_side"] = "none"
    elif "wall_pen_turns" in out and "wall_contact" not in out:
        out["wall_contact"] = abs(out["wall_pen_turns"]) >= 1e-6

    return out


def _span(samples: List[Dict[str, Any]], key: str, scale: float = 1.0) -> float:
    vals = [s[key] for s in samples if key in s and s[key] is not None]
    if len(vals) < 2:
        return 0.0
    return (float(vals[-1]) - float(vals[0])) * scale


def choose_clock(samples: List[Dict[str, Any]]) -> Tuple[Optional[str], float]:
    span_us = _span(samples, "t_us", 1e-6)
    span_ms = _span(samples, "timestamp_ms", 1e-3)
    span_host = _span(samples, "host_unix_s", 1.0)
    if span_host >= 0.2 and span_host < 7 * 86400:
        return "host_unix_s", 1.0
    if span_ms >= 0.2 and span_ms >= span_us * 1.5:
        return "timestamp_ms", 1e-3
    if span_us >= 0.05:
        return "t_us", 1e-6
    if span_ms >= 0.05:
        return "timestamp_ms", 1e-3
    if samples and samples[0].get("host_unix_s") is not None:
        return "host_unix_s", 1.0
    if samples and samples[0].get("t_us") is not None:
        return "t_us", 1e-6
    if samples and samples[0].get("timestamp_ms") is not None:
        return "timestamp_ms", 1e-3
    return "t_s", 1.0


def apply_time_base(samples: List[Dict[str, Any]], log: Optional["PrismLog"] = None) -> None:
    """Pick the most plausible clock, then shift t_s to start at 0. Mutates samples."""
    if not samples:
        return
    clock, scale = choose_clock(samples)
    if clock:
        for s in samples:
            if clock in s and s[clock] is not None:
                s["t_s"] = float(s[clock]) * scale
    t0 = samples[0].get("t_s")
    if t0 is None:
        return
    if log is not None:
        log.clock_key = clock
        log.clock_scale = scale
        log.clock_origin = float(t0)
    for s in samples:
        if "t_s" in s:
            s["t_s"] = s["t_s"] - t0
    # Drop obviously bogus heartbeat ages (uint32 wrap in some captures)
    for s in samples:
        hb = s.get("hb_age_ms")
        if hb is not None and (hb < 0 or hb > 60_000):
            s.pop("hb_age_ms", None)
    # dt and running energy
    energy = 0.0
    dissipated = 0.0
    prev_t = 0.0
    prev_tau = None
    for i, s in enumerate(samples):
        t = s.get("t_s", prev_t)
        dt = t - prev_t if i else 0.0
        if dt < 0:
            dt = 0.0
        s["dt_s"] = dt
        p = s.get("power_w")
        if p is not None and dt > 0:
            energy += p * dt
            dissipated += -min(0.0, p) * dt
        s["energy_mech_j"] = energy
        s["energy_dissipated_j"] = dissipated
        tau = s.get("tau_nm")
        if tau is not None and prev_tau is not None and dt > 0:
            s["dtau_dt_nm_s"] = (tau - prev_tau) / dt
        prev_tau = tau if tau is not None else prev_tau
        prev_t = t


# ---------------------------------------------------------------------------
# Event synthesis
# ---------------------------------------------------------------------------

def synthesize_events(samples: List[Dict[str, Any]]) -> List[Dict[str, Any]]:
    events: List[Dict[str, Any]] = []
    if not samples:
        return events

    def emit(kind: str, s: Dict[str, Any], level: str = "info", **data: Any) -> None:
        ev = {
            "rec": "event",
            "t_s": s.get("t_s", 0.0),
            "seq": s.get("seq"),
            "kind": kind,
            "level": level,
        }
        payload = {k: v for k, v in data.items() if v is not None}
        if payload:
            ev["data"] = payload
        events.append(ev)

    prev_q = None
    prev_w = None
    prev_status = None
    prev_seq = None
    prev_err = 0
    seq_steps: List[int] = []
    for s in samples:
        seq = s.get("seq")
        if prev_seq is not None and seq is not None:
            step = int(seq) - int(prev_seq)
            if 0 < step < 1_000_000:
                seq_steps.append(step)
        if seq is not None:
            prev_seq = seq
    seq_steps.sort()
    typical_step = seq_steps[len(seq_steps) // 2] if seq_steps else 1
    drop_thresh = max(typical_step * 4, typical_step + 8)
    prev_seq = None
    for s in samples:
        q = s.get("quiet")
        if prev_q is not None and q is not None and q != prev_q:
            emit("quiet_enter" if q else "quiet_exit", s)
        prev_q = q if q is not None else prev_q

        w = s.get("wall_contact")
        if prev_w is not None and w is not None and w != prev_w:
            emit(
                "wall_enter" if w else "wall_exit",
                s,
                "warn" if w else "info",
                wall_side=s.get("wall_side"),
                pos_deg=s.get("pos_deg"),
                vel_rad_s=s.get("vel_rad_s"),
            )
        prev_w = w if w is not None else prev_w

        st = s.get("status")
        if prev_status is not None and st and st != prev_status:
            if st == "error":
                emit("error", s, "error", code=s.get("last_error"))
            elif st == "idle" and prev_status == "running":
                emit("valve_stop", s)
            elif st == "running" and prev_status != "running":
                emit("valve_start", s)
        prev_status = st or prev_status

        err = int(s.get("last_error") or 0)
        if err and err != prev_err:
            kind = "runaway" if err in (101, 102) else "error"
            emit(kind, s, "error", code=err)
        prev_err = err

        seq = s.get("seq")
        if prev_seq is not None and seq is not None:
            gap = int(seq) - int(prev_seq)
            if gap > drop_thresh:
                emit("stream_drop", s, "warn", dropped=gap - typical_step)
        if seq is not None:
            prev_seq = seq

        if s.get("estop"):
            emit("estop", s, "fatal")
        if s.get("encoder_glitch"):
            emit("encoder_glitch", s, "warn")

    return events


# ---------------------------------------------------------------------------
# Stats / downsample
# ---------------------------------------------------------------------------

def _minmax_mean_rms(xs: List[float]) -> Dict[str, float]:
    n = len(xs)
    if n == 0:
        return {}
    mn = min(xs)
    mx = max(xs)
    mean = sum(xs) / n
    rms = math.sqrt(sum(x * x for x in xs) / n)
    return {
        "min": mn,
        "max": mx,
        "mean": mean,
        "rms": rms,
        "range": mx - mn,
        "last": xs[-1],
    }


def compute_stats(samples: List[Dict[str, Any]], keys: Optional[Sequence[str]] = None) -> Dict[str, Any]:
    if not samples:
        return {"samples": 0, "duration_s": 0.0, "sample_rate_hz": 0.0, "channels": {}}
    t0 = samples[0].get("t_s", 0.0)
    t1 = samples[-1].get("t_s", t0)
    dur = max(0.0, t1 - t0)
    rate = (len(samples) - 1) / dur if dur > 0 else 0.0
    if keys is None:
        keys = [c["key"] for c in CHANNELS if c["kind"] in ("float", "int", "bool")]
    ch: Dict[str, Any] = {}
    for k in keys:
        xs = []
        for s in samples:
            if k in s and s[k] is not None:
                if isinstance(s[k], bool):
                    xs.append(1.0 if s[k] else 0.0)
                else:
                    try:
                        xs.append(float(s[k]))
                    except (TypeError, ValueError):
                        pass
        if xs:
            ch[k] = _minmax_mean_rms(xs)
            ch[k]["n"] = len(xs)
    return {
        "samples": len(samples),
        "duration_s": dur,
        "sample_rate_hz": rate,
        "t0": t0,
        "t1": t1,
        "channels": ch,
    }


def lttb_indices(xs: Sequence[float], ys: Sequence[float], n_out: int) -> List[int]:
    """Largest-Triangle-Three-Buckets; returns selected indices (preserves peaks)."""
    n = len(xs)
    if n == 0:
        return []
    if n_out >= n or n_out < 3:
        return list(range(n))
    chosen = [0]
    bucket_size = (n - 2) / (n_out - 2)
    a = 0
    for i in range(n_out - 2):
        start = int(math.floor((i + 1) * bucket_size)) + 1
        end = int(math.floor((i + 2) * bucket_size)) + 1
        end = min(end, n)
        avg_x = 0.0
        avg_y = 0.0
        cnt = max(1, end - start)
        for j in range(start, end):
            avg_x += xs[j]
            avg_y += ys[j]
        avg_x /= cnt
        avg_y /= cnt
        range_start = int(math.floor(i * bucket_size)) + 1
        range_end = int(math.floor((i + 1) * bucket_size)) + 1
        range_end = min(range_end, n)
        ax, ay = xs[a], ys[a]
        max_area = -1.0
        next_a = range_start
        for j in range(range_start, range_end):
            area = abs((ax - avg_x) * (ys[j] - ay) - (ax - xs[j]) * (avg_y - ay))
            if area > max_area:
                max_area = area
                next_a = j
        chosen.append(next_a)
        a = next_a
    chosen.append(n - 1)
    return chosen


def lttb(xs: Sequence[float], ys: Sequence[float], n_out: int) -> Tuple[List[float], List[float]]:
    idx = lttb_indices(xs, ys, n_out)
    return [xs[i] for i in idx], [ys[i] for i in idx]


def _num(v: Any) -> Optional[float]:
    if v is None:
        return None
    if isinstance(v, bool):
        return 1.0 if v else 0.0
    try:
        x = float(v)
    except (TypeError, ValueError):
        return None
    if math.isnan(x) or math.isinf(x):
        return None
    return x


def series_bundle(
    samples: List[Dict[str, Any]],
    keys: Sequence[str],
    t0: Optional[float] = None,
    t1: Optional[float] = None,
    n_out: int = 4000,
) -> List[Dict[str, Any]]:
    """Downsample several keys on a shared time index so phase plots stay aligned."""
    rows: List[Dict[str, Any]] = []
    for s in samples:
        t = s.get("t_s")
        if t is None:
            continue
        if t0 is not None and t < t0:
            continue
        if t1 is not None and t > t1:
            continue
        rows.append(s)
    ts = [float(s["t_s"]) for s in rows]
    cols: Dict[str, List[Optional[float]]] = {k: [_num(s.get(k)) for s in rows] for k in keys}
    n_raw = len(ts)
    idx = list(range(n_raw))
    if n_out and n_raw > n_out:
        ref_key = next((k for k in keys if any(v is not None for v in cols[k])), keys[0] if keys else None)
        if ref_key is not None:
            yref = [0.0 if v is None else v for v in cols[ref_key]]
            idx = lttb_indices(ts, yref, n_out)
    out = []
    for key in keys:
        meta = CHANNEL_BY_KEY.get(key, {})
        xs: List[float] = []
        ys: List[float] = []
        col = cols.get(key, [])
        for i in idx:
            v = col[i] if i < len(col) else None
            if v is None:
                continue
            xs.append(ts[i])
            ys.append(v)
        out.append(
            {
                "key": key,
                "label": meta.get("label", key),
                "unit": meta.get("unit", ""),
                "color": meta.get("color", "#94a3b8"),
                "group": meta.get("group", ""),
                "derived": meta.get("derived", False),
                "n_raw": n_raw,
                "n": len(xs),
                "t": xs,
                "y": ys,
            }
        )
    return out


def series_for(
    samples: List[Dict[str, Any]],
    key: str,
    t0: Optional[float] = None,
    t1: Optional[float] = None,
    n_out: int = 4000,
) -> Dict[str, Any]:
    bundled = series_bundle(samples, [key], t0, t1, n_out)
    return bundled[0] if bundled else {
        "key": key, "label": key, "unit": "", "color": "#94a3b8",
        "group": "", "derived": False, "n_raw": 0, "n": 0, "t": [], "y": [],
    }


# ---------------------------------------------------------------------------
# File parse
# ---------------------------------------------------------------------------

class PrismLog:
    def __init__(self) -> None:
        self.path: Optional[str] = None
        self.format: str = "unknown"  # prism-log | stream-jsonl | csv | empty
        self.header: Dict[str, Any] = {}
        self.configs: List[Dict[str, Any]] = []
        self.samples: List[Dict[str, Any]] = []
        self.events: List[Dict[str, Any]] = []
        self.footer: Optional[Dict[str, Any]] = None
        self.stats: Dict[str, Any] = {}
        self.present_keys: List[str] = []
        self.clock_key: Optional[str] = None
        self.clock_scale: float = 1.0
        self.clock_origin: Optional[float] = None
        self.file_nbytes: int = 0
        self.partial_line: str = ""
        self.lock = threading.Lock()
        self.live: bool = False

    @property
    def config(self) -> Dict[str, Any]:
        return self.configs[0] if self.configs else {}

    def present_channels(self) -> List[Channel]:
        present = set(self.present_keys)
        out = [c for c in CHANNELS if c["key"] in present and c["key"] != "t_s"]
        # keep spec order
        return out

    def _norm_cfg(self) -> Dict[str, Any]:
        cfg = dict(self.config or {})
        if self.header.get("plant"):
            cfg["plant"] = self.header["plant"]
        kt = (self.header.get("device") or {}).get("torque_constant_nm_per_a")
        if kt:
            cfg["kt"] = kt
        return cfg

    def _stamp_clock(self, s: Dict[str, Any]) -> None:
        if self.live and self.clock_key is None and s.get("host_unix_s") is not None:
            self.clock_key = "host_unix_s"
            self.clock_scale = 1.0
        key = self.clock_key
        scale = self.clock_scale
        if key and key in s and s[key] is not None:
            abs_t = float(s[key]) * scale
        elif s.get("t_s") is not None:
            abs_t = float(s["t_s"])
        elif s.get("host_unix_s") is not None:
            key = "host_unix_s"
            scale = 1.0
            abs_t = float(s["host_unix_s"])
        elif s.get("t_us") is not None:
            key = "t_us"
            scale = 1e-6
            abs_t = float(s["t_us"]) * scale
        elif s.get("timestamp_ms") is not None:
            key = "timestamp_ms"
            scale = 1e-3
            abs_t = float(s["timestamp_ms"]) * scale
        else:
            return
        if self.clock_origin is None:
            self.clock_key = key
            self.clock_scale = scale
            self.clock_origin = abs_t
        s["t_s"] = abs_t - self.clock_origin

    def _append_energy(self, s: Dict[str, Any]) -> None:
        hb = s.get("hb_age_ms")
        if hb is not None and (hb < 0 or hb > 60_000):
            s.pop("hb_age_ms", None)
        if not self.samples:
            s["dt_s"] = 0.0
            s["energy_mech_j"] = 0.0
            s["energy_dissipated_j"] = 0.0
            return
        prev = self.samples[-1]
        dt = float(s.get("t_s") or 0) - float(prev.get("t_s") or 0)
        if dt < 0:
            dt = 0.0
        s["dt_s"] = dt
        p = s.get("power_w")
        energy = float(prev.get("energy_mech_j") or 0)
        dissipated = float(prev.get("energy_dissipated_j") or 0)
        if p is not None and dt > 0:
            energy += p * dt
            dissipated += -min(0.0, p) * dt
        s["energy_mech_j"] = energy
        s["energy_dissipated_j"] = dissipated
        tau = s.get("tau_nm")
        prev_tau = prev.get("tau_nm")
        if tau is not None and prev_tau is not None and dt > 0:
            s["dtau_dt_nm_s"] = (tau - prev_tau) / dt

    def _edge_events(self, s: Dict[str, Any]) -> None:
        if not self.samples:
            return
        prev = self.samples[-1]
        t = s.get("t_s", 0.0)

        def emit(kind: str, level: str = "info", **data: Any) -> None:
            ev = {"rec": "event", "t_s": t, "seq": s.get("seq"), "kind": kind, "level": level}
            payload = {k: v for k, v in data.items() if v is not None}
            if payload:
                ev["data"] = payload
            self.events.append(ev)

        if prev.get("quiet") is not None and s.get("quiet") is not None and prev["quiet"] != s["quiet"]:
            emit("quiet_enter" if s["quiet"] else "quiet_exit")
        if prev.get("wall_contact") is not None and s.get("wall_contact") is not None and prev["wall_contact"] != s["wall_contact"]:
            emit("wall_enter" if s["wall_contact"] else "wall_exit", "warn" if s["wall_contact"] else "info")

    def ingest_obj(self, obj: Dict[str, Any]) -> bool:
        """Append one JSON record. Returns True if a sample was added."""
        rec = obj.get("rec")
        if rec == "header":
            self.header = obj
            return False
        if rec == "config":
            self.configs.append(obj)
            return False
        if rec == "event":
            ev = dict(obj)
            if ev.get("t_s") is None and self.samples:
                ev["t_s"] = self.samples[-1].get("t_s", 0.0)
            self.events.append(ev)
            return False
        if rec == "footer":
            self.footer = obj
            return False
        if rec not in (None, "sample"):
            return False
        s = normalize_sample(obj, self._norm_cfg())
        self._stamp_clock(s)
        if s.get("t_s") is None:
            return False
        self._append_energy(s)
        self._edge_events(s)
        self.samples.append(s)
        for k in s.keys():
            if k not in self.present_keys:
                self.present_keys.append(k)
        return True

    def ingest_line(self, line: str) -> bool:
        s = line.strip()
        if not s or s.startswith("#"):
            return False
        try:
            obj = json.loads(s)
        except json.JSONDecodeError:
            return False
        if not isinstance(obj, dict):
            return False
        if obj.get("stream"):
            return False
        return self.ingest_obj(obj)

    def refresh_stats(self) -> None:
        keys = [k for k in self.present_keys if k != "t_s"]
        self.stats = compute_stats(self.samples, keys)

    def tail_file(self) -> int:
        """Read newly appended JSONL bytes. Returns number of new samples."""
        path = self.path
        if not path or path.endswith(".gz") or not os.path.isfile(path):
            return 0
        try:
            size = os.path.getsize(path)
        except OSError:
            return 0
        if size < self.file_nbytes:
            # File was replaced; caller should full-reload.
            return -1
        if size == self.file_nbytes and not self.partial_line:
            return 0
        with open(path, "rb") as fh:
            fh.seek(self.file_nbytes)
            chunk = fh.read()
        self.file_nbytes = size
        text = self.partial_line + chunk.decode("utf-8", "replace")
        parts = text.split("\n")
        self.partial_line = parts[-1]
        n = 0
        with self.lock:
            for line in parts[:-1]:
                if self.ingest_line(line):
                    n += 1
            if n:
                self.refresh_stats()
        return n

    def to_meta(self) -> Dict[str, Any]:
        watchable = bool(self.path) and not str(self.path).endswith(".gz")
        return {
            "path": self.path,
            "format": self.format,
            "header": self.header,
            "config": self.config,
            "n_samples": len(self.samples),
            "n_events": len(self.events),
            "stats": self.stats,
            "channels": self.present_channels(),
            "groups": GROUP_LABELS,
            "group_order": GROUP_ORDER,
            "defaults": [c["key"] for c in CHANNELS if c.get("default") and c["key"] in set(self.present_keys)],
            "watchable": watchable,
            "live": self.live,
        }


def _looks_like_csv(first: str) -> bool:
    return ("," in first) and not first.lstrip().startswith("{") and not first.lstrip().startswith("#")


def _parse_csv(text_iter: Iterable[str]) -> List[Dict[str, Any]]:
    import csv

    rows = list(csv.DictReader(text_iter))
    return rows


def parse_log(path: str) -> PrismLog:
    log = PrismLog()
    log.path = path
    raw_samples: List[Dict[str, Any]] = []
    wrote_magic = False
    first_data: Optional[str] = None

    with _open_text(path) as fh:
        # Peek first non-empty
        buf_lines = []
        for line in fh:
            buf_lines.append(line)
            if first_data is None:
                s = line.strip()
                if s:
                    first_data = s
        if first_data is None:
            log.format = "empty"
            return log

        if first_data == MAGIC or first_data.startswith("# PRISM-LOG"):
            wrote_magic = True
            log.format = "prism-log"
        elif _looks_like_csv(first_data):
            log.format = "csv"
        else:
            log.format = "stream-jsonl"

        if log.format == "csv":
            raw_rows = _parse_csv(buf_lines)
            cfg = {}
            for row in raw_rows:
                raw_samples.append(row)
            log.header = {
                "rec": "header",
                "format": "PRISM-LOG",
                "version": "1.0",
                "schema_level": "stream",
                "source": {"kind": "csv", "path": os.path.basename(path)},
            }
            for row in raw_samples:
                log.samples.append(normalize_sample(row, cfg))
        else:
            cfg_for_norm: Dict[str, Any] = {}
            for line in buf_lines:
                s = line.strip()
                if not s or s.startswith("#"):
                    continue
                try:
                    obj = json.loads(s)
                except json.JSONDecodeError:
                    continue
                rec = obj.get("rec")
                if rec == "header":
                    log.header = obj
                    continue
                if rec == "config":
                    log.configs.append(obj)
                    cfg_for_norm = obj
                    continue
                if rec == "event":
                    log.events.append(obj)
                    continue
                if rec == "footer":
                    log.footer = obj
                    continue
                if rec == "sample" or rec is None:
                    raw_samples.append(obj)

            if not log.header:
                log.header = {
                    "rec": "header",
                    "format": "PRISM-LOG",
                    "version": "1.0",
                    "schema_level": "stream" if not wrote_magic else "full",
                    "source": {"kind": log.format, "path": os.path.basename(path)},
                }
            cfg = log.configs[0] if log.configs else {}
            # Merge plant/kt into config for derived fields
            if log.header.get("plant"):
                cfg = dict(cfg)
                cfg["plant"] = log.header["plant"]
            if (log.header.get("device") or {}).get("torque_constant_nm_per_a"):
                cfg = dict(cfg)
                cfg["kt"] = log.header["device"]["torque_constant_nm_per_a"]
            for row in raw_samples:
                log.samples.append(normalize_sample(row, cfg))

    apply_time_base(log.samples, log)

    # Drop samples with no time
    log.samples = [s for s in log.samples if s.get("t_s") is not None]
    if path and os.path.isfile(path) and not str(path).endswith(".gz"):
        try:
            log.file_nbytes = os.path.getsize(path)
        except OSError:
            log.file_nbytes = 0

    # Present keys: anything that appears at least once
    keys = OrderedDict()
    for s in log.samples:
        for k in s.keys():
            keys[k] = True
    log.present_keys = list(keys.keys())

    if not log.events:
        log.events = synthesize_events(log.samples)
    else:
        # still add synthesized edges that authors didn't write
        have = {(e.get("kind"), round(float(e.get("t_s") or 0), 3)) for e in log.events}
        for e in synthesize_events(log.samples):
            sig = (e.get("kind"), round(float(e.get("t_s") or 0), 3))
            if sig not in have:
                log.events.append(e)
        log.events.sort(key=lambda e: float(e.get("t_s") or 0))

    log.stats = compute_stats(log.samples, [k for k in log.present_keys if k != "t_s"])
    if "channels" in log.header and isinstance(log.header["channels"], list):
        # union
        extra = [k for k in log.header["channels"] if k not in log.present_keys]
        log.present_keys.extend(extra)
    return log


def parse_bytes(data: bytes, name: str = "upload.log") -> PrismLog:
    import tempfile

    gz = name.endswith(".gz") or (len(data) >= 2 and data[:2] == b"\x1f\x8b")
    suffix = ".log.gz" if gz else ".log"
    fd, path = tempfile.mkstemp(suffix=suffix)
    try:
        os.write(fd, data)
        os.close(fd)
        log = parse_log(path)
        log.path = name
        return log
    finally:
        try:
            os.unlink(path)
        except OSError:
            pass


# ---------------------------------------------------------------------------
# Writer
# ---------------------------------------------------------------------------

def write_log(
    path: str,
    header: Dict[str, Any],
    samples: Sequence[Dict[str, Any]],
    *,
    config: Optional[Dict[str, Any]] = None,
    events: Optional[Sequence[Dict[str, Any]]] = None,
    footer: Optional[Dict[str, Any]] = None,
) -> None:
    opener = gzip.open if path.endswith(".gz") else open
    mode = "wt" if path.endswith(".gz") else "w"
    with opener(path, mode, encoding="utf-8") as fh:  # type: ignore[arg-type]
        fh.write(MAGIC + "\n")
        h = {"rec": "header"}
        h.update(header)
        h["rec"] = "header"
        h.setdefault("format", "PRISM-LOG")
        h.setdefault("version", "1.0")
        fh.write(json.dumps(h, separators=(",", ":")) + "\n")
        if config:
            c = {"rec": "config"}
            c.update(config)
            c["rec"] = "config"
            fh.write(json.dumps(c, separators=(",", ":")) + "\n")
        evs = list(events or [])
        ev_i = 0
        evs.sort(key=lambda e: float(e.get("t_s") or 0))
        for s in samples:
            t = float(s.get("t_s") or 0)
            while ev_i < len(evs) and float(evs[ev_i].get("t_s") or 0) <= t:
                e = dict(evs[ev_i])
                e["rec"] = "event"
                fh.write(json.dumps(e, separators=(",", ":")) + "\n")
                ev_i += 1
            row = {"rec": "sample"}
            for k, v in s.items():
                if v is None or k == "rec":
                    continue
                if isinstance(v, float):
                    row[k] = round(v, 6) if abs(v) < 1e5 else v
                else:
                    row[k] = v
            fh.write(json.dumps(row, separators=(",", ":")) + "\n")
        while ev_i < len(evs):
            e = dict(evs[ev_i])
            e["rec"] = "event"
            fh.write(json.dumps(e, separators=(",", ":")) + "\n")
            ev_i += 1
        if footer:
            f = dict(footer)
            f["rec"] = "footer"
            fh.write(json.dumps(f, separators=(",", ":")) + "\n")


# ---------------------------------------------------------------------------
# Demo generator — physically plausible quarter-turn
# ---------------------------------------------------------------------------

def generate_demo(
    duration_s: float = 8.0,
    hz: float = 200.0,
    *,
    seed: int = 7,
    hitl: bool = False,
) -> Tuple[Dict[str, Any], Dict[str, Any], List[Dict[str, Any]], List[Dict[str, Any]]]:
    """Synthetic full-schema recording of a 0→90° turn with wall contact.

    Not a bit-accurate replay of valve_physics.c, but every catalog channel is
    populated so the viewer can demonstrate the complete standard.
    """
    rng = random.Random(seed)
    dt = 1.0 / hz
    n = int(duration_s * hz)
    b = 0.20
    tau_c = 0.20
    kw = 40.0
    cw = 1.2
    closed, opened = 0.0, 90.0
    dpt = 360.0
    kt = KT_DEFAULT
    radius = 0.055
    J = 0.004
    pos = 2.0  # deg
    vel = 0.0  # rad/s
    pass_j = 0.0
    samples: List[Dict[str, Any]] = []
    events: List[Dict[str, Any]] = [
        {"t_s": 0.0, "kind": "session_start", "level": "info"},
        {"t_s": 0.05, "kind": "valve_start", "level": "info"},
        {"t_s": 0.05, "kind": "odrive_enable", "level": "info"},
    ]
    quiet_prev = True
    wall_prev = False
    temp_fet = 34.0
    temp_mot = 32.5
    bus_v = 24.1
    enc_seq = 0
    last_enc_t = 0.0

    for i in range(n):
        t = i * dt
        # Operator torque: rest → open to the 90° stop → hold → close
        if t < 0.5:
            tau_user = 0.0
        elif t < 1.1:
            tau_user = 0.22 * ((t - 0.5) / 0.6)
        elif t < 3.2:
            tau_user = 0.20
        elif t < 4.0:
            tau_user = 0.06
        elif t < 4.6:
            tau_user = 0.0
        elif t < 7.2:
            tau_user = -0.20
        else:
            tau_user = 0.0

        omega = vel
        theta_turns = pos / dpt
        th_off = closed / dpt
        th_on = opened / dpt
        pen = min(0.0, theta_turns - th_off) + max(0.0, theta_turns - th_on)
        in_wall = abs(pen) >= 1e-6
        wall_side = "open" if pen > 1e-6 else ("closed" if pen < -1e-6 else "none")

        quiet = (not in_wall) and abs(omega) < (0.08 if True else 0.16) and abs(tau_user) < 0.05
        # Schmitt-ish quiet
        if quiet_prev and abs(omega) < 0.16 and abs(tau_user) < 0.08 and not in_wall:
            quiet = True

        if in_wall:
            tau_visc = 0.0
            tau_coul = 0.0
            pen_soft = pen / (1.0 + abs(pen) / 0.05)
            omega_turns = (omega * RAD2DEG) / dpt
            tau_ws = -kw * pen_soft
            tau_wd = -cw * omega_turns
            tau_wall = max(-2.5, min(2.5, tau_ws + tau_wd))
        else:
            tau_visc = -b * omega
            cdir = 1.0 if omega > 0.08 else (-1.0 if omega < -0.08 else (1.0 if omega >= 0 else -1.0))
            cscale = 0.0
            if abs(omega) > 0.5:
                u = min(1.0, (abs(omega) - 0.5) / 1.5)
                cscale = u * u * (3 - 2 * u)
            elif abs(omega) > 0.02:
                cscale = 0.35
            tau_coul = -(tau_c * max(cscale, 0.4 if abs(omega) > 0.02 else 0.0)) * (cdir if abs(omega) > 0.02 else 0.0)
            tau_ws = tau_wd = tau_wall = 0.0

        if quiet:
            tau_visc = 0.0
            tau_coul = 0.0

        tau_phys = tau_visc + tau_coul + tau_wall
        # soft free-space sat
        if not in_wall:
            a = abs(tau_phys)
            L = 2.5
            if a > 1e-9:
                tau_phys = tau_phys * (L / (L + a))
        tau_clamped = max(-0.5, min(0.5, tau_phys))
        tau_filt = tau_clamped  # already slow
        tau_cmd = tau_filt

        # Plant: operator torque + haptic resistance. Walls are stiff stops.
        tau_net = tau_user + tau_cmd
        alpha = tau_net / J
        vel = vel + alpha * dt
        vel *= 0.992
        vel = max(-4.0, min(4.0, vel))
        pos = pos + vel * RAD2DEG * dt
        if pos > opened + 8.0:
            pos = opened + 8.0
            vel = min(0.0, vel)
        if pos < closed - 8.0:
            pos = closed - 8.0
            vel = max(0.0, vel)

        power = tau_cmd * vel
        pass_j += power * dt
        pass_j = max(-0.20, min(0.0, pass_j))

        temp_fet += (0.02 * abs(tau_cmd) * 12 - (temp_fet - 33.5) * 0.01) * dt
        temp_mot += (0.015 * abs(tau_cmd) * 12 - (temp_mot - 32.0) * 0.008) * dt
        iq = tau_cmd / kt
        bus_i = iq * 0.35 + rng.uniform(-0.02, 0.02)
        bus_v = 24.05 - 0.04 * abs(bus_i) + rng.uniform(-0.01, 0.01)

        new_enc = (t - last_enc_t) >= 0.001 - 1e-9
        if new_enc:
            enc_seq += 1
            last_enc_t = t
        enc_period = 1000 + rng.randint(-4, 4)
        enc_age = rng.randint(80, 380)
        enc_to_tx = rng.randint(90, 140) if new_enc else rng.randint(200, 400)
        loop_us = 11 + rng.randint(0, 3)

        if quiet != quiet_prev:
            events.append(
                {
                    "t_s": t,
                    "kind": "quiet_enter" if quiet else "quiet_exit",
                    "level": "info",
                }
            )
        if in_wall != wall_prev:
            events.append(
                {
                    "t_s": t,
                    "kind": "wall_enter" if in_wall else "wall_exit",
                    "level": "warn" if in_wall else "info",
                    "data": {"wall_side": wall_side, "pos_deg": pos, "vel_rad_s": vel},
                }
            )
        quiet_prev = quiet
        wall_prev = in_wall

        noise_p = rng.uniform(-0.02, 0.02)
        noise_v = rng.uniform(-0.008, 0.008)
        s: Dict[str, Any] = {
            "t_s": t,
            "t_us": int(t * 1e6),
            "timestamp_ms": int(t * 1000) + 12000,
            "host_unix_s": 1758206640.0 + t,
            "seq": i * 40,  # 8 kHz loop, 200 Hz log
            "pos_deg": pos + noise_p,
            "pos_turns": (pos + noise_p) / dpt,
            "vel_rad_s": vel + noise_v,
            "vel_raw_rad_s": vel + noise_v * 2.5,
            "vel_odrive_rad_s": vel + noise_v * 0.4,
            "acc_rad_s2": alpha,
            "tau_nm": tau_cmd,
            "tau_physics_nm": tau_phys,
            "tau_clamped_nm": tau_clamped,
            "tau_filt_nm": tau_filt,
            "tau_viscous_nm": tau_visc,
            "tau_coulomb_nm": tau_coul,
            "tau_wall_nm": tau_wall,
            "tau_wall_spring_nm": tau_ws,
            "tau_wall_damp_nm": tau_wd,
            "wall_pen_turns": pen,
            "wall_contact": in_wall,
            "wall_side": wall_side,
            "quiet": quiet,
            "settle_residual": False,
            "coulomb_dir": 1.0 if vel >= 0 else -1.0,
            "coulomb_scale": 0.0 if quiet or in_wall else min(1.0, abs(vel) / 2.0),
            "passivity_j": pass_j,
            "iq_set_a": iq,
            "iq_meas_a": iq + rng.uniform(-0.05, 0.05),
            "bus_v": bus_v,
            "bus_i": bus_i,
            "axis_state": 8,
            "axis_error": 0,
            "temp_fet_c": temp_fet,
            "temp_motor_c": temp_mot,
            "temp_fet_peak_c": temp_fet,
            "temp_motor_peak_c": temp_mot,
            "loop_time_us": loop_us,
            "enc_seq": enc_seq,
            "enc_age_us": enc_age,
            "enc_to_tx_us": enc_to_tx,
            "enc_period_us": enc_period,
            "new_enc": new_enc,
            "hb_age_ms": 4 + (i % 12),
            "can_tx": 800 + i,
            "can_rx": 790 + i,
            "can_err": 0,
            "can_tec": 0,
            "can_rec": 0,
            "data_valid": True,
            "estop": False,
            "last_error": 0,
            "status": "running",
            "output_mode": "hitl" if hitl else "odrive",
            "interaction": "human",
        }
        if hitl:
            s["hitl_tau_nm"] = tau_cmd
            s["hitl_pos_deg"] = pos
            s["hitl_vel_rad_s"] = vel
            s["hitl_rtt_us"] = 400 + rng.randint(0, 250)
            s["sim_alpha_rad_s2"] = alpha
        samples.append(s)

    events.append({"t_s": duration_s, "kind": "session_stop", "level": "info"})

    header = {
        "format": "PRISM-LOG",
        "version": "1.0",
        "schema_level": "full",
        "created_utc": "2026-09-18T16:04:00.000Z",
        "created_unix_s": 1758206640.0,
        "session_id": "demo-quarter-turn",
        "title": "Demo: quarter-turn 0→90° with wall contact",
        "notes": "Synthetic full-schema recording for the log viewer. Not captured from hardware.",
        "device": {
            "name": "PRISM",
            "ip": "127.0.0.1",
            "firmware": "demo",
            "mcu": "STM32H753ZI",
            "motor_controller": "ODrive S1",
            "odrive_node_id": 1,
            "torque_constant_nm_per_a": kt,
        },
        "plant": {
            "output_mode": "hitl" if hitl else "odrive",
            "interaction_mode": "human",
            "handle_id": "quarter-turn-handle",
            "handle_name": "Quarter-turn Handle",
            "handle_radius_m": radius,
            "task": "quarter_turn",
            "preset_name": "demo",
        },
        "timebase": {
            "control_loop_hz": 8000,
            "encoder_broadcast_hz": 1000,
            "sample_rate_hz": hz,
        },
        "source": {"kind": "demo", "recorder": "prism_log.generate_demo"},
    }
    config = {
        "t_s": 0.0,
        "physics": {
            "viscous_nm_s_per_rad": b,
            "coulomb_nm": tau_c,
            "wall_stiffness_nm_per_turn": kw,
            "wall_damping_nm_s_per_turn": cw,
            "smoothing_eps_rad_s": 0.30,
            "coulomb_hyst_rad_s": 0.08,
            "torque_limit_nm": 0.50,
            "closed_position_deg": closed,
            "open_position_deg": opened,
            "degrees_per_turn": dpt,
        },
        "gates": {"quiet_enable": True, "vel_source": "odrive"},
        "hitl": {"enabled": hitl, "inertia_kg_m2": J},
        "plant": header["plant"],
        "kt": kt,
    }
    return header, config, samples, events


def write_demo(path: str, **kwargs: Any) -> PrismLog:
    header, config, samples, events = generate_demo(**kwargs)
    footer = {
        "t_s": samples[-1]["t_s"] if samples else 0,
        "samples": len(samples),
        "duration_s": samples[-1]["t_s"] if samples else 0,
        "sample_rate_hz": kwargs.get("hz", 200.0),
    }
    write_log(path, header, samples, config=config, events=events, footer=footer)
    return parse_log(path)


def convert_file(src: str, dest: str, *, title: Optional[str] = None) -> PrismLog:
    log = parse_log(src)
    header = dict(log.header)
    header["schema_level"] = header.get("schema_level") or "stream"
    header["title"] = title or os.path.splitext(os.path.basename(src))[0]
    header.setdefault("created_utc", _utc_now())
    header.setdefault("source", {"kind": "convert", "path": os.path.basename(src)})
    config = log.config or None
    write_log(dest, header, log.samples, config=config, events=log.events, footer=log.footer)
    return parse_log(dest)


if __name__ == "__main__":
    import argparse

    ap = argparse.ArgumentParser(description="PRISM-LOG utilities")
    sub = ap.add_subparsers(dest="cmd", required=True)
    p_demo = sub.add_parser("demo", help="Write a full-schema demo .log")
    p_demo.add_argument("-o", "--output", default="samples/demo_quarter_turn.log")
    p_conv = sub.add_parser("convert", help="Convert stream JSONL/CSV to PRISM-LOG")
    p_conv.add_argument("src")
    p_conv.add_argument("-o", "--output", required=True)
    p_info = sub.add_parser("info", help="Print summary of a log")
    p_info.add_argument("path")
    args = ap.parse_args()
    if args.cmd == "demo":
        os.makedirs(os.path.dirname(args.output) or ".", exist_ok=True)
        log = write_demo(args.output)
        print(f"wrote {args.output}  samples={len(log.samples)}  keys={len(log.present_keys)}")
    elif args.cmd == "convert":
        os.makedirs(os.path.dirname(args.output) or ".", exist_ok=True)
        log = convert_file(args.src, args.output)
        print(f"wrote {args.output}  samples={len(log.samples)}")
    elif args.cmd == "info":
        log = parse_log(args.path)
        print(json.dumps(log.to_meta()["stats"], indent=2))
        print("keys:", ", ".join(log.present_keys[:40]))
