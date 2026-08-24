# C4 Component — import raster PNG

```mermaid
C4Component
    title Vertex Loom — import persistant et aperçu PNG
    Container_Boundary(render, "fabric_render") {
        Component(loader, "PNG loader", "C++20 / SDL2_image", "Décode un fichier PNG en pixels RGBA8 bornés")
        Component(image, "RasterImage", "C++20", "Largeur, hauteur et pixels sans type SDL ou OpenGL public")
    }
    Container_Boundary(asset, "Asset Studio") {
        Component(import_ui, "PNG import dialog", "Dear ImGui", "Saisit la source, l'identifiant et le nom puis présente les diagnostics")
        Component(gpu_preview, "OpenGL preview texture", "OpenGL", "Téléverse temporairement les pixels pour l'aperçu")
    }
    Container_Boundary(editor, "fabric_editor") {
        Component(importer, "Texture importer", "C++20", "Orchestre validation, copie et publication du document TextureAsset")
    }
    Container(project, "fabric_project", "C++20 / JSON", "Valide et publie le contrat TextureAsset")
    ContainerDb(files, "Local Files", "PNG", "Source raster choisie par l'utilisateur")
    Rel(import_ui, importer, "Demande l'import")
    Rel(importer, loader, "Valide et décode")
    Rel(importer, project, "Publie source et document")
    Rel(loader, files, "Lit")
    Rel(loader, image, "Produit")
    Rel(import_ui, gpu_preview, "Téléverse")
    Rel(image, gpu_preview, "Fournit les pixels")
```

## Contract

- Le chargeur accepte uniquement l'extension `.png`, sans tenir compte de la
  casse, puis laisse le décodeur vérifier le contenu réel.
- La sortie publique est RGBA8, contiguë par ligne et indépendante de SDL.
- L'en-tête IHDR est contrôlé avant décodage. Une image doit mesurer entre 1 et
  16384 pixels sur chaque axe et ne pas dépasser 67108864 pixels au total.
- L'import refuse tout identifiant ou PNG invalide et ne remplace jamais un
  asset existant.
- La source est copiée vers un fichier temporaire adjacent. Après validation,
  la source finale est publiée avant le document JSON atomique ; le document
  est l'unique marqueur rendant l'asset découvrable par le runtime.
- L'aperçu GPU utilise exactement les pixels validés lors de l'import.
