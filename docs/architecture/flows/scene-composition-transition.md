# Flux — Composition multi-map et transition de scène

```mermaid
sequenceDiagram
    participant Game as game_runtime
    participant Session as SceneRuntimeSession
    participant Project as fabric_project
    participant Preview as PreviewRuntime

    Game->>Session: load(sceneId)
    Session->>Project: charger SceneDocument et toutes ses maps
    Project-->>Session: map composée avec namespaces de montage
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
