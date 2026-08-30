# ADR-0043 — Ordre déterministe des contraintes d’animation

## Statut

Accepté.

## Décision

Les contraintes `copy_transform`, `limits` et `look_at` portent un identifiant,
une cible, une source et un rang d’exécution explicite. Les rangs sont uniques
et les dépendances source-vers-cible sont vérifiées avant résolution.

Tout cycle est refusé. L’ordre d’exécution est le tri stable par rang ; aucune
résolution implicite basée sur l’ordre mémoire ou l’ordre d’un conteneur n’est
autorisée.
