# ADR-0078 — Édition des collisions dans Map Studio

- Statut : accepté
- Date : 2026-08-25

## Décision

Map Studio sélectionne les collisions par leur index stable dans `MapDocument`
et permet de modifier leur centre, rayon, longueur de capsule et statut capteur.
La mutation passe par `MapSession::set_collision_shape`, qui valide le document
complet et refuse les collisions appartenant à un calque verrouillé.

## Conséquences

- Les collisions polygonales et les chaînes restent inspectables mais leur
  édition de points est reportée à un incrément dédié.
- L’index de collision reste la référence utilisée par les triggers.
- Chaque modification est enregistrée dans `CommandStack` et peut être annulée.
- Aucun nouveau format de document ou dépendance n’est introduit.
