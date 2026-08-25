# ADR-0063 — Contrôleur de personnage piloté par SDL

## Décision

Le Preview Runtime expose `--character` pour activer un personnage Box2D et
son `CharacterController`. Les touches `A`/flèche gauche, `D`/flèche droite
et espace sont traduites en actions logiques `move_left`, `move_right` et
`jump`. Un ReplayPlayer peut alimenter exactement le même `InputActionMap`.
Les boutons D-pad gauche/droite et A d’un contrôleur SDL sont également
reliés à ces actions.

Le programme peut fournir une table de bindings à `PreviewRuntimeOptions`.
Elle doit déclarer les trois actions de locomotion ; sinon le chargement est
refusé avant la création de la fenêtre. En l’absence de table, les bindings
SDL historiques restent utilisés.

La table peut aussi être construite sans code avec des options répétables
`--bind <action> <keyboard|gamepad> <code>` de `game_runtime`. Les doublons,
codes négatifs et identifiants d’action invalides sont refusés au parsing.

Les commandes sont appliquées avant chaque pas physique fixe de `1/60 s`.

## Conséquences

Le smoke-test peut valider une locomotion reproductible via un replay, tandis
que le mode interactif consomme les événements SDL sans coupler la physique au
renderer.
