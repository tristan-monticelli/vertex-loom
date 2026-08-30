# ADR-0127 — Suppression protégée des collisions de map

## Statut

Accepté — 2026-08-26

## Décision

Map Studio demande confirmation avant de supprimer une collision. La session
refuse l’opération si un trigger la référence ; l’interface expose le nombre
de références et demande de retirer ces triggers d’abord. Lorsqu’aucune
référence n’existe, les indices de collisions des triggers suivants sont
réindexés dans la même commande undoable.

Les événements et triggers suivent la même politique dans l’interface : une
référence entrante bloque la suppression de l’événement, tandis que le trigger
est supprimé après confirmation sans supprimer sa collision ni son événement.

## Conséquences

- Une suppression confirmée ne laisse pas de référence dangling dans le
  document de map.
- Undo/redo restaure ou réapplique la collision et les indices associés comme
  une unité.
- La suppression en cascade reste explicitement refusée ; l’utilisateur doit
  choisir les dépendances à retirer.
