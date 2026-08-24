# C4 Component — observabilité locale

```mermaid
C4Component
    title Fabric Engine — observabilité locale
    Container_Boundary(core, "fabric_core") {
        Component(logger, "JsonLineLogger", "C++20", "Sérialise des événements structurés vers un flux local de manière thread-safe")
    }
    Container(cli, "fabric_project_validate", "C++20 CLI", "Produit des diagnostics humains ou JSON Lines")
    System_Ext(local_stream, "Flux local", "Terminal, fichier ou capture de test")
    Rel(cli, logger, "Écrit les résultats avec --json")
    Rel(logger, local_stream, "Émet une ligne JSON par événement")
```

## Contract

Chaque ligne contient `timestampMs`, `level`, `category` et `message`. Les
champs contextuels sont placés dans l'objet `fields`. Le logger échappe les
caractères de contrôle et n'effectue aucun accès réseau.
