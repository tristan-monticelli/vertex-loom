# Contributing

Read `AGENTS.md` and the relevant documentation before making changes.

- Structural changes update the relevant C4 document.
- Durable decisions become ADRs.
- Mermaid sources are versioned; SVG files are generated artifacts.
- Commands declared in `.project/project-config.json` must exist.
- Source directories, code extensions, and contracts come only from that configuration.
- Node.js 22+ and npm 10+ are required for template tooling, independently of the product stack.
- Never delete or commit automatically.

Before contributing, run the idempotent project setup:

```sh
npm run setup
```

Pre-commit validates the Git index, architecture, links, Mermaid, placeholders,
and targeted mutation testing when enabled. Pre-push runs the complete commands
declared by the project.
