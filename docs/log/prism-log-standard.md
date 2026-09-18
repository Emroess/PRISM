# PRISM Log Format 1.0

**File extension:** `.log`  
**MIME type:** `application/x-prism-log`  
**Encoding:** UTF-8 (optional GZIP: `.log.gz`)  
**Line protocol:** newline-delimited JSON (NDJSON / JSONL)

This document is the canonical description of a PRISM session log. A compliant
file records everything physically observable on a PRISM plant — kinematics,
haptic torque, motor electricals, thermal state, CAN/timing, HITL/sim, and
task metadata — plus the derived quantities that follow from those signals.

The live TCP stream on port 8888 is a *subset* of this format (see
[Firmware stream mapping](#13-firmware-stream-mapping)). Recorders SHOULD
promote stream packets into this format and SHOULD poll REST/CAN for the
slower electrical and configuration channels that the 8 kHz loop does not
currently emit.

---

## 1. Design goals

1. **Complete.** If a quantity exists on the shaft, in the ODrive S1, on the
   CAN bus, in the STM32 control loop, or in Isaac Sim HITL, it has a name
   here — even if today's firmware does not yet serialize it.
2. **Self-describing.** A file opens without a separate sidecar. Header +
   config records carry units, plant identity, and the physics that produced
   the torque.
3. **Streamable.** Append-only. A viewer can plot a file that is still being
   written.
4. **Research-friendly.** JSON, SI units, explicit sign convention, no
   implicit scale factors.
5. **Lossy-tolerant.** Missing fields are omitted, never sent as `0`. A
   minimal file with only time, position, velocity, and torque is valid.

---

## 2. File structure

```
# PRISM-LOG/1.0
{"rec":"header", ...}
{"rec":"config", ...}
{"rec":"sample", ...}
{"rec":"sample", ...}
{"rec":"event",  ...}
{"rec":"sample", ...}
{"rec":"config", ...}      # optional mid-run parameter change
{"rec":"footer", ...}      # optional
```

### 2.1 Magic line

The first non-empty line MUST be exactly:

```
# PRISM-LOG/1.0
```

No trailing spaces. Subsequent `#` lines are comments and MUST be ignored by
readers.

### 2.2 Records

Every non-comment line is one JSON object with a required `rec` field:

| `rec`    | Required | Cardinality | Purpose |
|----------|----------|-------------|---------|
| `header` | yes      | exactly one, first JSON record | Session identity |
| `config` | recommended | ≥ 0; first one before samples | Physics / hardware snapshot |
| `sample` | yes for a useful log | 0..N | Time-series telemetry |
| `event`  | no       | 0..N | Discrete state changes |
| `footer` | no       | 0 or 1, last record | Summary statistics |

Unknown `rec` values MUST be skipped (forward compatible).

Unknown fields inside a known record MUST be preserved by pass-through tools
and MAY be ignored by viewers.

### 2.3 JSON rules

- IEEE-754 numbers. No hex floats. Integers may be JSON numbers.
- Booleans are JSON `true` / `false`, never `0` / `1`.
- Enumerations are lowercase strings (`"running"`, `"human"`, `"odrive"`).
- `null` means “sensor existed but this sample is invalid”. Omit the key if
  the channel was never sourced.
- NaN / Inf MUST NOT appear. Drop the field or emit `null`.
- Time fields use `.` as the decimal separator.

### 2.4 Compression

`.log.gz` is GZIP of the exact UTF-8 bytes. Readers MUST accept both.

---

## 3. Units and sign convention

| Quantity        | Unit (primary) | Also accepted |
|-----------------|----------------|---------------|
| Time            | s, µs, ms      | Unix seconds  |
| Angle           | deg            | rad, turns    |
| Angular rate    | rad/s          | deg/s, turn/s |
| Angular accel.  | rad/s²         | deg/s²        |
| Torque          | N·m            | —             |
| Force           | N              | —             |
| Current         | A              | —             |
| Voltage         | V              | —             |
| Temperature     | °C             | —             |
| Energy          | J              | mJ in legacy stream |
| Power           | W              | —             |
| Inertia         | kg·m²          | —             |
| Stiffness       | N·m/turn       | N·m/rad       |
| Damping         | N·m·s/rad      | N·m·s/turn    |
| Length          | m              | mm in metadata only |

**Sign convention (shaft frame)**

- Positive position is the encoder-positive direction after
  `encoder_zero_turns` is subtracted, scaled by `degrees_per_turn`.
- Positive velocity is `d(pos)/dt` in that same frame.
- Positive torque is the haptic torque commanded *on the shaft* in the
  encoder-positive direction. Firmware currently uses `VALVE_TORQUE_SIGN = +1`
  (ODrive positive torque).
- Dissipative physics produces `tau * omega < 0` (viscous and Coulomb oppose
  motion). Wall torque is restoring (`-k * penetration`).
- Passivity tank energy is stored as a **non-positive** joule value: more
  negative means more dissipative energy in the tank. The firmware cap is
  `VALVE_PASSIVITY_ENERGY_CAP_J = 0.20 J`.

**Mechanical scaling**

```
pos_deg   = (pos_turns - encoder_zero_turns) * degrees_per_turn
pos_rad   = pos_deg * π / 180
vel_deg_s = vel_rad_s * 180 / π
vel_turns_s = vel_rad_s / (2π)   if degrees_per_turn == 360
            = vel_deg_s / degrees_per_turn
```

Default `degrees_per_turn` is `360`. Handle adapters that gear the shaft MUST
put the true mechanical scale here, not in a hidden constant.

---

## 4. Header record

Exactly one. First JSON record after the magic line.

```json
{
  "rec": "header",
  "format": "PRISM-LOG",
  "version": "1.0",
  "schema_level": "full",
  "created_utc": "2026-09-18T16:04:00.000Z",
  "created_unix_s": 1758206640.0,
  "session_id": "20260918T160400Z-prism-01",
  "title": "Quarter-turn, robot mode, 90° travel",
  "notes": "",
  "device": {
    "name": "PRISM",
    "ip": "192.168.1.100",
    "firmware": "unknown",
    "mcu": "STM32H753ZI",
    "motor_controller": "ODrive S1",
    "odrive_node_id": 1,
    "torque_constant_nm_per_a": 0.083,
    "encoder_cpr": null,
    "can_nominal_bps": 250000,
    "can_data_bps": 5000000
  },
  "plant": {
    "output_mode": "odrive",
    "interaction_mode": "human",
    "handle_id": "quarter-turn-handle",
    "handle_name": "Quarter-turn Handle",
    "handle_radius_m": 0.055,
    "task": "quarter_turn",
    "preset_index": 0,
    "preset_name": "default"
  },
  "timebase": {
    "control_loop_hz": 8000,
    "encoder_broadcast_hz": 1000,
    "stream_hz": 100,
    "sample_rate_hz": 100.0,
    "host_clock": "unix",
    "device_clock": "dwt_us"
  },
  "channels": ["t_s", "seq", "pos_deg", "vel_rad_s", "tau_nm"],
  "source": {
    "kind": "stream",
    "path": "tcp://192.168.1.100:8888",
    "recorder": "tools/log_viewer/record.py",
    "host": "operator-laptop"
  }
}
```

### 4.1 Header fields

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `format` | string | yes | `"PRISM-LOG"` |
| `version` | string | yes | `"1.0"` |
| `schema_level` | string | no | `minimal` \| `stream` \| `full` (see §12) |
| `created_utc` | string | yes | ISO-8601 UTC timestamp of recording start |
| `created_unix_s` | number | no | Same instant as Unix seconds |
| `session_id` | string | recommended | Unique id, filename-safe |
| `title` | string | no | Human label |
| `notes` | string | no | Free text |
| `device` | object | recommended | Hardware identity |
| `plant` | object | recommended | Handle, mode, preset |
| `timebase` | object | recommended | Loop and log rates |
| `channels` | string[] | recommended | Sample keys present in this file |
| `source` | object | no | How the file was produced |

### 4.2 `plant.output_mode`

| Value | Meaning |
|-------|---------|
| `odrive` | Physical ODrive, CAN encoder in, CAN torque out |
| `hitl` | Isaac Sim / HITL TCP (port 8889). Passivity tank disabled |
| `both` | Reserved (side-by-side physical + sim) |

### 4.3 `plant.interaction_mode`

| Value | Meaning |
|-------|---------|
| `human` | Quiet gate, Coulomb speed schedule, ε smoothing, torque LPF, settle blank |
| `robot` | Stiction-accurate: Coulomb present at rest, no quiet/ε/LPF |

### 4.4 `plant.task`

| Value | Typical travel | Notes |
|-------|----------------|-------|
| `multi_turn` | 4 turns (1440°) | Hydrant handwheel |
| `quarter_turn` | 90° | Ball / butterfly |
| `door_handle` | ±45°, self-centering | Return spring via walls + stiffness |
| `wrench` | small angle, high torque | Fastener tightening |
| `custom` | from config | Anything else |

---

## 5. Config record

A snapshot of the haptic law and runtime knobs. Emit one before the first
sample. Emit another whenever any field changes (REST/CLI/web edit).

```json
{
  "rec": "config",
  "t_s": 0.0,
  "physics": {
    "viscous_nm_s_per_rad": 0.20,
    "coulomb_nm": 0.20,
    "wall_stiffness_nm_per_turn": 1.00,
    "wall_damping_nm_s_per_turn": 0.10,
    "smoothing_eps_rad_s": 0.30,
    "coulomb_hyst_rad_s": 0.08,
    "torque_limit_nm": 0.50,
    "closed_position_deg": 0.0,
    "open_position_deg": 90.0,
    "degrees_per_turn": 360.0,
    "encoder_zero_turns": 0.0
  },
  "auto": {
    "at_baseline": true,
    "coulomb_deadband_rad_s": 0.50,
    "coulomb_full_rad_s": 2.00,
    "coulomb_eps_rad_s": 0.30,
    "settle_arm_rad_s": 0.25,
    "settle_blank_rad_s": 0.45,
    "wall_tau_max_nm": 2.5,
    "free_space_tau_max_nm": 2.5,
    "torque_lpf_hz": 400.0,
    "velocity_lpf_hz": 30.0,
    "torque_slew_nm_per_s": 100.0
  },
  "gates": {
    "quiet_enable": true,
    "quiet_enter_rad_s": 0.08,
    "quiet_exit_rad_s": 0.16,
    "vel_source": "odrive"
  },
  "safety": {
    "omega_max_rad_s": 12.0,
    "runaway_omega_rad_s": 10.0,
    "runaway_omega_hard_rad_s": 18.0,
    "passivity_energy_cap_j": 0.20,
    "encoder_delta_max_turns": 0.025,
    "odrive_vel_limit_turns_s": 20.0,
    "odrive_current_limit_a": 120.0
  },
  "hitl": {
    "enabled": false,
    "inertia_kg_m2": 0.001,
    "bearing_damping_nm_s_per_rad": 0.005,
    "pos_limit_deg": 360.0
  }
}
```

`vel_source` is one of `delta`, `odrive`, `lpf_delta` (firmware
`VALVE_VEL_SOURCE_*`).

If a config record arrives mid-file, samples after it are interpreted against
the new law. Viewers SHOULD draw a vertical marker at `t_s`.

---

## 6. Sample record — channel catalog

This is the payload. A sample is one instant of plant state. Fields are
**flat** so they plot and export to CSV without walking nested objects.

Minimum useful sample:

```json
{"rec":"sample","t_s":1.234,"seq":9876,"pos_deg":45.2,"vel_rad_s":0.81,"tau_nm":0.184}
```

Full sample (every key the standard defines). Recorders write only the keys
they actually sourced; this listing is the complete physical vocabulary.

### 6.1 Timebase

| Key | Type | Unit | Rate | Source | Description |
|-----|------|------|------|--------|-------------|
| `t_s` | number | s | every sample | derived | Preferred plot axis. `t_us / 1e6` if present, else host time minus start |
| `t_us` | integer | µs | control | DWT accum. | Monotonic device time (`diag.t_us_accum`) |
| `timestamp_ms` | integer | ms | control | systick | STM32 `HAL_GetTick` / `board_get_systick_ms` |
| `host_unix_s` | number | s | host | recorder | Host wall clock when the packet was received |
| `dt_s` | number | s | derived | derived | Instantaneous sample period |
| `seq` | integer | 1 | control | `sample_seq` | Control-loop sequence, wraps at 2³² |
| `interpolated` | bool | — | recorder | recorder | `true` if this row was synthesized across a drop |

At least one of `t_s`, `t_us`, `timestamp_ms`, `host_unix_s` is REQUIRED.

### 6.2 Kinematics (shaft)

Everything a rotary encoder plus finite differences can say about a 1-DoF
shaft.

| Key | Type | Unit | Rate | Source | Description |
|-----|------|------|------|--------|-------------|
| `pos_turns` | number | turn | encoder | ODrive `pos_estimate` | Raw encoder estimate, **before** zero subtract |
| `pos_deg` | number | deg | encoder | haptic state | Mechanical angle after zero and `degrees_per_turn` |
| `pos_rad` | number | rad | derived | derived | `pos_deg * π/180` |
| `pos_norm` | number | 1 | derived | derived | 0 at closed, 1 at open. May leave `[0,1]` in over-travel |
| `pos_counts` | integer | count | encoder | ODrive shadow count | Cumulative encoder counts |
| `pos_cmd_deg` | number | deg | control | state | Legacy mirror of measured position (not a setpoint) |
| `vel_rad_s` | number | rad/s | encoder | haptic `omega_rad_s` | **Primary velocity** — filtered, used by Coulomb / quiet |
| `vel_raw_rad_s` | number | rad/s | encoder | `omega_raw_rad_s` | Unfiltered Δθ/Δt (viscous-lead candidate; currently unused by physics) |
| `vel_odrive_rad_s` | number | rad/s | encoder | ODrive `vel_estimate` | On-drive observer, scaled to rad/s |
| `vel_deg_s` | number | deg/s | derived | derived | `vel_rad_s * 180/π` |
| `vel_turns_s` | number | turn/s | derived | derived | Encoder-frame rate |
| `acc_rad_s2` | number | rad/s² | control | `alpha_rad_s2` | `(ω − ω_prev) * loop_hz` |
| `acc_deg_s2` | number | deg/s² | derived | derived | |
| `jerk_rad_s3` | number | rad/s³ | derived | derived | `d(acc)/dt`. Noisy; log only if explicitly enabled |
| `encoder_glitch` | bool | — | encoder | haptic | Single-sample jump `> VALVE_ENCODER_DELTA_MAX_TURNS` (0.025 turn ≈ 9°) rejected |

**Physical sensors behind these fields**

- ODrive S1 encoder (typically magnetic, CPR in the thousands) →
  `GET_ENCODER_ESTIMATES` (cmd 0x09) at `VALVE_ENCODER_BROADCAST_HZ` (1000 Hz
  design).
- Optional `GET_ENCODER_COUNT` (cmd 0x0A) → `pos_counts`, `encoder_cpr`.
- HITL path substitutes Isaac Sim integrator `{pos_deg, vel_rad_s}` for the
  CAN encoder.

### 6.3 Torque and force

The haptic law is resistive: viscous + Coulomb in free space, spring-damper
walls at the stops, then clamp, LPF, passivity, sign.

| Key | Type | Unit | Rate | Source | Description |
|-----|------|------|------|--------|-------------|
| `tau_nm` | number | N·m | control | commanded drive torque | **Primary torque** after LPF / passivity / sign. This is what went to the motor or HITL |
| `tau_physics_nm` | number | N·m | control | HIL output | Raw `valve_physics_calculate_torque_hil` before clamp/LPF/tank |
| `tau_clamped_nm` | number | N·m | control | clamp | After `torque_limit_nm` |
| `tau_filt_nm` | number | N·m | control | torque LPF | Human mode only; robot mode equals `tau_nm` |
| `tau_prev_nm` | number | N·m | control | state | Previous command (slew / discontinuity diagnostics) |
| `tau_viscous_nm` | number | N·m | control | physics | `−b · ω` (free space; 0 in walls in human mode) |
| `tau_coulomb_nm` | number | N·m | control | physics | `−τ_c · scale · dir` (Schmitt / ε-smoothed) |
| `tau_wall_nm` | number | N·m | control | physics | Total virtual-wall torque |
| `tau_wall_spring_nm` | number | N·m | control | physics | `−k_w · pen_soft` |
| `tau_wall_damp_nm` | number | N·m | control | physics | `−c_w · ω_turns` |
| `dtau_dt_nm_s` | number | N·m/s | derived | derived | Torque derivative |
| `force_handle_n` | number | N | derived | derived | `tau_nm / handle_radius_m` when radius is known |
| `iq_from_tau_a` | number | A | derived | derived | `tau_nm / torque_constant_nm_per_a` (0.083 default) |

`tau_viscous_nm` / `tau_coulomb_nm` / `tau_wall_*` are **internal** today —
computed in `valve_physics.c` but not streamed. A full-schema recorder on
device SHOULD export them. Offline tools MAY reconstruct an approximation
from `config` + `pos` + `vel` but MUST mark `tau_*` reconstructions with
`event.kind = "derived_physics"` or by omitting them (never silently pretend
they were measured).

### 6.4 Physics internals

| Key | Type | Unit | Rate | Source | Description |
|-----|------|------|------|--------|-------------|
| `wall_pen_turns` | number | turn | control | physics | Signed penetration past `[closed, open]` |
| `wall_contact` | bool | — | control | physics | `abs(wall_pen_turns) ≥ 1e-6` |
| `wall_side` | string | — | control | physics | `none` \| `closed` \| `open` |
| `quiet` | bool | — | control | quiet gate | Free-space b and τ_c suppressed; walls stay on |
| `settle_residual` | bool | — | control | settle blank | Mid-coast free-space blank after an energetic peak |
| `coulomb_dir` | number | 1 | control | Schmitt | `-1`, `0` (uncommitted), `+1` |
| `coulomb_scale` | number | 1 | control | speed schedule | 0..1 (smoothstep deadband→full). Robot mode = 1 |
| `free_space_sat` | bool | — | control | physics | Soft free-space cap `τ·L/(\|τ\|+L)` was material |
| `passivity_j` | number | J | control | energy tank | ≤ 0. Stream field `passivity_mj` is this × 1000 |
| `passivity_mj` | number | mJ | stream | stream | Legacy millijoule integer from TCP stream |
| `power_w` | number | W | derived | derived | Instantaneous mechanical power `tau_nm * vel_rad_s` |
| `power_passive_w` | number | W | derived | derived | `min(0, power_w)` (dissipated) |
| `energy_mech_j` | number | J | derived | derived | Running integral of `power_w` from t=0 |
| `energy_dissipated_j` | number | J | derived | derived | Running integral of `-min(0, power_w)` |

### 6.5 Motor electrical (ODrive S1)

These are physically present on the drive and already decoded in
`can_simple.h`. Broadcast periods on stock S1 firmware: Iq ~100 ms,
temperature ~1000 ms, bus V/I cyclic.

| Key | Type | Unit | Rate | Source | Description |
|-----|------|------|------|--------|-------------|
| `iq_set_a` | number | A | ~10 Hz | CAN 0x14 | Quadrature current setpoint |
| `iq_meas_a` | number | A | ~10 Hz | CAN 0x14 | Measured Iq |
| `id_meas_a` | number | A | — | not on CAN-simple | Direct-axis current (not exported by CANSIMPLE) |
| `bus_v` | number | V | ~10 Hz | CAN 0x17 | DC bus voltage |
| `bus_i` | number | A | ~10 Hz | CAN 0x17 | DC bus current (signed) |
| `p_elec_w` | number | W | derived | derived | `bus_v * bus_i` |
| `eta_mech` | number | 1 | derived | derived | `power_w / p_elec_w` when `p_elec_w > ε` |
| `axis_state` | integer | — | ~10 Hz | heartbeat | See §11.1 |
| `axis_error` | integer | — | ~10 Hz | heartbeat | Bitfield, 0 = healthy |
| `motor_flags` | integer | — | on demand | CAN 0x03 | Motor error flags |
| `encoder_flags` | integer | — | on demand | CAN 0x04 | Encoder error flags |
| `controller_status` | integer | — | heartbeat | heartbeat | Controller status byte |
| `control_mode` | integer | — | config | CAN 0x0B | 0 V, 1 torque, 2 vel, 3 pos. Valve uses 1 |
| `input_mode` | integer | — | config | CAN 0x0B | Passthrough = 1 |

### 6.6 Thermal

ODrive S1 enforces its own FET/motor limits. PRISM only *observes*.

| Key | Type | Unit | Rate | Source | Description |
|-----|------|------|------|--------|-------------|
| `temp_fet_c` | number | °C | ~1 Hz | CAN 0x15 | Inverter FET temperature |
| `temp_motor_c` | number | °C | ~1 Hz | CAN 0x15 | Motor thermistor |
| `temp_fet_peak_c` | number | °C | session | safety diag | Peak FET this run |
| `temp_motor_peak_c` | number | °C | session | safety diag | Peak motor this run |
| `temp_mcu_c` | number | °C | — | STM32 | On-die sensor (not currently logged) |
| `temp_ambient_c` | number | °C | — | external | Not on the stock plant |

### 6.7 Timing and I/O latency

PRISM is a real-time haptic loop. These fields are how you prove it.

| Key | Type | Unit | Rate | Source | Description |
|-----|------|------|------|--------|-------------|
| `loop_time_us` | integer | µs | control | DWT | This iteration's execute time |
| `loop_time_min_us` | integer | µs | session | diag | Running min |
| `loop_time_max_us` | integer | µs | session | diag | Running max |
| `enc_seq` | integer | 1 | encoder | CAN seq | Last ODrive encoder broadcast sequence |
| `enc_rx_us` | integer | µs | encoder | DWT | Device time the encoder CAN frame arrived |
| `enc_age_us` | integer | µs | control | diag | Age of that frame at torque TX |
| `enc_to_tx_us` | integer | µs | encoder | diag | RX ISR → first torque TX that consumed it |
| `enc_period_us` | integer | µs | encoder | diag | Interval between encoder frames (~1000 µs design) |
| `new_enc` | bool | — | control | diag | This loop consumed a fresh encoder frame |
| `hb_age_ms` | integer | ms | ~10 Hz | heartbeat cache | Age of last ODrive heartbeat |
| `telemetry_age_ms` | integer | ms | rest | diag | Age of encoder telemetry at last REST snapshot |
| `can_retry` | integer | 1 | control | diag | CAN retries this iteration |

### 6.8 CAN bus health

From `can_bus_status` / FDCAN PSR. Slow, but physically real and the first
thing to look at when encoder glitches appear.

| Key | Type | Unit | Rate | Source | Description |
|-----|------|------|------|--------|-------------|
| `can_tx` | integer | 1 | rest | FDCAN | TX frame count |
| `can_rx` | integer | 1 | rest | FDCAN | RX frame count |
| `can_tx_fail` | integer | 1 | rest | FDCAN | TX failures |
| `can_err` | integer | 1 | rest | FDCAN | Error count |
| `can_rx_lost` | integer | 1 | rest | FDCAN | RX FIFO lost |
| `can_rx_full` | integer | 1 | rest | FDCAN | RX FIFO full events |
| `can_rx_drop` | integer | 1 | rest | software | Software ring drops |
| `can_proto_err` | integer | 1 | rest | protocol | CANSIMPLE parse errors |
| `can_bus_off` | integer | 1 | rest | FDCAN | Bus-off events |
| `can_tec` | integer | 1 | rest | PSR | TX error counter |
| `can_rec` | integer | 1 | rest | PSR | RX error counter |
| `can_cel` | integer | 1 | rest | PSR | CAN error logging counter |
| `can_psr` | integer | — | rest | PSR | Protocol status register raw |

### 6.9 Safety and mode flags

| Key | Type | Unit | Rate | Source | Description |
|-----|------|------|------|--------|-------------|
| `status` | string | — | control | state | `idle` \| `initializing` \| `running` \| `error` |
| `estop` | bool | — | control | state | Emergency stop latched |
| `data_valid` | bool | — | stream | stream | Finite, in-range kinematics + torque |
| `output_mode` | string | — | config | haptic | `odrive` \| `hitl` |
| `interaction` | string | — | config | haptic | `human` \| `robot` |
| `last_error` | integer | — | on fault | safety | Last ODrive axis error or PRISM code (101 hard runaway, 102 held runaway) |
| `err_axis_n` | integer | 1 | session | safety | Axis error event count |
| `err_motor_n` | integer | 1 | session | safety | Motor error event count |
| `err_enc_n` | integer | 1 | session | safety | Encoder error event count |
| `err_stale_n` | integer | 1 | session | safety | Encoder stale events |
| `err_timeout_n` | integer | 1 | session | safety | Encoder timeout events |
| `err_disc_n` | integer | 1 | session | safety | Torque discontinuity events |
| `estop_n` | integer | 1 | session | safety | ESTOP count this boot |
| `tau_dot_max_nm_ms` | number | N·m/ms | session | safety | Peak torque derivative observed |

`status` in the live TCP stream is currently uppercase (`RUNNING`). Writers
MUST lowercase it. Readers MUST accept both.

### 6.10 HITL / Isaac Sim

Present when `output_mode == "hitl"` (or `both`).

| Key | Type | Unit | Rate | Source | Description |
|-----|------|------|------|--------|-------------|
| `hitl_connected` | bool | — | session | HITL TCP | Isaac client currently connected |
| `hitl_tau_nm` | number | N·m | control | HITL out | Torque frame sent to sim |
| `hitl_pos_deg` | number | deg | control | HITL in | Simulated joint angle |
| `hitl_vel_rad_s` | number | rad/s | control | HITL in | Simulated joint rate |
| `hitl_seq` | integer | 1 | control | HITL out | Seq stamped on the torque frame |
| `hitl_t_us` | integer | µs | control | HITL out | Device time stamped on the torque frame |
| `hitl_rtt_us` | integer | µs | derived | derived | Torque send → matching encoder return |
| `hitl_timeout_n` | integer | 1 | session | HITL | Encoder stale declarations |
| `hitl_parse_err_n` | integer | 1 | session | HITL | Malformed inbound JSON |
| `sim_dt_s` | number | s | sim | integrator | Isaac / HITL client step |
| `sim_inertia_kg_m2` | number | kg·m² | config | HITL client | `J` in `α = (τ − bω)/J` |
| `sim_alpha_rad_s2` | number | rad/s² | sim | integrator | Simulated angular acceleration |

On the HITL path the physical encoder is *not* in the loop; `pos_deg` /
`vel_rad_s` SHOULD equal `hitl_pos_deg` / `hitl_vel_rad_s`. Logging both lets
you catch injection bugs.

### 6.11 Task / benchmark (optional per sample)

| Key | Type | Unit | Description |
|-----|------|------|-------------|
| `task_progress` | number | 1 | 0..1 toward success (e.g. angle / travel) |
| `task_success` | bool | — | Inside the success region this sample |
| `turns_completed` | number | turn | Unwrapped travel from session start / 360 |

---

## 7. Event record

Sparse markers. Do not encode periodic signals as events.

```json
{
  "rec": "event",
  "t_s": 4.102,
  "seq": 32816,
  "kind": "wall_enter",
  "level": "info",
  "message": "open-stop contact",
  "data": {"wall_side": "open", "pos_deg": 90.4, "vel_rad_s": 1.1}
}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `t_s` | number | yes | Event time on the sample axis |
| `seq` | integer | no | Nearest control sequence |
| `kind` | string | yes | Machine-stable identifier (see table) |
| `level` | string | no | `debug` \| `info` \| `warn` \| `error` \| `fatal` |
| `message` | string | no | Human text |
| `data` | object | no | Kind-specific payload |

### 7.1 Standard `kind` values

| Kind | When |
|------|------|
| `session_start` / `session_stop` | Recorder start/stop |
| `valve_start` / `valve_stop` | Haptic loop start/stop |
| `odrive_enable` / `odrive_disable` | Axis closed-loop / idle |
| `estop` | Emergency stop |
| `error` | Axis/motor/encoder error (`data.code`) |
| `error_clear` | Errors cleared |
| `runaway` | ω trip (hard or held) |
| `encoder_glitch` | Rejected Δθ |
| `encoder_timeout` | Stale encoder / HITL timeout |
| `quiet_enter` / `quiet_exit` | Quiet gate |
| `settle_enter` / `settle_exit` | Residual settle blank |
| `wall_enter` / `wall_exit` | Virtual stop contact |
| `config_change` | Any physics field changed (`data` = diff) |
| `mode_change` | `interaction` or `output_mode` changed |
| `preset_load` | Preset applied |
| `stream_drop` | Sequence gap (`data.dropped`) |
| `hitl_connect` / `hitl_disconnect` | Isaac client |
| `calibrate` | ODrive calibration |
| `marker` | Operator annotation |

Recorders that only have the TCP stream SHOULD synthesize `quiet_*`,
`wall_*` (if travel is known), `estop`, and `stream_drop` from samples.

---

## 8. Footer record

Optional. Written once when recording stops.

```json
{
  "rec": "footer",
  "t_s": 32.410,
  "samples": 3241,
  "dropped": 2,
  "duration_s": 32.410,
  "sample_rate_hz": 99.97,
  "pos_deg": {"min": -1.2, "max": 92.4, "mean": 44.1},
  "vel_rad_s": {"min": -3.1, "max": 2.8, "rms": 0.74},
  "tau_nm": {"min": -0.49, "max": 0.50, "rms": 0.18},
  "energy_dissipated_j": 1.02,
  "estops": 0
}
```

Viewers MUST compute their own stats from samples; the footer is a
convenience, not a source of truth.

---

## 9. Derived quantities (MUST be reproducible)

If a file omits a derived key, a reader SHALL compute it as follows. Do not
write derived keys unless they were computed with these formulae (or the
header notes a different definition).

```
pos_rad              = pos_deg * π / 180
vel_deg_s            = vel_rad_s * 180 / π
vel_turns_s          = vel_deg_s / degrees_per_turn
pos_norm             = (pos_deg - closed) / (open - closed)
power_w              = tau_nm * vel_rad_s
power_passive_w      = min(0, power_w)
p_elec_w             = bus_v * bus_i
iq_from_tau_a        = tau_nm / kt          # kt default 0.083
force_handle_n       = tau_nm / handle_radius_m
energy_mech_j[k]     = energy_mech_j[k-1] + power_w[k] * dt_s[k]
energy_dissipated_j  = Σ -min(0, power_w) * dt
wall_pen_turns       = min(0, θ_turns - θ_closed) + max(0, θ_turns - θ_open)
wall_contact         = abs(wall_pen_turns) ≥ 1e-6
```

`dt_s` is `Δt_us / 1e6` when `t_us` is present, else `Δhost_unix_s`, else
`1 / sample_rate_hz`.

---

## 10. Sampling guidance

| Class | Design rate | What belongs there |
|-------|-------------|--------------------|
| Control | 8 kHz | Torque command, loop time, passivity, quiet, physics internals |
| Encoder | 1 kHz | Position, velocity, `new_enc`, encoder latency |
| Stream | 10–100 Hz (TCP 8888) | What the current firmware actually emits |
| Motor telem | ~10 Hz | Iq, bus V/I |
| Thermal | ~1 Hz | FET / motor temperature |
| Config | on change | Physics parameters |
| Event | on edge | Mode, faults, walls, quiet |

A `full` log at control rate is ~8 kB/s per 100-byte sample ≈ 28 MB/hour at
100 Hz, or ~2.3 GB/hour at 8 kHz JSONL. For long captures:

- Prefer 1 kHz (one sample per encoder frame, `new_enc == true`).
- GZIP.
- Or store `schema_level: "stream"` at the TCP interval and a short
  `full` snippet around events of interest.

Decimation MUST NOT average flags. Booleans use OR in the bin (`quiet` is
true if it was true anywhere in the bin). Extrema of `tau_nm` / `vel_rad_s`
SHOULD be preserved (min and max per bin) if the plot path downsamples.

---

## 11. Enumerations

### 11.1 ODrive axis state

| Value | Name |
|------:|------|
| 0 | undefined |
| 1 | idle |
| 2 | startup_sequence |
| 3 | full_calibration |
| 4 | motor_calibration |
| 6 | encoder_index_search |
| 7 | encoder_offset_calibration |
| 8 | closed_loop_control |
| 9 | lockin_spin |
| 10 | encoder_dir_find |
| 11 | homing |

Valve haptic operation requires `8`.

### 11.2 PRISM valve status

| Stream (legacy) | Log value | Firmware |
|-----------------|-----------|----------|
| `IDLE` | `idle` | `VALVE_STATE_IDLE` (0) |
| — | `initializing` | `VALVE_STATE_INITIALIZING` (1) |
| `RUNNING` | `running` | `VALVE_STATE_RUNNING` (2) |
| `ERROR` | `error` | `VALVE_STATE_ERROR` (3) |

### 11.3 PRISM error codes (non-ODrive)

| Code | Meaning |
|-----:|---------|
| 0 | none |
| 101 | runaway, instantaneous `\|ω\| ≥ 18 rad/s` |
| 102 | runaway, `\|ω\| ≥ 10 rad/s` held for 30 ms |

ODrive `axis_error` bitfields are logged raw; decode with ODrive S1
documentation.

---

## 12. Schema levels

| Level | Meaning |
|-------|---------|
| `minimal` | `t_s`, `seq`, `pos_deg`, `vel_rad_s`, `tau_nm` |
| `stream` | Everything the TCP stream currently sends, normalized |
| `full` | All sourced channels in §6 that the recorder could obtain |

A file MAY mix rates: electricals hold-last between 10 Hz updates; kinematics
update every sample. Hold-last MUST NOT invent motion — omit the slow key
until a fresh reading, or set it only on samples where it was measured and
let the viewer step-hold.

---

## 13. Firmware stream mapping

Current TCP JSON (port 8888) → PRISM-LOG sample keys.

| Stream field | Log key | Transform |
|--------------|---------|-----------|
| `timestamp_ms` | `timestamp_ms` | as-is |
| `t_us` | `t_us` | as-is; also `t_s = t_us / 1e6` |
| `loop_time_us` | `loop_time_us` | as-is |
| `seq` | `seq` | as-is |
| `position_turns` | `pos_turns` | as-is |
| `position_deg` | `pos_deg` | as-is |
| `omega_rad_s` | `vel_rad_s` | as-is |
| `torque_nm` | `tau_nm` | as-is |
| `filt_torque_nm` | `tau_filt_nm` | as-is |
| `status` | `status` | lowercase |
| `passivity_mj` | `passivity_mj` | as-is; also `passivity_j = passivity_mj / 1000` |
| `quiet` | `quiet` | as-is |
| `err` | `last_error` | as-is |
| `hb_age` | `hb_age_ms` | as-is |
| `enc_seq` | `enc_seq` | as-is |
| `enc_age_us` | `enc_age_us` | as-is |
| `enc_to_tx_us` | `enc_to_tx_us` | as-is |
| `enc_period_us` | `enc_period_us` | as-is |
| `new_enc` | `new_enc` | as-is |
| `data_valid` | `data_valid` | as-is |
| `wall_time` | `host_unix_s` | host receive time (some loggers) |

REST `GET /api/v1/status`, `/odrive`, `/can`, `/timing`, `/config`, `/hitl`,
`/interaction` fill the remaining motor, CAN, thermal, and config channels
when a recorder polls them.

---

## 14. What is physically available but not yet streamed

These exist on the plant or in RAM. A `full` implementation SHOULD add them
to the 1 kHz path (or a sideband REST poll). Until then, leave the keys out.

| Channel | Where it already lives |
|---------|------------------------|
| `vel_raw_rad_s`, `acc_rad_s2` | `struct valve_state` |
| `tau_viscous_nm`, `tau_coulomb_nm`, `tau_wall_*`, `wall_pen_turns`, `coulomb_dir`, `coulomb_scale`, `settle_residual` | `valve_physics.c` (not currently stored; would need locals promoted to state) |
| `iq_set_a`, `iq_meas_a` | `can_simple_get_cached_iq` |
| `bus_v`, `bus_i` | `can_simple_get_cached_bus_voltage_current` |
| `temp_fet_c`, `temp_motor_c` | `can_simple_get_cached_temperature` |
| `pos_counts`, encoder CPR | `GET_ENCODER_COUNT` |
| `axis_state`, `axis_error` | cached heartbeat |
| CAN PSR counters | `can_get_bus_status` |
| HITL RTT | `hitl_server` timestamps |
| MCU die temperature | STM32 ADC (not wired) |
| Handle 6-axis F/T | **not on stock PRISM** — only reconstruct `force_handle_n` from torque and radius |
| Motor Id, phase voltages, encoder magnetic field | ODrive USB protocol, not CANSIMPLE |

PRISM is a 1-DoF rotary plant. There is no linear encoder, no IMU, no
pressure transducer, and no multi-axis load cell on the stock bill of
materials. Those keys are intentionally absent.

---

## 15. Reader requirements

A compliant reader:

1. Accepts `.log` and `.log.gz`.
2. Accepts legacy STEVE TCP JSONL / CSV **without** the magic line, mapping
   fields per §13, and treats that as `schema_level: "stream"`.
3. Computes missing derived keys per §9.
4. Does not assume a fixed sample rate — uses `t_s` / `t_us` / `seq`.
5. Plots `pos_deg`, `vel_rad_s`, `tau_nm` as the default view.
6. Ignores unknown keys and unknown `rec` values.

---

## 16. Writer requirements

A compliant writer:

1. Emits the magic line and a `header` record first.
2. Emits a `config` record before samples whenever the values are known.
3. Uses the keys in this document; does not invent aliases.
4. Omits unsourced keys.
5. Lowercases `status`.
6. Uses seconds (`t_s`) as the plot time even if it also writes `t_us`.
7. Flushes a newline after every record (crash-safe).
8. On stop, MAY write a `footer`.

Reference writer: `tools/log_viewer/record.py`.  
Reference reader / viewer: `tools/log_viewer/server.py`.

---

## 17. Filename convention

```
prism_<YYYYMMDD>T<HHMMSS>Z_<session>_<schema>.log
```

Example: `prism_20260918T160400Z_quarter_turn_stream.log`

Not required for validity. The header `session_id` is the stable identifier.

---

## 18. Minimal complete example

```
# PRISM-LOG/1.0
{"rec":"header","format":"PRISM-LOG","version":"1.0","schema_level":"minimal","created_utc":"2026-09-18T16:04:00.000Z","session_id":"demo","channels":["t_s","seq","pos_deg","vel_rad_s","tau_nm"],"timebase":{"sample_rate_hz":100.0,"control_loop_hz":8000,"encoder_broadcast_hz":1000}}
{"rec":"config","t_s":0.0,"physics":{"viscous_nm_s_per_rad":0.2,"coulomb_nm":0.2,"wall_stiffness_nm_per_turn":1.0,"wall_damping_nm_s_per_turn":0.1,"closed_position_deg":0.0,"open_position_deg":90.0,"degrees_per_turn":360.0,"torque_limit_nm":0.5}}
{"rec":"event","t_s":0.0,"kind":"session_start","level":"info"}
{"rec":"sample","t_s":0.00,"seq":1,"pos_deg":0.0,"vel_rad_s":0.0,"tau_nm":0.0,"quiet":true,"status":"running"}
{"rec":"sample","t_s":0.01,"seq":81,"pos_deg":0.4,"vel_rad_s":0.70,"tau_nm":-0.18,"quiet":false,"status":"running"}
{"rec":"footer","t_s":0.01,"samples":2,"duration_s":0.01}
```

---

## 19. Changelog

| Version | Date | Notes |
|---------|------|-------|
| 1.0 | 2026-09-18 | Initial standard. Covers kinematics, haptic internals, ODrive electricals, thermal, CAN, timing, HITL, task, events. Maps firmware TCP stream 1:1. |
