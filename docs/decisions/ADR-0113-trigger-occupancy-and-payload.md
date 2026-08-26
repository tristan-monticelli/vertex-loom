# ADR-0113 — Occupation multi-acteurs des triggers

## Statut

Accepté — 2026-08-26.

## Contexte

Le runtime évaluait chaque trigger contre le point central du seul personnage
CLI. Les instances de joueur, monstre ou décor mobile n'émettaient aucun
événement, une collision `chain` était acceptée malgré une sémantique toujours
fausse, et les propriétés du trigger étaient ignorées.

## Décision

Une collision référencée par un trigger est obligatoirement un sensor fermé de
type cercle, capsule ou polygone. Les chains restent des segments physiques
solides et sont refusées comme zones. Le même bit `sensor` est transmis à
Box2D ; le validateur rejette donc toute divergence avant le runtime.

`TriggerRuntime` reçoit à chaque pas la liste des acteurs présents avec un
identifiant stable et leurs bounds monde. Il suit l'occupation par couple
trigger/acteur et émet une entrée ou sortie pour chacun. Preview Runtime fournit
le personnage avec sa box Box2D et chaque instance d'entité avec l'union de ses
bounds rendues ; une instance sans artwork utilise une box physique de repli
centrée de 1 × 1 unité. La propriété d'instance `triggerHalfExtents` permet de
déclarer explicitement les demi-dimensions positives utilisées pour
l'occupation, indépendamment de l'artwork. `triggerActor=false` exclut
explicitement un marqueur ou une décoration de cette participation ; toutes
les autres instances participent par défaut.

`GameplayEvent` expose `actor_id`. Son payload part du payload de l'événement,
puis les propriétés propres au trigger remplacent les clés de même identifiant
et ajoutent les autres clés. Cette priorité rend une zone configurable sans
dupliquer l'événement global.

## Conséquences

- Les joueurs, monstres et autres instances utilisent le même chemin runtime.
- Une zone détecte l'intersection de bounds, pas uniquement un centre.
- Plusieurs acteurs peuvent occuper simultanément un même trigger.
- Les tests couvrent formes, payload fusionné, entrées/sorties indépendantes et
  intégration Preview Runtime sans `--character`.
