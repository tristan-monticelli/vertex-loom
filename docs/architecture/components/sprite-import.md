# C4 Component — import Aseprite et sprites

```mermaid
C4Component
    title Vertex Loom — import et atlas de sprites
    Container_Boundary(render, "fabric_render") {
        Component(reader, "Aseprite reader", "C++20 / zlib 1.3.2", "Parse, décompresse et compose les frames sans outil externe")
        Component(slicer, "PNG slicer", "C++20", "Produit des frames depuis une grille ou des rectangles libres")
        Component(packer, "Deterministic MaxRects", "C++20", "Place, extrude et encode un atlas PNG reproductible")
    }
    Container_Boundary(editor, "fabric_editor") {
        Component(importer, "Sprite importer", "C++20", "Orchestre lecture, atlas, contrat et publication")
    }
    Container(project, "fabric_project", "C++20 / JSON", "Valide et publie SpriteSheetDefinition v1")
    Container(asset, "Asset Studio", "SDL2 / Dear ImGui", "Choisit une source et configure le découpage")
    ContainerDb(files, "Project Files", "ASEPRITE + PNG + JSON", "Source conservée, atlas généré et document versionné")
    Rel(asset, importer, "Demande l’import ou la régénération")
    Rel(importer, reader, "Lit une source Aseprite")
    Rel(importer, slicer, "Découpe une source PNG")
    Rel(reader, packer, "Fournit les frames composées")
    Rel(slicer, packer, "Fournit les frames découpées")
    Rel(importer, project, "Publie le bundle validé")
    Rel(project, files, "Valide les trois fichiers")
```

## Contract

- Le parseur borne la source à 256 Mio, le canvas à 16384 pixels par axe, le
  total à 67108864 pixels, le nombre de frames à 65535 et chaque allocation
  avant décompression.
- Le format est little-endian et chaque taille de frame, chunk, chaîne, palette
  et flux compressé est vérifiée avant lecture.
- Une frame Aseprite est composée à partir des calques visibles et de leurs
  groupes. Les modes de fusion autres que normal, les tilemaps, les références
  externes et les z-index non nuls sont refusés tant qu’ils ne sont pas rendus
  fidèlement.
- Les cels indexed utilisent la palette active de leur frame et l’index
  transparent du header pour les calques non background.
- Les frames liées résolvent uniquement une frame antérieure du même calque ;
  toute référence absente, future ou cyclique est refusée.
- Le packer utilise une comparaison totale indépendante des adresses et de
  l’ordre des conteneurs associatifs. Le rectangle publié exclut padding et
  extrusion.
- `SpriteSheetDefinition` référence uniquement les chemins canoniques dérivés
  de son identifiant. Le JSON est publié en dernier et marque la ressource
  chargeable.
- Chaque frame issue d’un PNG conserve aussi son rectangle `inputBounds` dans
  la source ; la grille et les frames libres sont donc régénérables sans état
  d’interface caché.
- Une régénération valide entièrement le nouvel atlas et le nouveau document,
  puis remplace chacun atomiquement avec rollback de l’atlas si le document ne
  peut pas être remplacé ; la source originale reste inchangée.

## Implementation

- `fabric/render/aseprite.hpp` expose le document décodé sans type zlib.
- `fabric_render/src/aseprite.cpp` contient le lecteur borné, la résolution des
  cels et la composition CPU déterministe.
- `fabric/render/sprite_atlas.hpp` expose découpage et génération d’atlas ;
  `fabric_render/src/sprite_atlas.cpp` porte MaxRects, extrusion et PNG.
- `fabric/project/sprite_sheet.hpp` expose `SpriteSheetDefinition v1` et
  `fabric_project/src/sprite_sheet.cpp` porte son JSON, sa publication et la
  validation complète signature/chunks/CRC/zlib de l’atlas PNG.
