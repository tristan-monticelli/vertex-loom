# ADR-0007 — Bibliothèque JSON du format projet

- Status: accepted
- Date: 2026-08-24

## Context

Le format projet doit être lu et écrit par le runtime et les deux éditeurs sans
maintenir un parseur JSON propriétaire. La dépendance doit rester portable et
reproductible dans CMake.

## Decision

Utiliser `nlohmann/json` version `v3.11.3`, récupérée par CMake `FetchContent`.
La dépendance reste privée à `fabric_project` afin de ne pas exposer ses types
dans les contrats publics du moteur.

## Alternatives

Un parseur maison augmenterait les risques sur les entrées invalides. RapidJSON
est performant mais impose une API plus bas niveau que nécessaire pour les
petits documents de métadonnées du projet.

## Consequences

La configuration CMake nécessite un accès à la dépendance lors du premier
build. Les API publiques restent composées de types Fabric Engine et peuvent
changer de bibliothèque JSON sans migration des consommateurs.
