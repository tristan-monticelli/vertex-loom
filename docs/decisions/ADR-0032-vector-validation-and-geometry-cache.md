# ADR-0032 — Validation des contours et cache de géométrie vectorielle

- Status: accepted
- Date: 2026-08-25

## Context

Un contour auto-intersectant n’a pas de remplissage unique pour le renderer
actuel : une triangulation naïve peut produire des pixels différents selon
l’ordre des sommets. Les Bézier doivent aussi être recalculées lorsque le
document ou la tolérance de vue change, sans conserver une géométrie dérivée
obsolète.

## Decision

`fabric_project` refuse les auto-intersections des chemins natifs avant
publication ou rendu headless. La validation inspecte les segments linéaires
et une approximation déterministe des cubiques, y compris le segment de
fermeture.

`fabric_render::VectorGeometryCache` indexe le résultat par la sérialisation
JSON canonique du `VectorAsset` et par la tolérance de courbe. Une modification
du document ou de la tolérance constitue donc une nouvelle version de cache.
Le cache ne persiste rien sur disque et peut être vidé explicitement.

## Consequences

Les formes à trous, les opérations booléennes et les contours auto-croisés
restent hors du contrat v2. La vue doit fournir une tolérance adaptée au zoom.
La mémoire du cache appartient à la session de rendu ; aucune migration de
document n’est nécessaire.
