# ADR-0036 — Conversion SVG explicite vers la géométrie native

- Status: accepted
- Date: 2026-08-25
- Supersedes: aucun

## Context

`VectorAsset v2` distingue un SVG lié opaque d’un document natif éditable. Le
renderer natif sait déjà consommer des chemins Bézier, mais aucun service ne
permettait de transformer un SVG compatible sans rasterisation persistante.

## Decision

Ajouter à `fabric_render` `convert_svg_to_native`, basé sur la version NanoSVG
fournie par SDL_image 2.8.12. Le service applique les mêmes limites de source
que l’aperçu SVG, produit un `VectorAsset v2 native` et convertit les chemins
cubiques, fills couleur et contours simples. Les coordonnées sont retournées
dans l’espace monde à origine haute, avec dimensions et styles conservés.

Les gradients, paints non supportés, sources invalides et documents sans
géométrie visible génèrent des diagnostics. La source SVG n’est jamais
réécrite et la conversion ne publie aucun fichier par elle-même ; la couche
d’édition demande confirmation, exécute une commande réversible et utilise la
publication native atomique. Undo restaure l’état `linkedSvg` sans réécrire le
fichier source ; redo réactive la géométrie native.

## Consequences

La conversion reste déterministe et sans atlas ni bitmap dérivé. NanoSVG est
une dépendance de build déjà épinglée par SDL_image ; son en-tête est utilisé
pour le parseur de conversion, tandis que le rasteriseur existant reste le
chemin de prévisualisation des SVG liés.
