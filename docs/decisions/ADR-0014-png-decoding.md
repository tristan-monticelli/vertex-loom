# ADR-0014 — Décodage PNG pour les outils

- Status: accepted
- Date: 2026-08-24

## Context

Asset Studio doit prévisualiser les PNG sur les trois plateformes sans exposer
un format de surface tiers dans les contrats partagés. Les entrées peuvent être
corrompues ou annoncer des dimensions déraisonnables.

## Decision

Utiliser SDL2_image `2.8.12`, épinglé au tag `release-2.8.12`, avec uniquement
le support PNG activé. `fabric_render` convertit la surface décodée en un
`RasterImage` RGBA8 possédé par le moteur. Une prélecture IHDR limite chaque axe
à 16384 pixels et l'image à 67108864 pixels avant le décodage.

L'interface crée une texture OpenGL temporaire depuis ces pixels. Cette tranche
ne copie pas encore la source dans le projet et ne sérialise pas de
`TextureAsset` ; ces écritures seront ajoutées avec leur propre contrat.

## Alternatives

Un décodeur PNG maison augmenterait fortement le risque sur les fichiers
invalides. Exposer `SDL_Surface` couplerait le renderer, les tests et les futurs
consommateurs à SDL_image.

## Consequences

Les builds qui incluent `fabric_render` compilent SDL2_image et son décodeur
PNG épinglé. Les tests du chargeur restent headless et l'aperçu OpenGL demeure
la responsabilité de l'application desktop.
