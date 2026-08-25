# ADR-0039 — Contrat AnimationClip v1

## Statut

Accepté.

## Contexte

Les entités doivent pouvoir exposer des propriétés animables sans que le
format d’animation dépende d’un composant ou d’une interface graphique.

## Décision

`AnimationClip v1` est un document JSON autonome stocké sous
`assets/animations/<id>.animation.json`. Chaque piste cible un
`nodeId + componentId + propertyId` stable et contient des clés typées.
La version initiale accepte les scalaires, `Vec2`, couleurs, booléens et
références de ressources, avec les interpolations step, linear et cubic.
Les références restent typées et sont validées avant publication.

Le parseur est strict, la sauvegarde est atomique et l’évaluation est
déterministe. Les tangentes, easing, rotation courte, timeline et contraintes
restent des incréments ultérieurs.

## Conséquences

Le runtime peut évaluer un clip sans connaître l’éditeur. Les propriétés
non interpolables doivent utiliser `step`. Les cibles supprimées ou renommées
seront signalées par la résolution de binding avant intégration timeline.
