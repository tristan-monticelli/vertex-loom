# ADR-0080 — Édition des payloads d’événements

- Statut : accepté
- Date : 2026-08-25

## Décision

Map Studio permet de sélectionner un `MapEventDefinition`, d’ajouter ou de
remplacer ses propriétés payload avec les six types de `MapPropertyValue`, et
de supprimer une propriété. `MapSession::set_event_payload` refuse les
identifiants vides ou dupliqués et enregistre la mutation dans `CommandStack`.
Les payloads sont affichés dans l’inspecteur du trigger qui référence
l’événement.

## Conséquences

- Les événements restent nommés par `ResourceId` et les triggers ne copient pas
  leur payload.
- Une modification du payload est immédiatement visible par tous les triggers
  qui référencent l’événement.
- Aucun nouveau type de propriété n’est introduit.
