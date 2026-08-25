# C4 Component — sources et authoring vectoriels

```mermaid
C4Component
    title Vertex Loom — migration de l’import SVG vers l’authoring natif
    Container_Boundary(render, "fabric_render") {
        Component(loader, "SVG preview loader", "C++20 / SDL2_image / NanoSVG", "Valide la taille du fichier et rasterise un aperçu RGBA8 borné")
        Component(image, "RasterImage", "C++20", "Aperçu possédé par le moteur sans type SDL ou OpenGL public")
        Component(geometry, "Native vector renderer", "C++20 / OpenGL", "Aplatit, triangule et produit des draw packets déterministes")
    }
    Container_Boundary(asset, "Asset Studio") {
        Component(import_ui, "SVG import dialog", "Dear ImGui", "Saisit la source, l'identifiant et le nom puis présente les diagnostics")
        Component(customizer, "Vector customizer", "Dear ImGui", "Crée et édite formes, fills, contours, clips et transforms")
        Component(gpu_preview, "OpenGL preview texture", "OpenGL", "Téléverse temporairement l'aperçu rasterisé")
    }
    Container_Boundary(editor, "fabric_editor") {
        Component(importer, "Vector importer", "C++20", "Orchestre validation, copie et publication du document VectorAsset")
    }
    Container(project, "fabric_project", "C++20 / JSON", "Valide et publie le contrat VectorAsset")
    ContainerDb(files, "Local Files", "SVG", "Source vectorielle choisie par l'utilisateur")
    Rel(import_ui, importer, "Demande l'import")
    Rel(importer, loader, "Valide et rasterise")
    Rel(importer, project, "Publie source et document")
    Rel(loader, files, "Lit")
    Rel(loader, image, "Produit")
    Rel(import_ui, gpu_preview, "Téléverse")
    Rel(image, gpu_preview, "Fournit les pixels")
    Rel(customizer, geometry, "Prévisualise VectorAsset v2 natif")
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
  éléments non pris en charge avant publication.
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
  native ; l’import opaque reste fonctionnel.
