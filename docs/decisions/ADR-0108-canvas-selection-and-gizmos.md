# ADR-0108 — Sélection et gizmos du canvas

- Statut : accepté
- Date : 2026-08-26

## Contexte

Le premier canvas liait directement l'outil actif à tout glisser commencé sur
la forme. Un clic sur un fill avec Scale actif redimensionnait donc l'objet, et
les poignées pouvaient changer automatiquement d'outil. La poignée de rotation
ajoutait aussi un décalage vertical écran qui ne suivait pas l'orientation de
la forme.

## Décision

Un clic sur une forme sert d'abord à la sélectionner. Move peut glisser la
forme déjà sélectionnée ; Rotate, Scale et Pivot ne démarrent une mutation que
depuis leur poignée active. Une poignée ne change jamais l'outil courant.

Le hit-test transforme le pointeur monde vers le repère local de chaque nœud
et parcourt les nœuds visibles du dessus vers le dessous. La poignée de
rotation prolonge la direction centre-vers-bord déjà transformée : elle suit
donc la rotation, l'échelle et le zoom du nœud au lieu de pointer vers le haut
de l'écran.

Les calculs indépendants d'ImGui vivent dans `fabric_editor` afin d'être testés
sans fenêtre. Le canvas conserve seulement la capture du pointeur et le rendu
des contrôles.

## Conséquences

- Changer d'outil ne modifie jamais un document.
- Sélection et transformation sont deux actions distinctes.
- Les formes tournées à 180 degrés conservent une poignée extérieure visible.
- Les futurs studios peuvent réutiliser les mêmes règles de hit-test.
