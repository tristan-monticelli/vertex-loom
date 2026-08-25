# ADR-0082 — Placement d’instances dans le canvas

- Statut : accepté
- Date : 2026-08-25

## Décision

Map Studio propose un mode de placement à clic unique. L’utilisateur fournit
un identifiant, une ressource et son type (`entity` ou `prefab`), puis le clic
convertit la position écran en unités monde et appelle `MapSession` avec les
réglages de snapping du canvas.

## Conséquences

- Le placement est refusé si l’identifiant, la ressource ou le calque actif est
  invalide/verrouillé.
- Le calcul des chunks et la validation restent centralisés dans la session.
- Le mode est réversible via `CommandStack` et ne crée aucun nouveau contrat.
