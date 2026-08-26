# ADR-0033 — Transport headless des fills image vectoriels

- Status: accepted
- Date: 2026-08-25

## Context

`VectorAsset v2` sait déjà référencer une texture locale et décrire son cadrage,
son transform UV, son opacité et sa déformation avec la forme. Un draw packet
qui ne transporte que la couleur solide perdrait cette information et
forcerait une conversion en sprite ou en bitmap persistante.

## Decision

`VectorDrawPacket` porte un `VectorImageFill` optionnel en plus de la couleur
solide optionnelle. Les fills couleur et image réutilisent la même silhouette
aplatie et la même triangulation déterministe. La référence texture reste
opaque au renderer headless ; sa résolution et son upload appartiennent à la
phase de compositing.

## Consequences

Le packet décrit suffisamment le fill sans posséder de pixels ni d’atlas. Les
textures doivent être validées par le registre de ressources avant publication.
Le renderer OpenGL devra appliquer le fit, le transform, l’opacité et
`deform_with_shape` lors d’un incrément ultérieur.
Les nouvelles liaisons utilisent `deform_with_shape = false` par défaut afin
que cette capacité différée ne transforme jamais implicitement une image.
