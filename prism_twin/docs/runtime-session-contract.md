# Runtime Session Contract (Persistence Phase 2)

This document defines runtime/session behavior for the PRISM twin REST server.

## Scope
- Applies to `prism_twin/simulation/twin_rest_server.py`.
- Covers reconnect-safe session binding and runtime diagnostics.
- Keeps behavior isolated to `prism_twin` (no real-hardware runtime changes).

## Runtime Endpoints

### `GET /api/v1/health`
Returns liveness/readiness summary:
- `status`: `ok` when handler is alive.
- `uptime_s`: process uptime.
- `running`: whether control loop is active.
- `instance_count`: number of managed instances.
- `session_count`: number of currently active sessions (after prune).

### `GET /api/v1/runtime`
Returns expanded runtime diagnostics:
- `status`, `uptime_s`, `requests`.
- `session_policy`:
  - `ttl_s`: inactivity expiration threshold.
  - `max_count`: max active sessions before eviction.
- `manager`: control-loop and instance summary.
- `instances`: current managed instances.
- `sessions`: active sessions with `created_at_s` and `last_seen_s`.

### `GET /api/v1/targets/capabilities`
Returns target capability metadata for client/UI negotiation:
- `targets.sim.configured` and supported shared/sim-only endpoint groups.
- `targets.real.configured` status for real-target adapter readiness.

Helper UI behavior contract:
- `simulation/sim_helper_ui.html` reads capabilities at startup before enabling controls.
- Sim-only controls (`instances`, `control`, `handle_select`, `preset_select`, `interaction_set_position`) are hidden unless `target_kind=sim` and `targets.sim.configured=true`.
- When opened with `?target_kind=real&target_id=<id>`, the helper UI enters telemetry-only mode.
- If `targets.real.configured=false`, the helper UI skips real-target status polling and renders an explicit unavailable state instead of issuing invalid requests.
- In `--with-viewer` mode, the native MuJoCo viewer renders the configured viewer instance only; additional created instances are active in API/runtime state but are not simultaneously shown in the same viewer window.

## Session Binding Endpoint

### `POST /api/v1/session/bind`
Body fields:
- `session_id` (optional): if absent, server generates UUID.
- `instance_id` (optional): defaults to resolution behavior; explicit is recommended.
- `create_if_missing` (optional, default `true`): create instance idempotently.
- `client_id` (optional): caller identity for diagnostics.

Response fields:
- `created_instance`: whether instance was created by this call.
- `created_session`: whether session record was newly created.
- `session`: `{ session_id, instance_id, target_kind, target_id }`.
- `session_policy`: current TTL/max settings.

### `POST /api/v1/viewer/select`
Optional viewer-instance routing endpoint (only when server started with `--with-viewer`):
- Body fields: `instance_id` (or equivalent sim target selectors).
- Behavior: requests native MuJoCo viewer to relaunch against the specified sim instance.
- Success: `200` with `viewer_instance`.
- If viewer mode is disabled: `409` with `viewer_not_enabled`.

## Routing Semantics
- If `instance_id` is not provided on requests that need targeting, server resolves by:
  1) explicit `instance_id` query/body,
  2) bound `session_id` (query/body),
  3) fallback default `prism_01`.
- This supports reconnect-safe clients that only retain a `session_id`.
- If `target_kind=real` is requested before real adapter wiring is configured,
  shared target endpoints return HTTP `501` with explicit `not configured` error.

## Session Lifecycle Policies
- `--session-ttl-s` (default `900`): session expires after inactivity.
- `--session-max-count` (default `64`): server evicts least-recently-seen sessions when over limit.
- Pruning is applied on request handling.

## Persistence Acceptance Criteria (Phase 2)
1. Repeated `POST /api/v1/instances` for same ID is idempotent (`created=false` on subsequent calls).
2. Session bind can create/reuse instance and session deterministically.
3. `status` resolution via `session_id` returns telemetry for bound target instance.
4. Session expiration/eviction policy is observable via `/health` and `/runtime`.
