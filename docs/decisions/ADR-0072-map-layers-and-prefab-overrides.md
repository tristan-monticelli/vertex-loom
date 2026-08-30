# ADR-0072 — Édition des calques et overrides de prefabs

## Décision

`MapSession` expose des commandes séparées pour la visibilité, le verrouillage
et la profondeur d’un calque. Les valeurs non finies de profondeur sont
refusées. Les overrides de prefabs utilisent le même remplacement ou ajout
typé que les propriétés d’instances.

Map Studio expose ces mutations dans la liste des calques et les enregistre
dans l’historique de commandes. Le tri effectif du renderer et l’éditeur de
prefab visuel restent des étapes ultérieures.

## Conséquences

Les calques peuvent être inspectés et modifiés sans accès direct aux documents,
et un override de prefab reste récupérable par undo/redo. La sélection multiple,
le placement visuel et l’application d’overrides à toutes les instances restent
à construire.
