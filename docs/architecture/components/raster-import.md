# C4 Component — aperçu raster PNG

```mermaid
C4Component
    title Vertex Loom — chargement et aperçu PNG
    Container_Boundary(render, "fabric_render") {
        Component(loader, "PNG loader", "C++20 / SDL2_image", "Décode un fichier PNG en pixels RGBA8 bornés")
        Component(image, "RasterImage", "C++20", "Largeur, hauteur et pixels sans type SDL ou OpenGL public")
    }
    Container_Boundary(asset, "Asset Studio") {
        Component(import_ui, "PNG preview dialog", "Dear ImGui", "Sélectionne un chemin et présente les diagnostics")
        Component(gpu_preview, "OpenGL preview texture", "OpenGL", "Téléverse temporairement les pixels pour l'aperçu")
    }
    ContainerDb(files, "Local Files", "PNG", "Source raster choisie par l'utilisateur")
    Rel(import_ui, loader, "Demande le décodage")
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
- L'aperçu GPU reste temporaire : la copie dans le projet et le document
  `TextureAsset` constituent l'incrément suivant.
