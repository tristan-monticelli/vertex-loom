# ADR-0119 — InputBinding v2

## Statut

Accepté.

## Décision

`InputDocument` reste le registre des signaux physiques, séparé de
`BehaviorGraph`. Chaque binding porte désormais un kind (`button` ou `axis`),
un seuil et une dead zone. Les documents v1 sont migrés en mémoire vers les
valeurs par défaut v2 afin de préserver les remappings existants.

## Conséquences

- Le runtime conserve la compatibilité des bindings boutons v1.
- L’éditeur peut préparer des axes sans encoder ces réglages dans le graphe de
  comportement.
- Les valeurs sont validées (`threshold` dans `[0,1]`, `deadZone` dans `[0,1)`).
