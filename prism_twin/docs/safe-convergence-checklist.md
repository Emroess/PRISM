# Safe Convergence Checklist (Sim + Real API Strategy)

Goal: converge toward one API surface and one control-core contract with multiple execution targets (`sim`, `real`) while keeping a light touch on real-hardware code.

## A) Merge Confidence Gate (Federated Base)
- [x] A1. Confirm base federation branch exists and is up to date with origin.
- [x] A2. Confirm change scope is isolated (prefer `prism_twin/` + minimal repo plumbing only).
- [x] A3. Confirm no functional changes to current real-hardware runtime path.
- [x] A4. Run twin sanity checks and record pass/fail.

## B) Real-Hardware Light-Touch Guardrails
- [x] B1. Any convergence code lands under `prism_twin/` first.
- [ ] B2. No firmware/control-loop behavior changes without explicit parity test plan.
- [ ] B3. Real-target integration introduced behind explicit target adapter boundary.
- [ ] B4. Keep all new behavior opt-in until validated.

## C) Convergence Architecture Checklist
- [ ] C1. Define `TargetAdapter` contract (`sim`, `real`).
- [ ] C2. Define `TargetRouter` semantics (`target_kind`, `target_id`, session binding).
- [ ] C3. Keep telemetry contract shape identical across targets.
- [ ] C4. Add reconnect-safe, idempotent runtime/session semantics.

## D) Persistence Implementation Readiness
- [x] D1. Branch selected: `feature/prism-twin-persistent-server`.
- [x] D2. Branch synchronized with federated base.
- [ ] D3. Runtime diagnostics endpoints planned (`/api/v1/health`, `/api/v1/runtime`).
- [ ] D4. Persistence acceptance criteria documented.

## E) Execution Log
- Date: 2026-03-05
- Branch: `feature/prism-twin-persistent-server`
- Commands run:
	- `git diff --name-only origin/main...origin/feature/prism-twin-federated-setup`
	- `uv run --python 3.11 --project PRISM_source/prism_twin python scripts/check_telemetry_schema.py`
	- `uv run --python 3.11 --project PRISM_source/prism_twin python scripts/validate_handle_catalog.py`
	- `uv run --python 3.11 --project PRISM_source/prism_twin python scripts/check_instance_isolation.py`
- Results:
	- Scope gate passed: only `prism_twin/*` plus `.gitignore` differ from `origin/main`.
	- Telemetry schema check: PASSED.
	- Handle catalog validation: PASSED.
	- Instance isolation check: PASSED.
	- Note: initial runs failed under CPython 3.14 (`mujoco` build); rerun with Python 3.11 succeeded.
- Decision:
	- Proceed with persistence implementation on `feature/prism-twin-persistent-server` using light-touch guardrails (real-hardware path unchanged).
