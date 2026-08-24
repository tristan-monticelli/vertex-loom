# CTXRoute Blueprint

An architecture-first GitHub template for software projects. It does not impose
a language, backend, frontend, database, deployment platform, or test framework.

The generated product remains stack-neutral. The template tooling requires
Node.js 22+ and npm 10+ to run CTXRoute, governance hooks, tests, and Mermaid.

## Create a project

1. Select **Use this template** on GitHub.
2. Clone the generated repository and enter its root directory.
3. Install Git, Node.js 22+, and npm 10+.
4. Run:

   ```sh
   npm run setup
   ```

5. Ask your coding agent to read `AGENTS.md` and initialize the project from your requirements.
6. Review the brief, decisions, C4 diagrams, and quality strategy.
7. Approve the cleanup and first project commit only when the starter is fully initialized.

`npm run setup` installs the pinned dependencies and Mermaid browser, enables
the repository Git hooks, validates CTXRoute, and runs the complete test suite.
It refreshes the ignored `node_modules/` directory but does not change global
Codex settings, delete tracked project files, or create commits.

`.codex/`, `.claude/`, `.githooks/`, `.project/`, `rules/`, `AGENTS.md`, and the
documentation structure are reusable infrastructure. Product source directories
and commands are created only after project discovery.

`.project/project-config.json` is the source of truth for source directories,
code extensions, contracts, commands, and mutation-testing policy. Invalid or
incomplete configuration blocks product writes.

## CTXRoute

[CTXRoute](https://github.com/zenonlab/ctxroute) injects only relevant project
context into agent actions. This repository pins a reviewed upstream commit and
uses project-local wrappers so hook paths work on Windows, macOS, and Linux.
Rule documents live in CTXRoute's canonical `.claude/hooks/docs/` directory and
remain available to both Codex and Claude-compatible tooling.

CTXRoute never modifies global Codex settings during installation. The tracked
`.codex/hooks.json` configuration is local to this project.

For prerequisite diagnostics without installing anything, run
`npm run setup:check`.

## Validate

```sh
npm run validate
```

The agent must not delete starter guides or create the first derived-project
commit without user confirmation.

## License

CTXRoute Blueprint is licensed under Apache-2.0. CTXRoute remains available under
its own MIT license; see `THIRD_PARTY_NOTICES.md`.
