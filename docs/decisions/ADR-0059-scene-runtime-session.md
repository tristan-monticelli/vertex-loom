# ADR-0059 — Session runtime de scènes

## Décision

`SceneRuntimeSession` charge une scène et son `entryMap` après validation du
projet, puis résout les transitions par `ResourceId`. Une transition est
préparée dans des documents temporaires ; la scène courante n’est remplacée
que si la cible et sa map sont valides.

`game_runtime --scene <id>` réutilise directement cette session pour résoudre
la map d’entrée avant d’initialiser `PreviewRuntime`.

## Conséquences

La résolution des scènes est testable sans SDL ni fenêtre et peut être
réutilisée par le Preview Runtime et le runtime jouable. Les points d’entrée
de transition sont conservés dans le document et seront utilisés par le
placement gameplay lorsque les spawn points seront disponibles.
