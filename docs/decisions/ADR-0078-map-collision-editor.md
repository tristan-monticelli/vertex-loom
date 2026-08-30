# ADR-0078 — Édition des collisions dans Map Studio

- Statut : accepté
- Date : 2026-08-25

## Décision

Map Studio sélectionne les collisions par leur index stable dans `MapDocument`
et permet de modifier leur centre, rayon, longueur de capsule, points et statut
capteur.
La mutation passe par `MapSession::set_collision_shape`, qui valide le document
complet et refuse les collisions appartenant à un calque verrouillé.

## Conséquences

- Les points ajoutés ou supprimés doivent respecter le minimum validé par le
  type : trois pour un polygone, deux pour une chaîne.
- L’index de collision reste la référence utilisée par les triggers.
- Les sommets polygonaux et de chaînes peuvent être déplacés depuis le canvas
  ou modifiés dans l’inspecteur.
- Chaque modification est enregistrée dans `CommandStack` et peut être annulée.
- Aucun nouveau format de document ou dépendance n’est introduit.
