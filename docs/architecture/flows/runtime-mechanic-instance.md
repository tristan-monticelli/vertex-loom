# Flux — Instance mécanique publiée dans Preview Runtime

```mermaid
sequenceDiagram
    participant Package as Projet ou paquet publié
    participant Runtime as Preview Runtime
    participant Compiler as fabric_physics compiler
    participant Simulation as MechanicSimulation
    participant Renderer as fabric_render

    Package->>Runtime: MapDocument + prefab + MechanicGraph
    Runtime->>Compiler: graphe + overrides + transform d'instance
    Compiler-->>Runtime: MechanicPlan validé
    Runtime->>Simulation: load(plan), play()
    loop Chaque pas fixe 1/60 s
        Runtime->>Simulation: update(1/60 s)
        Simulation-->>Runtime: poses courantes des corps
    end
    Runtime->>Renderer: paquets de l'entité transformés par la pose relative
```

Le chargement est atomique du point de vue du runtime : une ressource absente,
un plan invalide, un monde Box2D impossible à créer ou plusieurs corps liés à
la même racine visuelle refusent la map avant toute fenêtre SDL. Une instance
sans corps lié reste visuelle et statique.
