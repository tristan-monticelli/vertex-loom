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

`previewEntity` est une référence souple réservée au contexte d'auteur du
Studio. Elle reste persistée et validée comme une entité, mais ne participe ni
à la fermeture des dépendances runtime ni au graphe de packaging. Les
références stockées dans les clés d'animation restent, elles, des dépendances
fortes. Cette séparation évite le cycle structurel `Entity -> Animation ->
preview Entity` lorsqu'un clip est créé depuis puis joué par la même entité.

Le parseur est strict, la sauvegarde est atomique et l’évaluation est
déterministe. Les tangentes, easing, rotation courte, timeline et contraintes
restent des incréments ultérieurs.

## Conséquences

Le runtime peut évaluer un clip sans connaître l’éditeur. Les propriétés
non interpolables doivent utiliser `step`. Les cibles supprimées ou renommées
seront signalées par la résolution de binding avant intégration timeline.
Une entité de preview absente doit être signalée au chargement/à la validation
du document d'auteur, mais elle n'est pas copiée uniquement parce qu'elle sert
de contexte au clip.
