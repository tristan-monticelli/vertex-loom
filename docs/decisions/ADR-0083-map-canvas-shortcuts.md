# ADR-0083 — Raccourcis du canvas Map Studio

- Statut : accepté
- Date : 2026-08-25

## Décision

Lorsque le canvas est survolé et qu’aucun champ texte n’est actif, Map Studio
interprète `Delete` comme une suppression batch, les flèches comme un nudge
d’une unité monde avec snapping, et `Ctrl+D` comme une duplication avec offset
initial de `(1, 1)`. Chaque action appelle `MapSession`.

## Conséquences

- Les sélections verrouillées sont refusées sans modification partielle.
- La suppression d’une multi-sélection est une seule commande undoable.
- Les raccourcis ne capturent pas les touches pendant l’édition de texte.
