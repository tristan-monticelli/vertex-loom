# ADR-0003 — Validate starter completeness

- Status: accepted
- Date: 2026-08-24

## Context

The project configuration declares the infrastructure roots and root files that
make up a complete template. Path syntax validation alone allowed a declared
file to be absent while the complete validation suite still passed, weakening
cross-agent onboarding guarantees.

## Decision

Treat `starter.infrastructureRoots` and `starter.rootFiles` as a completeness
manifest while project status is `template`. Configuration validation requires
every declared root to be a directory and every declared root file to be a file.

Full architecture and documentation audits inspect tracked files and
non-ignored working-tree files, while respecting the repository's `.gitignore`
rules for generated and operating-system artifacts.

Keep `AGENTS.md` as the authoritative doctrine. Provide `CLAUDE.md` as a concise
Claude-compatible entry point that directs agents to that doctrine without
duplicating it.

Do not enforce starter-path existence after initialization, because an approved
derived-project cleanup may revise or remove template-only material.

## Alternatives

Removing absent entries from the manifest would hide incomplete packaging.
Duplicating all agent instructions in `CLAUDE.md` would create a second doctrine
that could drift from `AGENTS.md`.

## Consequences

Incomplete templates now fail setup, local validation, and CI before product
work begins. Ignored generated and operating-system artifacts do not create
false product or documentation failures. Tests and fixture repositories must
materialize the declared starter structure while exercising template status.
