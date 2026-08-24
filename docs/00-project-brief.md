# Project brief

## Summary

- Name: Vertex Loom
- Problem solved: moteur 2D natif et ateliers de création pour un jeu de
  plateformes original à rendu vectoriel, inspiré d'une esthétique textile et
  diorama.
- Users: développeur moteur, artiste technique et level designer.
- In scope: runtime 2D, rendu avec profondeur, artworks vectoriels natifs,
  formes contenant des fills image ou matériaux, personnalisateur intégré,
  keyframes génériques, entités, physique, Asset Studio et Map Studio.
- Out of scope: assets ou personnages de Nintendo, backend en ligne, consoles et outil 3D généraliste.

## Constraints

- Performance: viser 60 FPS sur desktop courant, avec boucle de jeu déterministe.
- Security and compliance: fonctionnement local ; valider les chemins et formats d'assets avant import.
- Availability and reliability: hors ligne ; sauvegarde atomique des projets.
- Budget, timeline, or platform: macOS, Windows et Linux ; priorité à la qualité d'Asset Studio et Map Studio avant le runtime jouable.

## Decisions to make

- Language and runtime: C++20, CMake, SDL2 et OpenGL.
- Frontend: Asset Studio et Map Studio, applications desktop natives.
- Backend: aucun.
- Storage: JSON pour métadonnées et assets séparés sur disque.
- Tests: CTest pour le cœur et tests Node conservés pour la gouvernance.
- Deployment: binaires desktop et archives de projet.
- Observability: logs locaux, overlay de debug et métriques de frame.

## Success criteria

- Le runtime charge un projet et maintient 60 FPS sur une scène de référence.
- Une entité peut être créée, transformée, animée, sauvegardée puis rechargée.
- Un artwork peut combiner un contour vectoriel et une image remplissante,
  puis animer leurs transforms indépendamment sans spritesheet.
- Une map peut être composée, validée, sauvegardée et exécutée dans le runtime.
- Les deux éditeurs et le runtime lisent le même format de projet versionné.
- Collisions, sérialisation et validation des ressources sont testées automatiquement.
