# ADR-0005 — Deux applications d'authoring spécialisées

- Status: accepted
- Date: 2026-08-24

## Context

La création d'assets et la composition de niveaux ont des flux de travail
différents.

## Decision

Créer Asset Studio pour textures, entités, transformations et animations, et
Map Studio pour niveaux, calques, collisions et objets. Les deux utilisent
Shared Core et le même format de projet.

## Alternatives

Un éditeur monolithique mélangerait les flux et augmenterait la complexité de
l'interface.

## Consequences

Les outils restent spécialisés et testables ; Shared Core devient un contrat
versionné important.
