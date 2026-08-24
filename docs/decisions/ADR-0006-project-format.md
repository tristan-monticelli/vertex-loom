# ADR-0006 — Format de projet lisible et versionné

- Status: accepted
- Date: 2026-08-24

## Context

Les éditeurs et le runtime doivent échanger les mêmes scènes et ressources,
avec des fichiers inspectables dans Git.

## Decision

Utiliser JSON pour métadonnées, scènes et contrats, avec assets lourds séparés.
Chaque projet porte une version de schéma et les imports sont validés.

## Alternatives

Un format binaire serait plus compact mais difficile à inspecter ; un format
texte sans version rendrait les migrations fragiles.

## Consequences

Les diffs et migrations sont possibles, sans dupliquer les gros assets dans les
fichiers JSON.
