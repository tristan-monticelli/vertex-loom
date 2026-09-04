# ADR-0152 — Poignées de transformation des mécaniques

## Statut

Accepté — 2026-09-04.

## Contexte

Le canevas spatial de Mechanics permet de sélectionner et déplacer les corps,
pivots et capteurs, mais la taille et la rotation restent cachées dans
l'inspecteur. Ce parcours oblige à alterner entre le canevas et des champs
numériques pour une opération spatiale élémentaire, alors que le contrat
`MechanicGraph v1` expose déjà `size` et `rotation`.

## Décision

La forme rectangulaire sélectionnée affiche une poignée carrée de
redimensionnement dans son angle local supérieur droit. Un corps affiche aussi
une poignée circulaire de rotation, prolongée depuis son axe local vertical.
Les capteurs peuvent être déplacés et redimensionnés mais ne reçoivent pas une
rotation que leur contrat ne possède pas.

Un joint relié à un pivot expose une couronne autour de celui-ci. Le centre
sélectionne le pivot ; la couronne sélectionne le joint et son glisser déplace
la propriété `position` du pivot relié. Cette indirection est explicite dans
l'état éphémère du widget : le joint reste sélectionné tandis que la commande
cible le pivot, unique propriétaire de l'ancrage spatial dans le contrat.

Le glisser fournit un aperçu continu sans muter le document. Au relâchement,
une seule commande `MechanicSession::set_node_property` écrit `position`,
`center`, `size` ou `rotation`; validation, undo/redo et dirty state restent
donc centralisés dans la session. La taille est bornée à `0,1` unité par axe et
la rotation est exprimée en degrés. Le redimensionnement conserve le centre de
la forme et suit ses axes locaux.

La sélection, le type de glisser et les coordonnées intermédiaires sont des
états de vue éphémères. Aucun champ n'est ajouté au schéma persistant. Les
probes E2E observent les positions dessinées des poignées, mais seule une suite
d'événements souris peut déclencher les mutations testées.

## Alternatives

Conserver uniquement les champs numériques maintiendrait deux contextes pour
une même opération visuelle. Persister un transform propre au widget
dupliquerait le contrat physique et introduirait une migration inutile.
Appliquer une commande à chaque mouvement de souris saturerait l'historique
d'undo et reconstruirait la simulation pendant le glisser.

## Conséquences

- Le parcours nominal placement → taille → rotation reste dans le canevas.
- Les anciennes mécaniques restent compatibles sans migration.
- Une forme tournée est dessinée et testée dans ses axes réels.
- Un joint privé de liaison pivot reste visible dans le graphe, sans couronne
  spatiale puisqu'il ne possède aucun ancrage à projeter.
- La preuve d'acceptation sauvegarde et recharge les valeurs produites par de
  vrais gestes UI avant de publier et lancer le paquet.
