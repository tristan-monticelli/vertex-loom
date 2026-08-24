# Vertex Loom

[![Validate](https://github.com/tristan-monticelli/vertex-loom/actions/workflows/validate.yml/badge.svg)](https://github.com/tristan-monticelli/vertex-loom/actions/workflows/validate.yml)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus)](https://isocpp.org/)
[![CMake](https://img.shields.io/badge/CMake-3.24%2B-064F8C?logo=cmake)](https://cmake.org/)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue)](LICENSE)

Vertex Loom is a cross-platform C++20 foundation for a 2D engine and native
authoring tools focused on original textile, sprite, and vector worlds.

The project prioritizes Asset Studio and Map Studio quality before a complete
game runtime. The shared project format is complete and Phase 2 now has its
first native Asset Studio workspace.

## Current capabilities

- Versioned JSON project manifests shared by tools and runtime.
- Strict resource identifiers and portable local asset paths.
- Explicit schema migration from the prototype format to version 1.
- Atomic manifest replacement on macOS, Linux, and Windows.
- Human-readable and JSON Lines diagnostics.
- Headless project validation.
- Native SDL2/OpenGL/Dear ImGui Asset Studio shell with project inspection.
- Unified CMake, CTest, Node governance, architecture, and documentation checks.

## Build and test

Requirements: CMake 3.24+, a C++20 compiler, Node.js 22+, and npm 10+.

```sh
npm install
npm run validate
```

For the C++ suite only:

```sh
npm run validate:cpp
```

Open Asset Studio, optionally with a project directory:

```sh
./build/asset_studio
./build/asset_studio tests/fixtures/valid-project
```

## Validate a project

```sh
./build/fabric_project_validate path/to/project
./build/fabric_project_validate --json path/to/project
```

A project contains `project.json` plus `assets`, `entities`, `maps`, `scenes`,
and `schemas` directories. See the
[Shared Core contract](docs/systems/shared-core.md) and
[architecture documentation](docs/architecture/README.md).

## Roadmap

1. Asset Studio static authoring and import pipeline.
2. Hierarchical animation and textile deformation controls.
3. Map Studio composition, collisions, triggers, and events.
4. Preview runtime, followed by the game runtime.

Vertex Loom does not include or reproduce Nintendo characters or assets.

## License

Vertex Loom is licensed under Apache-2.0. Third-party components retain their
own licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
