# ADR-0052 — Résolution des drawables vectoriels du Preview Runtime

## Statut

Accepté.

## Décision

Le Preview Runtime résout chaque instance vers son `EntityDefinition` direct
ou via son prefab, puis charge les `VectorAsset` référencés par les nœuds. Les
SVG liés sont convertis en géométrie bornée à l’exécution ; les vecteurs natifs
sont tessellés par le même constructeur de draw packets que l’éditeur.

Les transforms de nœuds parents sont appliqués avant la transform d’instance,
et le matériau multiplie la couleur et l’opacité du packet. Les textures sont
chargées depuis leur `TextureAsset`, décodées en RGBA8 après l’initialisation
SDL, uploadées une fois dans le contexte OpenGL puis résolues par identifiant.
Les packets sont triés par identifiant stable et les géométries hors du
viewport sont écartées avant l’appel au renderer.
