# C4 Component — observabilité locale

```mermaid
C4Component
    title Vertex Loom — observabilité locale
    Container_Boundary(core, "fabric_core") {
        Component(logger, "JsonLineLogger", "C++20", "Sérialise des événements structurés corrélés par session et ressource vers un flux local de manière thread-safe")
        Component(trace, "TraceContext", "C++20", "Porte sessionId et resourceId entre Studio, preview et runtime publié")
    }
    Container(studio, "Studios", "C++20 desktop", "Crée une session de trace et la transmet aux previews")
    Container(runtime, "PreviewRuntime", "C++20 / SDL2", "Émet chargement, erreurs et résumé runtime")
    Container(cli, "fabric_project_validate", "C++20 CLI", "Produit des diagnostics humains ou JSON Lines")
    System_Ext(local_stream, "Flux local", "Terminal, fichier ou capture de test")
    Rel(cli, logger, "Écrit les résultats avec --json")
    Rel(studio, trace, "Crée et propage")
    Rel(runtime, logger, "Écrit avec le contexte reçu")
    Rel(trace, logger, "Enrichit chaque ligne")
    Rel(logger, local_stream, "Émet une ligne JSON par événement")
```

## Contract

Chaque ligne contient `timestampMs`, `level`, `category` et `message`. Le bloc
`context` optionnel porte `sessionId` et `resourceId`; les autres champs sont
placés dans `fields`. Le logger échappe les caractères de contrôle et
n'effectue aucun accès réseau.
