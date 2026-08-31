# ADR-0029 — Contours vectoriels natifs

- Status: accepted
- Date: 2026-08-25

## Decision

`VectorNode` peut porter un `stroke` optionnel indépendant de son fill. Le
contour contient une couleur RGBA, une largeur strictement positive, une
jointure `miter`, `round` ou `bevel`, et une extrémité `butt`, `round` ou
`square`. Les nœuds sont dessinés dans l’ordre de leur tableau `native.nodes`,
qui constitue l’ordre de dessin déterministe de la v2.

Le document conserve les paramètres du contour ; `fabric_render` produit une
géométrie triangulée dédiée pour les segments, jointures et extrémités. Les
préviews Asset Studio et Preview Runtime consomment ensuite ces triangles via
le renderer OpenGL partagé, avec blending alpha explicite.
