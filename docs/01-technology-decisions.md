# Technology decisions

| Topic | Choice | Rationale | ADR |
| --- | --- | --- | --- |
| Language | C++20 | Contrôle du runtime, du rendu et de la mémoire. | ADR-0004 |
| Runtime | CMake + SDL2 + OpenGL | Fenêtre, input, audio et rendu desktop portable. | ADR-0004 |
| Frontend | Asset Studio + Map Studio, C++/Dear ImGui | Deux flux spécialisés partageant le cœur. | ADR-0005 |
| Editor shell | SDL2 2.32.10 + OpenGL + Dear ImGui 1.92.9 | Fenêtre et UI d'outil portables avec sources épinglées. | ADR-0012 |
| Backend | none | Produit local hors ligne. | — |
| Storage | JSON + assets sur disque | Métadonnées diffables et ressources lourdes séparées. | ADR-0006 |
| Deployment | archives desktop | Distribution locale au début. | — |
| Observability | logs locaux, overlay debug, métriques frame | Diagnostic sans télémétrie distante. | ADR-0004 |
