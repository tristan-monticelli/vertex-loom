# ADR-0011 — Nom public Vertex Loom

- Status: accepted
- Date: 2026-08-24

## Context

Le dépôt dérivé doit être publié comme moteur 2D autonome et ne plus se
présenter comme le blueprint CTXRoute dont il provient.

## Decision

Utiliser « Vertex Loom » comme nom public et `vertex-loom` comme nom de dépôt.
« Vertex » exprime le moteur 2D et vectoriel ; « Loom » rappelle les outils et
matériaux textiles. Les namespaces et cibles `fabric::*` restent inchangés pour
préserver les contrats techniques déjà testés.

## Alternatives

`vertex-2d-engine` est descriptif mais générique. `Fabric Engine` correspond au
nom de travail initial mais distingue moins clairement le projet public.

## Consequences

Le README, les métadonnées npm, CMake et les titres d'architecture utilisent le
nom public. Les exécutables et bibliothèques gardent leurs noms techniques
actuels jusqu'à ce qu'une migration apporte une valeur fonctionnelle.
