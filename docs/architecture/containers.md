# C4 Container

```mermaid
C4Container
    title Vertex Loom — conteneurs
    Person(creator, "Créateur", "Développe et compose le jeu")
    System_Boundary(fabric, "Vertex Loom") {
        Container(runtime, "Game Runtime", "C++20 / SDL2 / OpenGL", "Valide un projet avant fenêtre, résout une scène ou map avec transitions atomiques, traduit les événements SDL vers le CharacterController, interpole Camera2D, culling par chunks et culling géométrique, route les entrées de zones vers les événements gameplay, lit optionnellement un ReplayDocument par frame, vérifie les checkpoints quantifiés, persiste ProgressSave via SDL_GetPrefPath, mixe et joue les WAV PCM, exécute Box2D à pas fixe et rend le Preview Runtime")
        Container(asset, "Asset Studio", "C++20 / SDL2 / OpenGL / Dear ImGui", "Crée et personnalise des artworks vectoriels, fills, entités et animations")
        Container(map, "Map Studio", "C++20 / SDL2 / Dear ImGui", "Édite maps, calque actif, placement, duplication et sélection rectangulaire d’instances, événements et payloads, triggers et collisions via MapSession, avec inspecteurs de points et triggers, annotations d’événements, canvas 2D, grille, caméra pan/zoom, cadrage automatique, sélection et poignées de transformation réversibles")
        Container(physics, "fabric_physics", "C++20 / Box2D v3.1.1", "Possède le monde physique et exécute les pas fixes validés")
        Container(core, "fabric_core", "C++20 static library", "Vec2, Color, Rect, Transform, identifiants de ressources et journaux structurés locaux")
        Container(projectlib, "fabric_project", "C++20 / nlohmann-json", "Manifest, textures, documents vectoriels et graphe de ressources")
        Container(editorlib, "fabric_editor", "C++20 static library", "Session projet, prompts typés, historique réversible, autosave et orchestration d’authoring")
        Container(renderlib, "fabric_render", "C++20 / SDL2_image / OpenGL", "Décodage PNG/SVG, géométrie, compositing vectoriels et batching stable des draw packets")
        Container(projectcli, "fabric_project_validate", "C++20 CLI", "Valide un dossier projet sans interface graphique")
        Container(renderbench, "fabric_render_benchmark", "C++20 / SDL2 / OpenGL", "Mesure le rendu d’une scène synthétique dense : packets, draw calls, triangles et p95")
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
    Rel(map, physics, "Construit et inspecte la physique")
    Rel(physics, runtime, "Fournit le monde physique")
    Rel(runtime, projectlib, "Charge")
    Rel(runtime, renderlib, "Rend les draw packets")
    Rel(projectcli, projectlib, "Utilise")
    Rel(renderbench, renderlib, "Mesure")
    Rel(projectlib, core, "Utilise les types communs")
    Rel(editorlib, projectlib, "Valide et charge")
    Rel(editorlib, renderlib, "Valide les sources raster et vectorielles")
    Rel(renderlib, projectlib, "Construit les draw packets VectorAsset v2")
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
