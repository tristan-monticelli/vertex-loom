# ADR-0051 — Bootstrap du Preview Runtime

## Statut

Accepté.

## Décision

`game_runtime` utilise `fabric_runtime::PreviewRuntime`. Le chargement complet
du projet et de la map, ainsi que la construction du monde Box2D, précèdent
`SDL_Init` et toute création de fenêtre. Une erreur de validation empêche donc
le runtime d’atteindre l’interface graphique.

Le mode `--smoke-test` exécute un nombre fini de frames sans interaction et le
mode `--benchmark` exécute par défaut 600 frames. La première tranche rend les
instances de map sous forme de draw packets de diagnostic ; la résolution des
drawables, matériaux et animations réels sera ajoutée dans les tranches de
chargement suivantes.
