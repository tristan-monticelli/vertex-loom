# ADR-0077 — Édition des triggers dans Map Studio

- Statut : accepté
- Date : 2026-08-25

## Décision

Map Studio expose la création et la suppression des `TriggerDefinition` déjà
définies par `MapDocument v1`. Chaque création demande un identifiant, un
événement existant et l’index d’une collision. La session valide le document
avant d’enregistrer l’opération dans `CommandStack`.

## Conséquences

- Les événements doivent être déclarés avant de créer un trigger.
- Un trigger ne peut pas être créé avec une collision inexistante ou une
  référence d’événement invalide.
- La suppression reste refusée si l’identifiant n’existe pas.
- Aucun nouveau contrat de données ni dépendance n’est introduit.
