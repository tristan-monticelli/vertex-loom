# C4 Component — import vectoriel SVG

```mermaid
C4Component
    title Vertex Loom — import persistant et aperçu SVG
    Container_Boundary(render, "fabric_render") {
        Component(loader, "SVG preview loader", "C++20 / SDL2_image / NanoSVG", "Valide la taille du fichier et rasterise un aperçu RGBA8 borné")
        Component(image, "RasterImage", "C++20", "Aperçu possédé par le moteur sans type SDL ou OpenGL public")
    }
    Container_Boundary(asset, "Asset Studio") {
        Component(import_ui, "SVG import dialog", "Dear ImGui", "Saisit la source, l'identifiant et le nom puis présente les diagnostics")
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
```

## Contract

- L'import accepte uniquement l'extension `.svg`, sans tenir compte de la
  casse, puis laisse NanoSVG vérifier le contenu.
- La source SVG originale est copiée sans conversion et reste la ressource
  chargée par les futurs renderers vectoriels.
- Le fichier source ne dépasse pas 8 Mio et l'aperçu rasterisé tient dans un
  carré de 2048 pixels de côté sans dépasser 4194304 pixels.
- L'import refuse tout identifiant ou SVG invalide et ne remplace jamais un
  asset existant.
- La source est publiée avant le document JSON atomique ; le document est le
  marqueur rendant le vecteur découvrable par les outils et le runtime.
- L'aperçu OpenGL utilise exactement les pixels validés lors de l'import.
