# ADR-0030 — Hiérarchie et clipping des nœuds vectoriels

- Status: accepted
- Date: 2026-08-25

## Decision

Un `VectorNode` peut référencer un parent par son identifiant stable et un
nœud de clipping par son identifiant stable. Les références sont locales au
`VectorAsset`; elles ne suivent jamais un chemin de fichier externe.

Le validateur exige des cibles existantes, interdit l’auto-parentage et refuse
les cycles de parentage avant publication. Le clipping vers soi-même est
également refusé. L’ordre du tableau `native.nodes` reste l’ordre de dessin
déterministe dans chaque niveau ; la résolution graphique complète des
transforms et des clips sera branchée par le renderer dédié.
