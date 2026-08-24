# ADR-0008 — Validation unifiée Node et C++

- Status: accepted
- Date: 2026-08-24

## Context

Le dépôt conserve les contrôles Node du blueprint et ajoute désormais des
contrats C++ compilés. Une validation verte doit couvrir les deux ensembles sur
les trois systèmes de la matrice CI.

## Decision

Conserver `npm run validate` comme point d'entrée multiplateforme. Cette
commande exécute la gouvernance, configure et construit CMake, lance CTest puis
les tests Node. Les commandes C++ restent aussi disponibles séparément.

## Alternatives

Deux pipelines indépendants faciliteraient les exécutions ciblées mais
permettraient à une validation dite complète d'ignorer une moitié du dépôt.

## Consequences

La validation complète est plus longue et peut télécharger les dépendances
CMake au premier passage. `npm test` et `npm run test:cpp` restent disponibles
pour les boucles locales ciblées.
