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
