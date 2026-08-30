# ADR-0017 — Dépendances des fondations d’édition

- Status: accepted
- Date: 2026-08-24

## Context

Les nouvelles suites C++ ont besoin de diagnostics de test structurés sans
réécrire les exécutables de test existants. Asset Studio doit aussi créer,
ouvrir et importer avec les sélecteurs de fichiers natifs des trois plateformes
au lieu d’exiger une saisie manuelle de chemins.

## Decision

Épingler Catch2 `v3.15.3` par CMake FetchContent et l’utiliser pour toute
nouvelle suite via `Catch2::Catch2WithMain`. Les tests historiques restent des
exécutables autonomes jusqu’à ce qu’une modification fonctionnelle justifie
leur migration.

Épingler Native File Dialog Extended `v1.3.0` par FetchContent et lier sa cible
`nfd` uniquement aux applications d’édition. Asset Studio initialise NFD après
SDL, libère chaque chemin avec `NFD_FreePath` et ferme NFD avant de quitter.

## Alternatives

Réécrire immédiatement tous les tests augmenterait le diff sans améliorer le
gate courant. Des dialogues ImGui conserveraient une expérience non native et
demanderaient leur propre navigateur de fichiers. Les API spécifiques Cocoa,
Win32 et GTK dupliqueraient trois implémentations.

## Consequences

Les nouvelles suites disposent de sections et d’assertions homogènes tandis
que l’historique reste stable. NFD ajoute des prérequis GTK de développement au
runner Linux, sans service distant ni accès réseau à l’exécution.
