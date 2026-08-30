# ADR-0054 — SceneDocument v1

## Statut

Accepté.

## Décision

`SceneDocument v1` est stocké sous `scenes/<id>.scene.json`. Il déclare les
maps composant une scène, son `entryMap` et des transitions vers d’autres
scènes avec un point d’entrée textuel. Une transition peut aussi déclarer
l’identifiant d’un événement gameplay qui la déclenche. Les références de maps et de scènes
entrent dans le registre global afin que le runtime refuse les scènes
incomplètes avant d’ouvrir une fenêtre. `game_runtime --scene <id>` transmet
la scène au Preview Runtime, qui résout directement l’`entryMap`.
