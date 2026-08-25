# C4 Container

```mermaid
C4Container
    title Vertex Loom — conteneurs
    Person(creator, "Créateur", "Développe et compose le jeu")
    System_Boundary(fabric, "Vertex Loom") {
        Container(runtime, "Game Runtime", "C++20 / CMake", "Point d'entrée du runtime ; SDL2, OpenGL et la boucle de jeu viendront dans les slices suivants")
        Container(asset, "Asset Studio", "C++20 / SDL2 / OpenGL / Dear ImGui", "Crée et personnalise des artworks vectoriels, fills, entités et animations")
        Container(map, "Map Studio", "C++20 / CMake", "Point d'entrée de l'éditeur de maps ; UI Dear ImGui à venir")
        Container(core, "fabric_core", "C++20 static library", "Vec2, Color, Rect, Transform, identifiants de ressources et journaux structurés locaux")
        Container(projectlib, "fabric_project", "C++20 / nlohmann-json", "Manifest, textures, documents vectoriels et graphe de ressources")
        Container(editorlib, "fabric_editor", "C++20 static library", "Session projet, prompts typés, historique réversible, autosave et orchestration d’authoring")
        Container(renderlib, "fabric_render", "C++20 / SDL2_image / OpenGL", "Décodage PNG/SVG, géométrie et compositing vectoriels")
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
    Rel(asset, renderlib, "Prévisualise les draw packets vectoriels")
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
La tranche actuellement compilée décode les aperçus PNG/SVG. Le renderer cible
consomme `VectorAsset v2` et le modèle forme/fill/contour/clip d’ADR-0023. Le
pipeline sprite a été retiré par ADR-0025.
