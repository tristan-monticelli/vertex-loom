# ADR-0094 — Liaison d’animation d’une instance de map

## Statut

Accepté.

## Contexte

`AnimationClip v1` cible des nœuds par identifiant stable, tandis que
`MapDocument v1` possède déjà des propriétés custom typées sur ses instances.
Un champ de scène supplémentaire n’est pas encore nécessaire pour une
première lecture runtime.

## Décision

La propriété custom d’instance dont l’identifiant est `animation` est réservée
à une `ResourceReference` dont `expectedType` vaut `animation`. Elle est
facultative et unique par instance. Le validateur MapDocument vérifie sa forme,
et `PreviewRuntime::load` vérifie que le clip correspondant est chargé avant
d’initialiser la physique ou SDL.

Pendant le rendu, le runtime évalue le clip à l’horloge fixe de simulation.
Les pistes `transform.position`, `transform.rotationDegrees`, `transform.scale`,
`material.color` et `material.opacity` sont appliquées aux draw packets du nœud
ciblé. Les couleurs et opacités remplacent les valeurs rendues ; les autres
propriétés restent évaluables headless mais ne modifient pas encore les packets.

## Conséquences

- Une map peut persister une animation par instance sans changer de schéma.
- Une référence invalide ou absente est refusée avant toute fenêtre runtime.
- Le premier binding est déterministe et compatible avec les checkpoints.
- États, contraintes et animation par défaut d’entité restent des incréments
  ultérieurs.
