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

## Phase K — Unified API / Multi-Target Routing
- [x] K1. Add explicit `target_kind`/`target_id` metadata in status/config responses.
- [ ] K2. Introduce formal Target Router abstraction (`sim` and `real` adapters behind one API surface).
- [ ] K3. Enforce sim-only endpoint gating (reject or no-op on `target_kind=real`).
- [ ] K4. Add cross-target contract tests to confirm schema parity for shared endpoints.

## Phase L — UI Strategy and Deployment Split
- [x] L1. Decide deployment split: STM32-hosted hardware UI remains real-only; independent sim helper UI for simulation.
- [ ] L2. Define shared control surface vs sim-only control surface.
- [ ] L3. Update helper UI to clearly label sim-only controls and target context.
- [ ] L4. Document operator workflow for choosing real vs sim UI paths.

## Phase M — Composed MuJoCo Scene Integration
- [ ] M1. Define attach-to-existing-scene mode (operate on PRISM instance inside larger MuJoCo model).
- [ ] M2. Add namespace contract for PRISM entities in multi-articulation scenes.
- [ ] M3. Validate PRISM + robot-arm co-simulation baseline without changing PRISM controller semantics.
- [ ] M4. Add regression scenario for PRISM interaction in a composed scene.

## Immediate Work Started (Now)
1. Implement Phase B (preset extraction + loader parity). ✅
2. Implement Phase E baseline telemetry parity. ✅
3. Scaffold deterministic runner hook (Phase C5). ✅

## Next Actions (Priority Order)
1. Land persistence hardening and runtime diagnostics acceptance for reconnect safety.
2. Implement Phase K2-K4 (formal target router + target-gating + contract tests).
3. Execute Phase L2-L4 (clear split between shared vs sim-only UI controls).
4. Begin Phase M1-M2 to guarantee compatibility with larger composed MuJoCo scenes.
5. Defer containerization for viewer path; containerize headless runtime only after K/L contracts stabilize.
