# Follow-up PR Plan (Post Federation)

This file defines the recommended split after the base federation PR (`feature/prism-twin-federated-setup`) is merged.

## Branch 1: Stability
- Branch: `feature/prism-twin-stability`
- Goal: harden UI↔viewer synchronization behavior.
- Scope:
  - Remove fragile timing dependencies between telemetry polling and UI control writes.
  - Add explicit state ownership rules for slider/preset UI interactions.
  - Add a minimal automated check for preset persistence + manual position control.
- Acceptance:
  - Preset dropdown selection does not self-reset under normal polling.
  - Manual position slider visibly drives handle motion in unified mode.

## Branch 2: Multi-instance UX
- Branch: `feature/prism-twin-multi-instance-ux`
- Goal: make second-instance creation/visibility obvious and testable.
- Scope:
  - Ensure `Create prism_02` yields clear UI and runtime feedback.
  - Add viewer-side instance selection or rendering behavior for multi-instance confirmation.
  - Improve helper UI instance list/status clarity.
- Acceptance:
  - Creating `prism_02` provides unambiguous confirmation in UI and viewer/runtime status.
  - Instance-scoped controls apply to selected instance reliably.

## Branch 3: Persistent Server
- Branch: `feature/prism-twin-persistent-server`
- Goal: provide long-lived server behavior resilient to reconnecting clients.
- Scope:
  - Clarify and enforce persistent runtime lifecycle semantics.
  - Make instance creation idempotent and reconnect-safe.
  - Add lightweight health/readiness endpoint and recovery guidance.
- Acceptance:
  - New clients can join/reload without breaking runtime or instance map.
  - Server remains stable across repeated UI reconnect cycles.

## Suggested Merge Order
1. `feature/prism-twin-federated-setup` (base)
2. `feature/prism-twin-stability`
3. `feature/prism-twin-multi-instance-ux`
4. `feature/prism-twin-persistent-server`

## Notes
- Keep all digital twin code under `prism_twin/` (federated structure).
- Keep PRs focused and scoped to one concern each for easier review.
