# ADR-0106 — Composants visuels dans les entités

## Statut

Accepté.

## Décision

`EntityDefinition` passe en v2. Un drawable accepte désormais
`visualComponent` en plus de `none`, `vector` et `texture`. Il référence un
`VisualComponent v1` et porte un `VisualComponentInstance` optionnel pour la
variante, l'ancre et les overrides typés. Les matériaux restent réservés aux
drawables texture/vectoriel ; un composant gère ses matériaux dans sa
composition.

Le parseur accepte v1 et le migre en mémoire vers v2 sans modifier ses
drawables. Un document v1 ne peut pas déclarer `visualComponent`. Toute
sauvegarde ultérieure sérialise la v2.

Asset Studio et Preview Runtime résolvent le composant avec le resolver partagé
de `fabric_render`, puis appliquent la hiérarchie et le transform d'instance de
l'entité aux draw packets obtenus. Aucun preset n'ajoute de branche au renderer.

## Conséquences

- Les composants visuels deviennent plaçables et animables comme n'importe
  quel drawable d'entité.
- Le graphe de ressources suit la référence du composant et ses overrides.
- Les fixtures et tests couvrent migration v1, round-trip v2 et égalité des
  paquets Studio/runtime.
