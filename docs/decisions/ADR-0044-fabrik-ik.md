# ADR-0044 — Solveur FABRIK 2D borné

## Statut

Accepté.

## Décision

Les chaînes IK 2D utilisent FABRIK avec une racine conservée, des longueurs
de segments calculées une fois, un nombre maximal d’itérations fixe et une
tolérance explicite. Une cible hors de portée étire la chaîne dans sa direction
sans dépasser les longueurs cumulées. Les chaînes dégénérées ou non finies sont
refusées avant résolution.
