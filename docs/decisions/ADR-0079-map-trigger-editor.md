# ADR-0079 — Édition et visualisation des triggers

- Statut : accepté
- Date : 2026-08-25

## Décision

Map Studio sélectionne les triggers par leur index dans `MapDocument`, permet
de modifier leur événement et leur collision référencée, et affiche le nom de
l’événement à l’ancre de la collision dans le canvas. La mutation passe par
`MapSession::set_trigger`, qui valide la référence d’événement, la collision,
les doublons d’identifiant et le calque verrouillé.

## Conséquences

- Les événements doivent être déclarés avant d’être affectés à un trigger.
- Les références invalides sont refusées avant l’ajout à l’historique.
- Le nom d’événement est une annotation d’édition et ne modifie pas le format
  de `MapDocument v1`.
