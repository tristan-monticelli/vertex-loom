# ADR-0001 — Node.js for template governance

- Status: accepted
- Date : 2026-08-24

## Context

The template must validate its hooks, configuration, and diagrams without
imposing a technology stack on derived products.

## Decision

Use Node.js and npm only for template infrastructure: CTXRoute, governance hooks,
governance tests, and Mermaid. Keep product stack decisions separate in
`.project/project-config.json`.

Expose `npm run setup` as the single cross-platform bootstrap command. It uses
the lockfile, installs Mermaid's browser explicitly, enables repository-local
Git hooks, and runs the full validation suite.

## Alternatives

Shell-only tooling would reduce dependencies but weaken portability and
testability. Reimplementing Mermaid validation would add unjustified complexity.

## Consequences

Node.js 22+ and npm 10+ are explicit template prerequisites because CTXRoute
requires Node.js 22+. They do not determine the language, runtime, or
architecture of a derived product.

Setup changes only repository-local state and dependency caches. It never edits
global agent settings, deletes project files, or creates commits.
