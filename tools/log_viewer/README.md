# PRISM Log Viewer

Localhost tool for inspecting [PRISM-LOG/1.0](../../docs/log/prism-log-standard.md) recordings.

Default view is the shaft essentials: **position**, **velocity**, **torque**, and **mechanical power**, with playhead readouts, stats, and event markers. Other tabs expose haptic internals, motor electricals, loop timing, and phase portraits (`θ–ω`, `τ–ω`).

## Run

```bash
python3 tools/log_viewer/server.py --open
```

Then browse `http://127.0.0.1:8742/`.

The server binds loopback only. It serves:

- bundled samples in `tools/log_viewer/samples/`
- existing captures in repo `logs/` (stream JSONL / CSV accepted)
- drag-and-drop / Open for `.log`, `.log.gz`, `.jsonl`, `.csv`

No third-party Python packages.

On load the **whole recording** is plotted (position, velocity, torque, power). **Play demo** loads the bundled quarter-turn and plays it from t = 0; the traces stay on screen while the playhead and KPI cards scrub through them.

| Control | Action |
|---------|--------|
| ▶ / ⏸ | Replay from the playhead (does not hide traces unless “Hide future” is on) |
| ⏮ | Jump to t = 0 and show the full window |
| scrubber | Drag to any time; KPIs follow |
| Follow live | Keep the playhead on the latest sample (growing file or device stream) |
| Hide future | Tape-reveal: only draw up to the playhead |
| Connect | Live TCP stream from a PRISM (`ip:8888`) |
| scroll / drag / click | zoom, pan, scrub a plot |
| double-click | show all time |

## Record a live session

```bash
python3 tools/log_viewer/record.py --ip 192.168.1.100 -o capture.log
python3 tools/log_viewer/record.py --ip 192.168.1.100 --rest --duration 30 -o capture.log
```

`--rest` polls `/api/v1/config`, `/odrive`, `/timing`, `/interaction`, `/hitl` at ~10 Hz and merges those channels onto the TCP stream samples.

## Convert an existing stream file

```bash
python3 tools/log_viewer/prism_log.py convert logs/lever_test_20260903_105824.jsonl -o tools/log_viewer/samples/lever_test.log
python3 tools/log_viewer/prism_log.py demo -o tools/log_viewer/samples/demo_quarter_turn.log
python3 tools/log_viewer/prism_log.py info capture.log
```

## Format

See [PRISM Log Format 1.0](../../docs/log/prism-log-standard.md).
