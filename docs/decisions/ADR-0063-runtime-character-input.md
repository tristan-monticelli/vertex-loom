# ADR-0063 — Contrôleur de personnage piloté par SDL

## Décision

Le Preview Runtime expose `--character` pour activer un personnage Box2D et
son `CharacterController`. Les touches `A`/flèche gauche, `D`/flèche droite
et espace sont traduites en actions logiques `move_left`, `move_right` et
`jump`. Un ReplayPlayer peut alimenter exactement le même `InputActionMap`.

Les commandes sont appliquées avant chaque pas physique fixe de `1/60 s`.

## Conséquences

Le smoke-test peut valider une locomotion reproductible via un replay, tandis
que le mode interactif consomme les événements SDL sans coupler la physique au
renderer.
