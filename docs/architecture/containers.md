# C4 Container

```mermaid
C4Container
    title Vertex Loom — conteneurs
    Person(creator, "Créateur", "Développe et compose le jeu")
    System_Boundary(fabric, "Vertex Loom") {
        Container(runtime, "Game Runtime", "C++20 / CMake", "Point d'entrée du runtime ; SDL2, OpenGL et la boucle de jeu viendront dans les slices suivants")
        Container(asset, "Asset Studio", "C++20 / CMake", "Point d'entrée de l'éditeur d'assets ; UI Dear ImGui à venir")
        Container(map, "Map Studio", "C++20 / CMake", "Point d'entrée de l'éditeur de maps ; UI Dear ImGui à venir")
        Container(core, "fabric_core", "C++20 static library", "Types partagés, identifiants de ressources et journaux structurés locaux")
        Container(projectlib, "fabric_project", "C++20 / nlohmann-json", "Manifest, sérialisation et validation du format projet")
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
    Rel(map, projectlib, "Lit et écrit")
    Rel(runtime, projectlib, "Charge")
    Rel(projectcli, projectlib, "Utilise")
    Rel(projectlib, core, "Utilise les types communs")
    Rel(projectlib, project, "Valide, lit et écrit")
```

## Scope and assumptions

Les trois applications sont des exécutables desktop. Shared Core est une
bibliothèque sans état distant. Le premier squelette CMake expose les cibles
`fabric_core`, `asset_studio`, `map_studio`, `game_runtime` et
`fabric_core_smoke`. Le composant `fabric_project` et son validateur headless
constituent le premier contrat de données partagé. Les dépendances graphiques
seront ajoutées avec leurs contrats et ADR lors de leur premier usage réel.
