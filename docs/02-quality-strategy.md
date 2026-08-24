# Quality strategy

<!-- Guide: select only levels useful to the project and record tools that are actually installed. Mutation testing is recommended for critical or algorithmic logic, parsers, validators, and business rules. It is usually unnecessary for simple UI or glue code. Run targeted mutation tests before important commits and the complete mutation suite before major releases. -->

## Test matrix

| Type | Required? | Tool | Command | Rationale |
| --- | --- | --- | --- | --- |
| Unit | to be decided | to be decided | to be decided | to be decided |
| Integration | to be decided | to be decided | to be decided | to be decided |
| End-to-end | to be decided | to be decided | to be decided | to be decided |
| Contract | to be decided | to be decided | to be decided | to be decided |
| Property | to be decided | to be decided | to be decided | to be decided |
| Snapshot | to be decided | to be decided | to be decided | to be decided |
| Performance | to be decided | to be decided | to be decided | to be decided |
| Security | to be decided | to be decided | to be decided | to be decided |
| Mutation | to be decided | to be decided | to be decided | to be decided |

## Decision rule

Describe the covered risk, target scope, and execution timing for every selected test level.

Record the mutation decision in `.project/project-config.json`. Hooks run the
command only when `preCommit` or `prePush` is `true`.
