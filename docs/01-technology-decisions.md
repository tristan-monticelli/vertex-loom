# Technology decisions

| Topic | Choice | Rationale | ADR |
| --- | --- | --- | --- |
| Language | C++20 | Contrôle du runtime, du rendu et de la mémoire. | ADR-0004 |
| Runtime | CMake + SDL2 + OpenGL | Fenêtre, input, audio et rendu desktop portable. | ADR-0004 |
| Frontend | Asset Studio + Map Studio, C++/Dear ImGui | Deux flux spécialisés partageant le cœur. | ADR-0005 |
| Editor shell | SDL2 2.32.10 + OpenGL + Dear ImGui 1.92.9 | Fenêtre et UI d'outil portables avec sources épinglées. | ADR-0012 |
| PNG decoding | SDL2_image 2.8.12 | Décodeur portable, borné derrière un contrat RGBA8 interne. | ADR-0014 |
| Texture import | JSON versionné + publication locale sans remplacement | Projet portable, source validée et contrat commun aux outils et au runtime. | ADR-0015 |
| SVG import lié | SDL2_image 2.8.12 / NanoSVG intégré | Source externe conservée et bornée ; le SVG n’est pas le format d’édition natif. | ADR-0016, ADR-0022 |
| Vector authoring | `VectorAsset v2` natif | Géométrie, fills, contours, clips et propriétés animables indépendants du SVG. | ADR-0022, ADR-0023 |
| Creation flows | Prompts typés `Create / Import / Add existing` | Chaque opération expose ses champs, validations et effets sans état partagé ambigu. | ADR-0024 |
| Sprite compatibility | Pipeline Aseprite/atlas gelé | Lecture héritée conservée, sans dépendance des futurs éditeurs ou runtimes. | ADR-0021, ADR-0022 |
| New C++ tests | Catch2 3.15.3 | Nouvelles suites structurées sans réécriture immédiate des tests historiques. | ADR-0017 |
| Native dialogs | Native File Dialog Extended 1.3.0 | Sélecteurs Cocoa, Win32 et GTK derrière une API C portable. | ADR-0017 |
| Common contracts | Types C++ partagés + ProjectManifest v2 | Unités, transforms, en-têtes et références identiques dans tous les modules. | ADR-0018 |
| Resource graph | ResourceRegistry déterministe | Résolution headless et refus des doublons, absences et cycles. | ADR-0019 |
| Editor history | CommandStack réversible avec fusion | Undo/redo testable sans UI et point dirty explicite. | ADR-0020 |
| Recovery | Autosave atomique en miroir | Récupération locale validée et jamais appliquée automatiquement. | ADR-0020 |
| Backend | none | Produit local hors ligne. | — |
| Storage | JSON + assets sur disque | Métadonnées diffables et ressources lourdes séparées. | ADR-0006 |
| Deployment | archives desktop | Distribution locale au début. | — |
| Observability | logs locaux, overlay debug, métriques frame | Diagnostic sans télémétrie distante. | ADR-0004 |
