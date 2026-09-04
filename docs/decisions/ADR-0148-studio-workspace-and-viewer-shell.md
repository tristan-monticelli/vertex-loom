# ADR-0148 — Coquille de workspace et commandes du viewer

- Statut : accepté
- Date : 2026-09-04

## Contexte

Asset Studio possédait trois zones redimensionnables, mais plaçait l'Inspector
à gauche et le Project Explorer à droite. Le viewer central n'exposait pas ses
commandes de cadrage : le zoom et le panoramique existaient seulement comme
gestes implicites sur certains canvas, la grille était toujours visible et le
fond ne pouvait pas être choisi. À la taille minimale 900 × 600, cette
inversion et les rangées d'actions longues rendaient les parcours Entity et
Animation difficiles à lire malgré leurs outils contextuels.

Les documentations officielles comparées dans l'audit du 3 septembre convergent
sur une hiérarchie ou un projet persistant, une vue centrale, les propriétés de
la sélection à droite et, pour l'animation, une timeline sous la vue.

## Décision

Asset Studio adopte une coquille stable `Project | Viewer | Inspector` :

- Project reste à gauche comme source de sélection et de drag ;
- Viewer conserve au moins 320 px et porte une barre d'outils commune ;
- Inspector reste à droite et ne montre que la ressource sélectionnée ;
- Timeline reste un dock inférieur du Viewer pour une Animation ;
- les deux séparateurs verticaux et le séparateur de Timeline restent bornés ;
- les actions du Project Explorer passent à la ligne selon la largeur utile.

La barre du Viewer expose `Fit`, zoom arrière, pourcentage, zoom avant,
`Grid` et un fond `Dark` ou `Light`. `Fit` remet le zoom à 100 % du cadrage
calculé et recentre le panoramique. La molette et le bouton du milieu restent
des raccourcis directs. Le fond et la grille s'appliquent aux previews raster,
vectorielles, Entity, Animation et composition ; ils ne modifient aucun asset.

Un probe graphique à 1440 × 900 et un autre à 900 × 600 doivent prouver l'ordre
des zones, la largeur minimale du Viewer et la présence des commandes. Une
capture produite sans affichage réel ne constitue pas une preuve.

## Conséquences

Le stockage des documents et les schémas JSON sont inchangés. Le nouvel état de
viewer est éphémère. Les parcours Entity et Animation conservent leur sélection
et leur timeline, mais suivent désormais les conventions des moteurs comparés.
Map Studio doit réutiliser la même grammaire gauche/centre/droite lorsqu'il sera
séparé de son formulaire historique ; ADR-0134 reste le contrat de ses panneaux
internes jusqu'à cette migration.
