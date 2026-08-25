# ADR-0052 — Résolution des drawables vectoriels du Preview Runtime

## Statut

Accepté.

## Décision

Le Preview Runtime résout chaque instance vers son `EntityDefinition` direct
ou via son prefab, puis charge les `VectorAsset` référencés par les nœuds. Les
SVG liés sont convertis en géométrie bornée à l’exécution ; les vecteurs natifs
sont tessellés par le même constructeur de draw packets que l’éditeur.

Les transforms de nœuds parents sont appliqués avant la transform d’instance,
et le matériau multiplie la couleur et l’opacité du packet. Les nœuds texture
ont temporairement un placeholder déterministe jusqu’à l’arrivée du resolver
de textures OpenGL ; aucune texture n’est silencieusement traitée comme une
géométrie persistante.
