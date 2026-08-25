# ADR-0071 — Overrides typés d’instances de map

## Décision

`MapSession::set_instance_property` ajoute ou remplace une propriété par son
identifiant sur une instance. Chaque mutation passe par le snapshot commandé
de la session, donc undo/redo et dirty state restent cohérents. Un identifiant
vide est refusé ; les valeurs utilisent strictement `MapPropertyValue` du
contrat map.

## Conséquences

Les outils de placement peuvent personnaliser une instance sans modifier sa
définition d’entité. Les overrides de prefabs, la validation de schéma des
propriétés et l’interface de sélection restent à compléter.
