# ADR-0063 — Contrôleur de personnage piloté par SDL

## Décision

Le Preview Runtime expose `--character` pour activer un personnage Box2D et
son `CharacterController`. Les trois actions sémantiques de locomotion sont
choisies explicitement avec `--character-actions <left> <right> <jump>` ou
`PreviewRuntimeOptions.character_actions`. Un ReplayPlayer peut alimenter
exactement le même `InputActionMap` sans convention de nom.

Lorsque `character_actions` est fourni, la table de bindings ou le document
Input doit déclarer les trois identifiants ; sinon le chargement est refusé
avant la création de la fenêtre. Sans cette option, le corps existe mais reste
stationnaire : aucune action implicite n'est injectée.

La table peut aussi être construite sans code avec des options répétables
`--bind <action> <keyboard|gamepad> <code>` de `game_runtime`. Les doublons,
codes négatifs et identifiants d’action invalides sont refusés au parsing.

Les commandes sont appliquées avant chaque pas physique fixe de `1/60 s`.

## Conséquences

Le smoke-test peut valider une locomotion reproductible via un replay, tandis
que le mode interactif consomme les événements SDL sans coupler la physique au
renderer.
