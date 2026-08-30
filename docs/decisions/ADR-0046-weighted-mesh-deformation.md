# ADR-0046 — Déformation maillée pondérée par nœuds

## Statut

Accepté.

## Décision

Un maillage de déformation porte des sommets de repos, des triangles et une
liste d’influences `nodeId + weight` par sommet. Chaque pose applique le
transform du nœud au sommet de repos, puis les résultats sont mélangés par les
poids normalisés.

Les poids doivent avoir une somme positive, les triangles doivent être indexés
dans le maillage et les poses doivent être uniques et finies. Une influence
sans pose est une référence manquante et annule le résultat.
