# Issue Draft: Align multi-instance semantics across firmware, sim, UI, and CLI

## Title
Synchronize multi-instance PRISM architecture across hardware API mechanics and UI/CLI contracts

## Summary
We need one explicit architecture for how multiple PRISM instances are addressed and managed across:
- Real hardware runtime/API behavior (mechanics + endpoint semantics)
- Simulation runtime/API behavior
- UI behavior (hardware HTML and sim helper)
- CLI behavior

Current sim runtime supports instance-scoped operations and viewer target switching, while real-target routing is present but adapter wiring remains staged. We should prevent divergence now by defining one canonical contract.

## Problem
Without a shared spec and implementation plan, we risk:
- API drift between `sim` and `real` targets
- inconsistent UX between hardware UI and sim UI for multi-instance targeting
- mismatched CLI affordances vs API routing (`target_kind`, `target_id`, `instance_id`)
- difficult migration when moving to composed single-world multi-PRISM + robot scenes

## Proposed outcomes
1. Define canonical multi-instance target model:
   - `target_kind` (`sim|real`)
   - `target_id` (hardware id or instance id)
   - `instance_id` resolution rules
2. Define parity matrix for endpoints:
   - shared endpoints and guaranteed semantics
   - sim-only and real-only endpoint classes
   - explicit capability negotiation behavior
3. Define UI/CLI interaction contract:
   - target selection UX for hardware UI, sim UI, and CLI
   - failure semantics for unavailable targets/capabilities
4. Define migration path for composed scenes:
   - from per-instance model/data worlds to shared composed-world index mapping

## Scope
- In scope: API/contract design, UI/CLI contract, integration plan and acceptance criteria
- Out of scope: full real adapter implementation details for all hardware capabilities

## Acceptance criteria
- Written contract doc for multi-instance routing used by firmware + twin + CLI clients
- Endpoint parity matrix approved and published
- UI contract approved (shared vs sim-only vs real-only behavior)
- CLI target selection semantics approved and tested against API
- Implementation milestones and ownership assigned

## Suggested labels
- architecture
- api
- simulation
- firmware
- ui
- cli
- multi-instance
