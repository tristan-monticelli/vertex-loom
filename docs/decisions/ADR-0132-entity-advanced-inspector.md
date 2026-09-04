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

La création nominale d'une chaîne IK n'est plus cachée dans ce formulaire.
Dans l'Entity workspace, l'auteur sélectionne au moins deux nœuds dans l'ordre
racine→extrémité puis invoque `Create IK from selected joints`. Une seule
commande ajoute une cible sœur déplaçable, légèrement décalée de l'extrémité,
et une chaîne FABRIK valide. Le canvas dessine les segments, la liaison vers la
cible et les poignées existantes ; déplacer la cible réutilise le gizmo Entity
et l'historique normal. Les itérations et la tolérance restent dans l'inspecteur
avancé.

## Conséquences

- Les contrats avancés sont découvrables et éditables sans modifier le JSON.
- Une chaîne IK valide peut être créée et manipulée sur le canvas sans saisir
  d'identifiants ni ouvrir le JSON.
- La validation reste la source unique des contraintes de cohérence.
- La couverture UX E2E des interactions de ces panneaux reste à compléter.
