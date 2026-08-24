# ADR-0004 — Runtime natif C++20

- Status: accepted
- Date: 2026-08-24

## Context

Le projet doit fournir un moteur 2D réutilisable avec boucle temps réel,
physique, rendu et outils desktop portables.

## Decision

Utiliser C++20 avec CMake pour le runtime et le cœur partagé. SDL2 fournit la
fenêtre, les entrées et l'audio ; OpenGL fournit le premier backend de rendu.

## Alternatives

TypeScript accélérerait le prototype mais réduirait le contrôle natif. Un
moteur existant réduirait le code initial mais contredirait l'objectif d'outils
sur mesure.

## Consequences

Meilleur contrôle et performances, au prix d'une chaîne de build native.
