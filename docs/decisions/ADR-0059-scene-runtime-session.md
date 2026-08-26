# ADR-0059 — Session runtime de scènes

## Décision

`SceneRuntimeSession` charge une scène et toutes les maps déclarées après
validation du projet, puis résout les transitions par `ResourceId`. Une
transition est préparée dans des documents temporaires ; la scène courante
n’est remplacée que si la cible, toutes ses maps et son point d'entrée sont
valides.

`SceneMapReference.layer_id` est un identifiant de montage unique dans la
scène. La composition préfixe par cet identifiant les couches, prefabs,
instances et triggers locaux de chaque map, tout en conservant les références
vers les ressources projet. Les événements de même identifiant sont fusionnés
uniquement si leur payload est identique ; sinon la scène est refusée.
`entryMap` reste la map gameplay principale et doit appartenir à `maps`, mais
ne limite plus le chargement aux données de cette map.

Une `SceneTransition` peut porter l’identifiant optionnel d’un événement
Gameplay. `transition_for_event` sélectionne la première transition déclarée
pour cet identifiant et réutilise la même préparation atomique que
`transition`.

Un point d'entrée est une instance de map portant exactement une propriété
texte `sceneEntryPoint`. Sa valeur est l'identifiant ciblé par
`SceneTransition.entry_point` et son `Transform.position` est le spawn monde.
Les identifiants de points d'entrée doivent être uniques dans la scène
composée. Une transition vers un point absent ou ambigu échoue sans remplacer
la scène courante. Preview Runtime reçoit cette position comme spawn du
personnage au prochain chargement.

`PreviewRuntimeOptions` accepte exactement un `map_id` ou un `scene_id`. Avec
`scene_id`, Preview Runtime charge et valide lui-même le `SceneDocument`, le
conserve via `scene()` et compose toutes ses maps avant de charger la map.
`game_runtime --scene <id>` transmet donc directement l’identifiant au runtime
sans conversion préalable.

## Conséquences

La résolution des scènes est testable sans SDL ni fenêtre et peut être
réutilisée par le Preview Runtime et le runtime jouable. Les anciennes scènes
à une map restent équivalentes : leur identifiant de montage ne change que les
identifiants internes de la map composée.
