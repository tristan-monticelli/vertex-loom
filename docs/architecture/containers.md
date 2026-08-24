# C4 Container

```mermaid
C4Container
    title Vertex Loom — conteneurs
    Person(creator, "Créateur", "Développe et compose le jeu")
    System_Boundary(fabric, "Vertex Loom") {
        Container(runtime, "Game Runtime", "C++20 / CMake", "Point d'entrée du runtime ; SDL2, OpenGL et la boucle de jeu viendront dans les slices suivants")
        Container(asset, "Asset Studio", "C++20 / SDL2 / OpenGL / Dear ImGui", "Ouvre et inspecte un projet dans un atelier desktop natif")
        Container(map, "Map Studio", "C++20 / CMake", "Point d'entrée de l'éditeur de maps ; UI Dear ImGui à venir")
        Container(core, "fabric_core", "C++20 static library", "Vec2, Color, Rect, Transform, identifiants de ressources et journaux structurés locaux")
        Container(projectlib, "fabric_project", "C++20 / nlohmann-json / zlib", "Manifest, documents d'assets, sérialisation et validation du format projet et des atlas PNG")
        Container(editorlib, "fabric_editor", "C++20 static library", "Session projet, historique réversible, autosave et orchestration des imports")
        Container(renderlib, "fabric_render", "C++20 / SDL2_image / zlib", "Décodage PNG/SVG, lecture Aseprite et génération déterministe d’atlas indépendants du GPU")
        Container(projectcli, "fabric_project_validate", "C++20 CLI", "Valide un dossier projet sans interface graphique")
        ContainerDb(project, "Project Files", "JSON + assets", "Projet versionné et ressources sur disque")
    }
    Rel(creator, asset, "Crée et prévisualise")
    Rel(creator, map, "Compose et teste")
    Rel(creator, runtime, "Lance et joue")
    Rel(asset, core, "Utilise")
    Rel(map, core, "Utilise")
    Rel(runtime, core, "Utilise")
    Rel(asset, projectlib, "Lit et écrit")
    Rel(asset, editorlib, "Pilote une session")
    Rel(asset, renderlib, "Charge les aperçus et génère les atlas")
    Rel(map, projectlib, "Lit et écrit")
    Rel(runtime, projectlib, "Charge")
    Rel(projectcli, projectlib, "Utilise")
    Rel(projectlib, core, "Utilise les types communs")
    Rel(editorlib, projectlib, "Valide et charge")
    Rel(editorlib, renderlib, "Valide les sources raster et vectorielles")
    Rel(projectlib, project, "Valide, lit et écrit")
```

## Scope and assumptions

Les trois applications sont des exécutables desktop. Shared Core est une
bibliothèque sans état distant. Le premier squelette CMake expose les cibles
`fabric_core`, `asset_studio`, `map_studio`, `game_runtime` et
`fabric_core_smoke`. Le composant `fabric_project` et son validateur headless
constituent le premier contrat de données partagé. Asset Studio utilise
`fabric_editor` pour la session, une coquille SDL2/OpenGL/Dear ImGui et le
premier composant `fabric_render` pour décoder les aperçus PNG et SVG en RGBA8.
`fabric_render` lit aussi les sources Aseprite sans exécutable externe et
produit les atlas PNG déterministes partagés avec le runtime.
