# ADR-0149 — Workspace de composition Map Studio

- Statut : accepté
- Date : 2026-09-04
- Remplace : ADR-0134 pour la disposition globale

## Contexte

ADR-0134 a séparé deux enfants ImGui, mais le premier contenait successivement
calques, instances, placement, canvas, transforms et collisions. Le canvas
était donc une étape d'un formulaire vertical de 360 px au lieu d'être le
centre de composition. Le second panneau ne commençait qu'aux événements et
triggers. L'écran sans map demandait en plus un identifiant de stockage avant
le nom visible.

Cette organisation contredit la convergence observée dans Godot, Unity,
Unreal, GameMaker et Construct : hiérarchie ou contenu à gauche, monde éditable
au centre, propriétés et logique de la sélection à droite.

## Décision

Lorsque la map est ouverte, Map Studio utilise trois panneaux persistants :

- `map-layers-pane` à gauche : calques, visibilité, verrouillage, ordre et liste
  des instances ;
- `map-canvas-pane` au centre : placement, temps de preview, canvas, cadrage,
  zoom/grille, snapping et transformation directe ;
- `map-selection-pane` à droite : collisions, événements, triggers, prefabs,
  overrides et mécanique de la sélection.

Deux séparateurs bornés préservent au moins 320 px de canvas à la taille
minimale 960 × 640. Les panneaux latéraux scrollent indépendamment. Les actions
globales Save, Preview, Validate et Publish restent dans l'en-tête du document.

Sans map ouverte, l'écran sépare clairement `Open an existing map` et `Create a
new map`. La création demande seulement un nom visible ; l'identifiant est
généré et affiché avant validation. Les identifiants techniques restent
éditables dans les inspecteurs avancés lorsqu'un contrat l'exige.
Le placement préremplit aussi un identifiant unique depuis la ressource choisie
et laisse l'auteur le remplacer seulement si nécessaire.

## Conséquences

Les contrats MapDocument et les fichiers existants restent inchangés. La
disposition ne déplace aucune commande métier et ne change ni l'historique ni
l'autosave. ADR-0134 reste applicable aux IDs stables, sections repliables,
pickers et navigation clavier, mais plus à la disposition en deux panneaux.
Une capture avec affichage réel doit montrer le canvas central et les trois IDs
de panneaux ; le test d'accessibilité ne peut pas être satisfait par un rendu
headless ignoré.
