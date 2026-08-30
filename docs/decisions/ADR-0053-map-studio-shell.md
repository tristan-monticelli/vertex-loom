# ADR-0053 — Coquille interactive de Map Studio

## Statut

Accepté.

## Décision

Map Studio utilise la même pile SDL2/OpenGL/Dear ImGui que Asset Studio, mais
son état d’édition reste porté par `MapSession`. Le premier écran ouvre un
projet et une map par arguments, inspecte les calques, instances, collisions,
triggers et événements, et expose sauvegarde, undo/redo et déclaration
d’événements.

Les opérations de composition supplémentaires seront ajoutées comme commandes
à `MapSession`; l’interface ne modifie jamais directement le JSON.
