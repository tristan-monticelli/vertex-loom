# ADR-0028 — Chemins vectoriels natifs

- Status: accepted
- Date: 2026-08-25

## Context

Les primitives rectangulaire, elliptique et linéaire ne suffisent pas pour
authorer une silhouette simple. Le contrat natif doit pouvoir conserver un
chemin éditable sans dépendre du DOM SVG.

## Decision

Ajouter `path` à `VectorShapeKind`. Un chemin est une liste ordonnée de
commandes `move`, `line`, `cubic` et `close` :

- `move` et `line` portent un point `point` ;
- `cubic` porte `point`, `control1` et `control2` ;
- `close` ne porte aucun point.

Un chemin doit commencer par `move`, contenir au moins un segment, et ne peut
contenir qu’un `close` final. Les coordonnées doivent être finies. La première
version ne détecte pas encore les auto-intersections et ne calcule pas encore
la tessellation persistante.

Le preview aplatit les courbes cubiques par échantillonnage déterministe pour
afficher la silhouette. Cette géométrie reste dérivée et n’est jamais écrite
dans le document.
