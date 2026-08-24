# ADR-0002 — CTXRoute integration

- Status: accepted
- Date: 2026-08-24

## Context

The template needs relevant project rules to reach coding agents at the action
where they matter. Absolute paths in hook configuration are not reusable across
clones or operating systems.

## Decision

Install CTXRoute from the official `zenonlab/ctxroute` HTTPS archive, pinned to
a reviewed commit. Keep CTXRoute configuration and rule documents in the derived
project under the canonical `.claude/hooks/docs/` path. Invoke the six supported
Codex hooks through a small project-local Node wrapper that resolves dependency
and data paths at runtime.

## Alternatives

Global installation would make project behavior depend on each contributor's
machine. Vendoring CTXRoute would duplicate its source and complicate updates.
Absolute hook paths would break when a project is cloned elsewhere.

## Consequences

Node.js 22+ is required by the pinned CTXRoute version. `npm run setup` installs
and validates the engine, Mermaid browser, and repository Git hooks. Tracked
hook configuration works on Windows, macOS, and Linux. Dependency updates
require an explicit commit review and ADR update.
