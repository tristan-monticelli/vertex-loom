# ADR-0130 — Gizmo de translation du canvas d’entité

## Statut

Accepté — 2026-08-26

## Décision

Le canvas d’entité affiche une croix centrée sur la position du nœud courant.
Lorsque le nœud n’est pas verrouillé, un clic proche de cette croix démarre un
drag ; le déplacement écran est converti avec l’échelle du canvas puis appliqué
à la position locale du nœud. Chaque mise à jour appelle
`ProjectSession::set_selected_entity_node`, afin de conserver validation,
fusion de commandes et dirty state.

Les previews de visual components restent en lecture seule. La sélection du
nœud continue d’être pilotée par l’arbre de l’inspecteur jusqu’à l’ajout d’une
sélection directe par hit-test sur le canvas.

## Conséquences

- Le gizmo respecte le verrouillage et ne modifie pas directement les draw
  packets temporaires.
- Le canvas et l’inspecteur partagent la même mutation undoable.
- `asset_studio_entity_e2e` injecte un clic/drag SDL sur le gizmo, puis vérifie
  la position après sauvegarde et reload.
