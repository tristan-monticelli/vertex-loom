# ADR-0150 — Inspecteurs progressifs Entity et Animation

- Statut : accepté
- Date : 2026-09-04

## Contexte

Les workspaces Entity et Animation disposent du canvas, de la hiérarchie, de la
timeline et des commandes nécessaires, mais l'Inspector présente encore le
contrat persistant presque dans son ordre de sérialisation. Un nouveau créateur
rencontre ainsi les systèmes XPBD, les IDs de nœuds, le parentage, le pivot, le
Z-order, les bindings et les valeurs évaluées avant d'avoir accompli sa tâche
élémentaire : choisir un élément, le déplacer puis l'animer.

Les tests existants prouvent la mutation et le rendu, pas la lisibilité de ce
chemin. La présence d'une fonction dans un panneau n'est donc pas une preuve
qu'elle est découvrable ou utilisable sans connaître le modèle interne.

## Décision

Les Inspectors Entity et Animation appliquent une divulgation progressive :

- le parcours nominal montre d'abord la sélection courante, l'action suivante
  et les propriétés visuelles courantes ;
- les ressources et nœuds sont choisis par leur nom visible ; l'ID persistant
  n'est qu'une information avancée ;
- `Animate selected node…` conserve l'Entity et le nœud courants, puis ouvre un
  clip ciblé ;
- le choix du nœud, le playhead, l'auto-key et les quatre clés de transform sont
  visibles sans ouvrir un volet avancé ;
- parentage, pivot, ordre Z, changement de type, overrides, systèmes de logique,
  contraintes, IK, déformation, XPBD, bindings bruts et courbes restent
  disponibles dans des sections avancées fermées par défaut ;
- les actions secondaires de hiérarchie sont regroupées au lieu de former une
  rangée horizontale tronquée.

La création d'une Entity ou d'une Animation demande les choix indispensables.
Les transforms initiaux, marqueurs et détails de document sont repliés. Les
valeurs par défaut existantes restent appliquées et restent modifiables ensuite.

## Critères d'acceptation

- Une Entity existante expose son nœud sélectionné, sa visibilité, son nom et
  son transform sans faire défiler un système avancé.
- Une Animation ciblée permet de choisir un nœud par son nom et de poser une clé
  Position, Rotation, Scale ou Pivot depuis le panneau visible.
- Le chemin Entity → Animation ne demande aucun ID technique.
- À 900 × 600, les actions nominales ne sont pas tronquées horizontalement.
- Les E2E graphiques activent le chemin visible, sauvegardent, rechargent et
  produisent une capture avec un vrai contexte OpenGL.
- Entity v4 et AnimationClip v3 restent inchangés et les anciennes ressources
  conservent leur rendu.

## Conséquences

La modification concerne l'orchestration ImGui et ses preuves. Les commandes de
`ProjectSession`, les validateurs, l'autosave, le renderer et les schémas JSON
restent les sources de vérité existantes. Les fonctions expertes ne sont ni
supprimées ni dupliquées ; elles changent seulement de niveau de visibilité.
