# Vertex Loom release archive

Vertex Loom 0.1.0 is distributed as a portable desktop archive.

## Launch

Extract the archive without changing its `bin` and `share` layout, then run:

```sh
bin/asset_studio
bin/map_studio
bin/game_runtime --help
```

On Linux, OpenGL 3 and a desktop display are required for the studios and the
interactive runtime. The validation CLI remains headless:

```sh
bin/fabric_project_validate path/to/project
```

## Verification

Compare the archive hash with `SHA256SUMS`. The CycloneDX file
`vertex-loom.sbom.cdx.json` lists the bundled build dependencies. Licenses and
notices are installed under `share/vertex-loom/licenses`.

Public archives are produced only by tags matching `v0.1.0`, `v0.1.0-rc.N` or
`v0.1.0-beta.N`. Release candidates and betas are marked as prereleases. The
workflow requires the official public contact, approved default-image
provenance, macOS notarization, Windows signing, and native GPU gates.
