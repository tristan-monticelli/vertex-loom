# CTXRoute Blueprint

[![Validate](https://github.com/zenonlab/ctxroute-blueprint/actions/workflows/validate.yml/badge.svg)](https://github.com/zenonlab/ctxroute-blueprint/actions/workflows/validate.yml)
[![Node.js 22+](https://img.shields.io/badge/Node.js-22%2B-339933?logo=node.js&logoColor=white)](https://nodejs.org/)
[![npm 10+](https://img.shields.io/badge/npm-10%2B-CB3837?logo=npm&logoColor=white)](https://www.npmjs.com/)
[![Mermaid](https://img.shields.io/badge/diagrams-Mermaid-ff3670?logo=mermaid&logoColor=white)](https://mermaid.js.org/)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue)](LICENSE)

An architecture-first [GitHub template](https://docs.github.com/en/repositories/creating-and-managing-repositories/creating-a-template-repository)
for software projects. It does not impose a language, backend, frontend,
database, deployment platform, or test framework.

The generated product remains stack-neutral. The template tooling requires
[Node.js 22+](https://nodejs.org/) and [npm 10+](https://www.npmjs.com/) to run
[CTXRoute](https://github.com/zenonlab/ctxroute), governance hooks, tests, and
[Mermaid](https://mermaid.js.org/).

## Create a project

1. Select **Use this template** on GitHub.
2. Clone the generated repository and enter its root directory.
3. Install Git, Node.js 22+, and npm 10+.
4. Run:

   ```sh
   npm run setup
   ```

5. Ask your coding agent to read [`AGENTS.md`](AGENTS.md) and [`CLAUDE.md`](CLAUDE.md),
   then initialize the project from your requirements.
6. Review the [project brief](docs/00-project-brief.md), [technology decisions](docs/01-technology-decisions.md),
   [architecture decision records](docs/decisions/README.md), [C4 diagrams](docs/architecture/README.md),
   and [quality strategy](docs/02-quality-strategy.md).
7. Approve the cleanup and first project commit only when the starter is fully initialized.

`npm run setup` installs the pinned dependencies and Mermaid browser, enables
the repository Git hooks, validates CTXRoute, and runs the complete test suite.
It refreshes the ignored `node_modules/` directory but does not change global
Codex settings, delete tracked project files, or create commits.

`.codex/`, `.claude/`, `.githooks/`, `.project/`, `rules/`, [`AGENTS.md`](AGENTS.md),
[`CLAUDE.md`](CLAUDE.md), and the documentation structure are reusable
infrastructure. Product source directories and commands are created only after
project discovery.

[`AGENTS.md`](AGENTS.md) is the authoritative repository doctrine. [`CLAUDE.md`](CLAUDE.md)
is the Claude-compatible entry point that points back to that doctrine; the two
files are intentionally consistent and should be read together by agents.

`.project/project-config.json` is the source of
truth for source directories, code extensions, contracts, commands, and
mutation-testing policy. Invalid or incomplete configuration blocks product
writes.

## CTXRoute

[CTXRoute](https://github.com/zenonlab/ctxroute) injects only relevant project
context into agent actions. This repository pins a reviewed upstream commit and
uses project-local wrappers so hook paths work on Windows, macOS, and Linux.
Rule documents live in CTXRoute's canonical
[`.claude/hooks/docs/`](.claude/hooks/docs/) directory and remain available to
both Codex and Claude-compatible tooling.

CTXRoute never modifies global Codex settings during installation. The tracked
`.codex/hooks.json` configuration is local to this project.

For prerequisite diagnostics without installing anything, run
`npm run setup:check`.

## Validate

```sh
npm run validate
```

The agent must not delete starter guides or create the first derived-project
commit without user confirmation. See the [repository contribution rules](CONTRIBUTING.md)
and [security policy](SECURITY.md) for project-level guidance.

## License

CTXRoute Blueprint is licensed under Apache-2.0. CTXRoute remains available under
its own MIT license; see `THIRD_PARTY_NOTICES.md`.
