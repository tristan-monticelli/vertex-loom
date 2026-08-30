# ADR-0132 — Sections avancées de l’inspecteur d’entité

- Statut : accepté
- Date : 2026-08-26

## Contexte

Les contraintes, chaînes IK, déformation, XPBD et machine d’états étaient
présents dans le contrat JSON et consommés par le runtime, mais restaient
inaccessibles dans l’éditeur.

## Décision

Asset Studio expose une section dédiée pour chaque système. Les champs édités
sont copiés dans une nouvelle `EntityDefinition`, validés par
`ProjectSession::set_selected_entity_definition`, puis enregistrés dans le
même historique de commandes que les autres propriétés de l’entité.

Les contrôles de création ne contournent pas la validation : une configuration
incomplète est refusée et conserve le document précédent.

## Conséquences

- Les contrats avancés sont découvrables et éditables sans modifier le JSON.
- La validation reste la source unique des contraintes de cohérence.
- La couverture UX E2E des interactions de ces panneaux reste à compléter.
