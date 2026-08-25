# ADR-0029 — Contours vectoriels natifs

- Status: accepted
- Date: 2026-08-25

## Decision

`VectorNode` peut porter un `stroke` optionnel indépendant de son fill. Le
contour contient une couleur RGBA, une largeur strictement positive, une
jointure `miter`, `round` ou `bevel`, et une extrémité `butt`, `round` ou
`square`. Les nœuds sont dessinés dans l’ordre de leur tableau `native.nodes`,
qui constitue l’ordre de dessin déterministe de la v2.

Le document conserve les paramètres du contour ; le preview actuel applique
la couleur et la largeur et utilise la primitive de ligne ImGui pour les
jointures et extrémités jusqu’au renderer triangulé dédié.
