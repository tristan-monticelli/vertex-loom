# Project documentation

This repository is a GitHub starter. Discovery documents remain guides while `.project/project-config.json` has `"status": "template"`.

## Workflow

1. Read the complete request and every starter document.
2. Complete `00-project-brief.md` and `01-technology-decisions.md`.
3. Produce C4 Context and Container diagrams.
4. Define the test strategy.
5. Add required ADRs.
6. Replace guides with real project documentation and change the configuration to `initialized`.

Initialization fails if a decision, source directory, code extension, mutation
choice, or required C4 diagram is missing.

## Structure

- `00-project-brief.md`: goals, scope, and constraints.
- `01-technology-decisions.md`: technical choices and criteria.
- `02-quality-strategy.md`: test and quality strategy.
- `architecture/`: versioned C4, component, and flow diagrams.
- `decisions/`: durable decisions as ADRs.
- `systems/`: subsystem contracts and responsibilities.
- `workflows/`: important procedures and interactions.
- `templates/`: documentation templates to copy and adapt.

Mermaid is the versioned source; SVG renders are generated artifacts.
