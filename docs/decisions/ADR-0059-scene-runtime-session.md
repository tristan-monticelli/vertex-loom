# ADR-0059 — Session runtime de scènes

## Décision

`SceneRuntimeSession` charge une scène et son `entryMap` après validation du
projet, puis résout les transitions par `ResourceId`. Une transition est
préparée dans des documents temporaires ; la scène courante n’est remplacée
que si la cible et sa map sont valides.

`PreviewRuntimeOptions` accepte exactement un `map_id` ou un `scene_id`. Avec
`scene_id`, Preview Runtime charge et valide lui-même le `SceneDocument`, le
conserve via `scene()` et résout son `entryMap` avant de charger la map.
`game_runtime --scene <id>` transmet donc directement l’identifiant au runtime
sans conversion préalable.

## Conséquences

La résolution des scènes est testable sans SDL ni fenêtre et peut être
réutilisée par le Preview Runtime et le runtime jouable. Les points d’entrée
de transition sont conservés dans le document et seront utilisés par le
placement gameplay lorsque les spawn points seront disponibles.
