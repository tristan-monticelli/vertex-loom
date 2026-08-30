# ADR-0012 — Coquille desktop d'Asset Studio

- Status: accepted
- Date: 2026-08-24

## Context

La Phase 2 doit commencer par un atelier réellement ouvrable sur macOS,
Windows et Linux, sans coupler l'état projet à la boucle graphique. Les imports
et le renderer d'assets ont besoin d'un hôte stable avant leur intégration.

## Decision

Construire la première tranche visuelle avec SDL2 `2.32.10`, OpenGL fourni par
la plateforme et Dear ImGui `1.92.9`, récupérés ou localisés par CMake. Épingler
les deux sources tierces à leurs tags de release.

Créer `fabric_editor` pour l'état de session indépendant de l'interface. Une
session n'adopte que le manifeste renvoyé par `load_project`, qui effectue sa
lecture et la validation du dossier dans la même opération ; un échec conserve
le projet valide déjà ouvert et publie les erreurs du nouvel essai.

## Alternatives

SDL3 modifierait le contrat explicitement demandé pour le premier renderer.
Une interface propre à chaque système compliquerait immédiatement la matrice
multiplateforme. Placer tout l'état dans `main.cpp` empêcherait les tests
headless et la réutilisation par Map Studio.

## Consequences

Le build des éditeurs télécharge et compile SDL2 et Dear ImGui au premier
passage. `FABRIC_BUILD_EDITORS=OFF` conserve un build headless sans ces
dépendances. Le premier écran ouvre et inspecte un projet, mais l'import et le
rendu d'assets restent les incréments suivants.
