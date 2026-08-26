# Flux — signal vers action de comportement

```mermaid
sequenceDiagram
    participant Source as Input / IA / Map / Trigger / Timer
    participant Runtime as Preview Runtime
    participant Eval as BehaviorEvaluator(instance)
    participant Graph as BehaviorGraph v1
    participant World as Animation / Physics / Mechanics / Transformation

    Source->>Runtime: signal(source, semanticId, typed payload)
    Runtime->>Eval: evaluate(signal, fixedStep)
    Eval->>Graph: resolve source and ordered connections
    Graph-->>Eval: validated nodes and ports
    Eval->>Eval: condition / branch / sequence / state / timing
    Eval-->>Runtime: ordered typed actions + bounded trace
    Runtime->>World: apply complete action batch
    World-->>Runtime: state valid for the same frame
```

Une action sémantique possède le même chemin quel que soit son producteur. Les
actions sont ordonnées par le graphe et appliquées comme un lot pour éviter un
état intermédiaire incohérent, notamment lors d'une transformation.
