# PRISM Twin Implementation Plan (Actionable)

## Phase A — Specification and Baseline
- [x] A1. Author firmware-faithful twin specification.
- [x] A2. Link every requirement to at least one code module and one validation check.

## Phase B — Source-of-Truth Parameter Pipeline
- [x] B1. Implement extractor to parse firmware defaults from `firmware/src/valve/valve_nvm.c`.
- [x] B2. Generate machine-readable twin preset artifact (`generated_firmware_presets.json`).
- [x] B3. Enforce loader usage in sim (remove hard-coded divergent defaults).
- [x] B4. Add parity check utility that reports diffs between extracted and loaded values.

## Phase C — Controller Fidelity
- [x] C1. Keep core HIL equation parity in dedicated controller module.
- [x] C2. Implement quiet gate semantics.
- [x] C3. Add torque low-pass filter stage with configurable cutoff.
- [x] C4. Add passivity energy tank guard.
- [x] C5. Add deterministic fixed-step runner (1 kHz lockstep).

## Phase D — Nonideality Modeling
- [x] D1. Add actuator lag, saturation, and slew limits.
- [x] D2. Add command delay/jitter queue hooks.
- [x] D3. Add sensor quantization and delay/staleness hooks.
- [ ] D4. Add configuration profile for nonideality parameters.

## Phase E — Telemetry Contract Parity
- [x] E1. Implement firmware-compatible status payload keys and units.
- [x] E2. Include safety and temperature placeholder fields.
- [x] E3. Add schema checker for required fields and types.

## Phase F — Validation and Tuning
- [x] F1. Create scripted excitation profiles (step, chirp, reversals, wall impacts).
- [x] F2. Compute metrics (RMSE, peak error, settling, overshoot, chatter, phase lag).
- [x] F3. Add threshold config and pass/fail gating.
- [x] F4. Generate confidence report artifact per run.

## Phase G — Multi-Instance and Target Routing
- [x] G1. Add runtime instance manager for multiple PRISM devices in one scene.
- [x] G2. Namespace model entities per instance and expose instance registry.
- [x] G3. Add API target scoping (`instance_id`) with explicit default.
- [x] G4. Add `target_kind` and `target_id` to status/config responses.

## Phase H — Handle Catalog and Geometry Swapping
- [x] H1. Define handle metadata schema and create initial catalog file.
- [x] H2. Implement handle loader with default assumptions (units=inches, z_offset_mm=105, Y_to_Z).
- [x] H3. Implement runtime handle swap for selected instance.
- [x] H4. Add validation checks for handle mesh folder completeness.

## Phase I — Simulation UI and Interaction
- [x] I1. Add simulation helper UI widget (SIM badge, instance selector, handle selector).
- [x] I2. Wire UI actions to API for target+handle selection.
- [ ] I3. Add MuJoCo click-drag handle interaction mode (native viewport callback path).
- [x] I4. Stream telemetry during interactive manipulation.

## Phase J — Multi-Instance Validation
- [ ] J1. Add validation scenarios with two or more PRISM instances.
- [ ] J2. Verify API and telemetry isolation by `instance_id`.
- [ ] J3. Add regression tests for handle swap correctness and persistence.

## Immediate Work Started (Now)
1. Implement Phase B (preset extraction + loader parity). ✅
2. Implement Phase E baseline telemetry parity. ✅
3. Scaffold deterministic runner hook (Phase C5). ✅

## Next Actions (Priority Order)
1. Build `instance_id` runtime/API routing (Phase G1-G4).
2. Add handle catalog + loader + swap path (Phase H1-H3).
3. Implement minimal sim helper UI for target and handle selection (Phase I1-I2).
4. Add click-drag interaction with live telemetry (Phase I3-I4).
5. Extend validation to multi-instance isolation checks (Phase J1-J2).
