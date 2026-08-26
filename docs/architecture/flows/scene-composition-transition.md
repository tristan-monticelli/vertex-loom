# Flux — Composition multi-map et transition de scène

```mermaid
sequenceDiagram
    participant Game as game_runtime
    participant Session as SceneRuntimeSession
    participant Project as fabric_project
    participant Preview as PreviewRuntime

    opt campagne portable
        Game->>Project: ouvrir scene-package.json
        Project-->>Game: rootScene + fermeture transitive
    end

    Game->>Session: load(project, sceneId) ou load_package(package)
    Session->>Project: charger SceneDocument et toutes ses maps
    Project-->>Session: compose_scene_maps + points d'entrée
    Session-->>Game: scène + map composée
    Game->>Preview: scène + spawn optionnel
    Preview-->>Game: événement gameplay
    Game->>Session: transition_for_event(eventId)
    Session->>Project: préparer scène cible + maps + entryPoint
    alt cible complète et point unique
        Project-->>Session: nouvelle scène, map composée, spawn
        Session-->>Game: transition atomique validée
    else erreur
        Project-->>Session: diagnostics
        Session-->>Game: scène courante conservée
    end
```

La map composée prend l'identifiant de la scène et reste éphémère. La session
expose le `SceneEntryPoint` résolu ; `game_runtime` transmet sa position dans
`PreviewRuntimeOptions.character_spawn` au chargement suivant.

La publication headless utilise
`fabric_map_package_export --scene <scene-id> <projet> <destination>`. Le
manifeste racine, les scènes atteignables, toutes leurs maps et les dépendances
de chaque map sont copiés avant l'écriture atomique de `scene-package.json`.
`game_runtime --package` détecte ensuite le type de paquet et conserve la scène
active dans la boucle sans dépendre du projet source.
