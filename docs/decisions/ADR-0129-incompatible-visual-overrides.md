# ADR-0129 — Confirmation des overrides visuels incompatibles

## Statut

Accepté — 2026-08-26

## Décision

Lorsqu’un nœud d’entité porte une instance `VisualComponent` avec des
overrides et que l’utilisateur choisit `none`, `texture` ou `vector`, Asset
Studio n’efface pas immédiatement les overrides. Il ouvre une confirmation
qui indique leur nombre et explique qu’ils appartiennent au composant courant.

La confirmation applique le changement de kind et supprime l’instance du
composant dans une mutation unique de nœud ; l’annulation ne modifie rien.
Changer vers un autre `VisualComponent` reste sans perte tant que les
overrides sont encore compatibles avec le composant sélectionné.

## Conséquences

- Une action de changement de drawable ne détruit plus silencieusement des
  paramètres édités.
- La validation, l’undo/redo, l’autosave et la publication restent centralisés
  dans `ProjectSession`.
- Une migration sélective d’overrides entre composants reste un travail séparé.

## Preuve UX

`asset_studio_ui_overrides_e2e` vérifie la modale rendue, l’annulation qui
conserve l’override et la confirmation qui retire l’instance avant le passage
au drawable texture.
