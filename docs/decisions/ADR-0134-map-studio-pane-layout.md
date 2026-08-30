# ADR-0134 — Panneaux séparés dans Map Studio

- Statut : accepté
- Date : 2026-08-26

## Contexte

Map Studio regroupait les calques, instances, placement, collisions, triggers
et overrides dans deux colonnes ImGui implicites. Cette disposition rendait la
sélection courante difficile à suivre et ne donnait pas d’identité aux zones
fonctionnelles.

## Décision

Le contenu de la carte est organisé en deux panneaux persistants :
`map-layers-pane` pour la hiérarchie des calques et
`map-selection-pane` pour le contenu et l’inspecteur liés à la sélection. Les
deux panneaux utilisent des `BeginChild` bordés avec scroll horizontal local.
Un séparateur horizontal interactif conserve une largeur bornée du panneau des
calques et expose le curseur de redimensionnement. Les sous-zones `Layers` et
`Events` sont des sections repliables avec des IDs stables, afin de garder la
navigation clavier exploitable quand la carte contient beaucoup de contenu.
