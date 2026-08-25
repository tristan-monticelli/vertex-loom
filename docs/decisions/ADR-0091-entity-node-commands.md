# ADR-0091 — Commandes de composition d’entité

- Statut : accepté
- Date : 2026-08-25

## Décision

La composition d’une `EntityDefinition` passe par trois opérations headless :
ajout d’un nœud, duplication d’un nœud et suppression d’un nœud feuille.
L’ajout valide l’identifiant, le parent, le drawable et le transform avec le
document complet ; la duplication génère un identifiant et un nom uniques.

Chaque opération remplace la liste des nœuds par une commande réversible non
fusionnée avec une autre opération structurelle et réutilise le flux dirty,
autosave, récupération et sauvegarde atomique de `ProjectSession`. Une
suppression est refusée tant que le nœud possède des enfants, afin de ne pas
créer implicitement des références orphelines.

## Conséquences

- Une hiérarchie multi-nœuds peut être construite sans éditer manuellement le
  JSON.
- Les références parentales restent valides après undo/redo et reload.
- Le réordonnancement avancé et les opérations de sous-arbre restent séparés
  pour préserver une sémantique de commande explicite.
