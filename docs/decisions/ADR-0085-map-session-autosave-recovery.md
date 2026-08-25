# ADR-0085 — Autosave et récupération de Map Studio

- Statut : accepté
- Date : 2026-08-25

## Décision

`MapSession` réutilise `AutosaveScheduler` et écrit le document map dans le
miroir `.vertex-loom/autosave/` après deux secondes d’inactivité, avec la
limite maximale de trente secondes. L’écriture passe par la sauvegarde
atomique et le parseur/validateur map strict.

À l’ouverture, une récupération n’est proposée que si l’autosave est valide
et plus récent que le document principal. `accept_recovery` charge l’état dans
la session et le marque dirty ; il ne remplace jamais le document principal.
`decline_recovery` ne modifie ni le document ni l’historique.

## Conséquences

- Les commandes Map Studio bénéficient de la même garantie de récupération que
  les documents Asset Studio.
- Le canvas peut continuer à éditer une map récupérée avant publication.
- Les erreurs d’autosave restent diagnostiquables dans `MapSession`.
- La récupération UI est explicite et n’écrase jamais silencieusement une map.
