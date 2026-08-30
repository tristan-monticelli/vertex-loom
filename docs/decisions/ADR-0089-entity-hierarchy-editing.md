# ADR-0089 — Édition hiérarchique des entités

- Statut : accepté
- Date : 2026-08-25

## Décision

Asset Studio expose l’inspection des nœuds de l’entité sélectionnée. Le nom,
le parent et le transform sont éditables nœud par nœud via `ProjectSession`.
Chaque mutation est une commande réversible ; le document actif est sauvegardé
atomiquement et bénéficie de l’autosave et de la récupération validée.

La validation du document est exécutée avant publication ou autosave. Elle
refuse les parents inexistants, les cycles et les transforms non finis.

## Conséquences

- Une entité mono-nœud créée peut être composée progressivement sans modifier
  directement le fichier principal.
- Changer de ressource pendant une édition d’entité dirty reste bloqué jusqu’à
  sauvegarde ou annulation de l’historique.
- Le rendu visuel complet des drawables hybrides et les gizmos d’entité restent
  des incréments ultérieurs.
