# ADR-0021 — Import Aseprite et atlas de sprites déterministe

- Status: legacy — retained for compatibility, excluded from target architecture by ADR-0022
- Date: 2026-08-24

## Context

Asset Studio doit transformer des sources Aseprite et des découpages PNG en
ressources directement chargeables, sans exécutable Aseprite installé. Le
résultat doit être portable, validable sans fenêtre et identique sur macOS,
Windows et Linux.

## Decision

Définir `SpriteSheetDefinition` version 1 dans `fabric_project`. Le document
`assets/textures/<id>.sprite.json` référence une source conservée
`<id>.aseprite` ou `<id>.source.png`, un atlas généré `<id>.atlas.png`, les
rectangles de frames, leurs durées et pivots, les tags et les slices.

Implémenter dans `fabric_render` un lecteur binaire C++20 conforme à la
[spécification officielle Aseprite](https://github.com/aseprite/aseprite/blob/main/docs/ase-file-specs.md).
Il accepte RGBA, grayscale et indexed, les palettes, groupes, cels raw,
compressés ou liés, tags, slices et pivots. Les références externes, tilemaps,
modes de fusion non pris en charge, dimensions excessives, fichiers tronqués
et chunks inconnus sont refusés avec un diagnostic structuré.

Épingler zlib `v1.3.2` par `FetchContent` pour décoder les cels et encoder les
atlas PNG. Le packer MaxRects trie ses entrées et ses choix avec des clés
totales, ajoute un pixel transparent de padding et un pixel d’extrusion, puis
encode des scanlines sans filtre avec des paramètres zlib fixes.

Le découpage PNG manuel utilise le même pipeline. Une grille produit ses
rectangles en ordre ligne-colonne ; les frames libres conservent l’ordre saisi.
La régénération remplace atomiquement l’atlas et le document validés mais ne
modifie jamais la source conservée.

## Alternatives

Piloter l’exécutable Aseprite créerait une dépendance externe et des résultats
variables selon sa version. Conserver uniquement le PNG exporté perdrait les
durées, tags et pivots. Un packer dépendant de l’ordre d’un conteneur ou du GPU
ne fournirait pas de sortie reproductible.

## Consequences

L’import fonctionne hors ligne après compilation et produit un contrat commun
aux éditeurs et au runtime. Le lecteur refuse explicitement les fonctions
Aseprite qui modifieraient le rendu sans être implémentées, au lieu de générer
silencieusement un atlas incorrect. Les nouvelles suites testent les bytes PNG
et le JSON produits, ainsi que les variantes et corruptions du format source.
