# C4 Component — sources et authoring vectoriels

```mermaid
C4Component
    title Vertex Loom — migration de l’import SVG vers l’authoring natif
    Container_Boundary(render, "fabric_render") {
        Component(loader, "SVG preview loader", "C++20 / SDL2_image / NanoSVG", "Valide la taille du fichier et rasterise un aperçu RGBA8 borné")
        Component(converter, "SVG native converter", "C++20 / NanoSVG", "Convertit explicitement les chemins Bézier et styles simples vers VectorAsset v2 et rapporte les pertes")
        Component(image, "RasterImage", "C++20", "Aperçu possédé par le moteur sans type SDL ou OpenGL public")
        Component(geometry, "Native vector renderer", "C++20 / OpenGL", "Valide les contours, applique les transforms, aplatit, triangule, met en cache et produit des draw packets déterministes")
        Component(gl_renderer, "OpenGL vector backend", "OpenGL 3 / SDL loader", "Compile le programme couleur, gère VAO/VBO/IBO et dessine les triangles des packets")
    }
    Container_Boundary(asset, "Asset Studio") {
        Component(import_ui, "SVG import dialog", "Dear ImGui", "Saisit la source, l'identifiant et le nom puis présente les diagnostics")
        Component(customizer, "Vector customizer", "Dear ImGui", "Crée et édite formes, fills, contours, clips et transforms")
        Component(gpu_preview, "OpenGL preview texture", "OpenGL", "Téléverse temporairement l'aperçu rasterisé")
    }
    Container_Boundary(editor, "fabric_editor") {
        Component(importer, "Vector importer", "C++20", "Orchestre validation, copie et publication du document VectorAsset")
        Component(conversion_command, "SVG conversion command", "C++20 / CommandStack", "Publie la conversion native avec undo/redo et conserve le SVG lié lors de l’annulation")
    }
    Container(preview_cli, "fabric_asset_preview", "C++20 CLI", "Résout un VectorAsset natif et émet ses draw packets JSON sans fenêtre")
    Container(project, "fabric_project", "C++20 / JSON", "Valide et publie le contrat VectorAsset")
    ContainerDb(files, "Local Files", "SVG", "Source vectorielle choisie par l'utilisateur")
    Rel(import_ui, importer, "Demande l'import")
    Rel(importer, loader, "Valide et rasterise")
    Rel(importer, converter, "Convertit sur action explicite")
    Rel(import_ui, conversion_command, "Confirme la conversion")
    Rel(conversion_command, project, "Publie le document natif")
    Rel(preview_cli, project, "Charge le VectorAsset")
    Rel(preview_cli, geometry, "Construit les packets")
    Rel(importer, project, "Publie source et document")
    Rel(loader, files, "Lit")
    Rel(loader, image, "Produit")
    Rel(import_ui, gpu_preview, "Téléverse")
    Rel(image, gpu_preview, "Fournit les pixels")
    Rel(customizer, geometry, "Prévisualise VectorAsset v2 natif")
    Rel(geometry, gl_renderer, "Fournit les packets")
```

## Contract

- L'import accepte uniquement l'extension `.svg`, sans tenir compte de la
  casse, puis laisse NanoSVG vérifier le contenu.
- La source SVG originale est copiée sans conversion. Le lecteur migre les
  documents v1 en mémoire vers `sourceKind = linkedSvg`, sans réécrire le SVG,
  qui reste prévisualisé par NanoSVG.
- Le contrat réserve `sourceKind = native` à une géométrie sans dépendance SVG.
  Le socle actuellement chargeable couvre taille, origine, nœuds stables,
  transforms, rectangles, ellipses et fills couleur, transparents ou image.
  L’image possède cadrage, transform, opacité et liaison à la déformation ; le
  registre vérifie sa référence texture. Chemins, contours et clips restent des
  extensions explicites.
- Une conversion de SVG lié vers natif est explicite et doit présenter les
  éléments non pris en charge avant publication. Le convertisseur NanoSVG
  couvre les chemins cubiques, fills couleur et contours simples ; gradients et
  autres paints non supportés produisent un diagnostic et ne sont jamais
  supprimés silencieusement.
- Asset Studio expose la conversion dans l’inspecteur du SVG lié. La commande
  est réversible, sauvegarde le `VectorAsset v2 native` au même emplacement
  JSON et ne modifie jamais le fichier SVG source.
- `fabric_asset_preview <project> <vector-id>` valide le document natif et
  émet les sommets, indices, contours, fills et UV des draw packets en JSON.
- Le fichier source ne dépasse pas 8 Mio et l'aperçu rasterisé tient dans un
  carré de 2048 pixels de côté sans dépasser 4194304 pixels.
- L'import refuse tout identifiant ou SVG invalide et ne remplace jamais un
  asset existant.
- La source est publiée avant le document JSON atomique ; le document est le
  marqueur rendant le vecteur découvrable par les outils et le runtime.
- L'aperçu OpenGL utilise exactement les pixels validés lors de l'import.
- Le renderer natif et le personnalisateur représentés ici sont les composants
  cibles de l’étape suivante ; le document natif est déjà persistant mais ne
  possède désormais des draw packets headless déterministes pour la géométrie
  native. Les chemins auto-intersectants sont rejetés par le validateur. Le
  cache headless est indexé par le JSON canonique du document et la tolérance
  de courbe, ce qui invalide automatiquement une version modifiée ; l’import
  opaque reste fonctionnel. Chaque draw packet porte soit une couleur solide,
  soit la référence texture et les UV transformées du fill image, ainsi que la
  même triangulation de silhouette ; aucun atlas ni bitmap dérivé n’est créé.
  Les sommets sont exprimés dans l’espace monde après application du transform
  du nœud et de ses parents dans l’ordre stable de la hiérarchie.
  Le backend OpenGL 3 compile ses shaders et possède ses buffers via les
  fonctions chargées par SDL ; il refuse explicitement les fills image tant
  qu’aucun résolveur de textures n’est fourni, mais dessine les contours
  ouverts et fermés avec le même packet.
