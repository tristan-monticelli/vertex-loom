# Project brief

## Summary

- Name: Vertex Loom
- Problem solved: moteur 2D natif et ateliers de création pour un jeu de
  plateformes original à rendu vectoriel, inspiré d'une esthétique textile et
  diorama.
- Users: développeur moteur, artiste technique, level designer puis créateur
  de maps utilisant les mêmes capacités d'authoring depuis le jeu.
- In scope: runtime 2D, rendu avec profondeur, artworks vectoriels natifs,
  images recadrées sans altération de leur source, compositions par calques,
  composants visuels paramétriques, chemins texturés, personnalisateur intégré,
  keyframes génériques, mécaniques physiques composables, entités, Asset Studio,
  Map Studio et publication portable de maps vers un catalogue intégré au jeu.
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
- Une texture importée reste intacte ; son recadrage et ses overlays sont
  éditables, réversibles et reproduits à l'identique par les studios et le
  runtime.
- Une fonctionnalité jouable n'est livrée qu'après authoring, preview,
  sauvegarde et validation dans le studio qui la possède.
- Une map peut être composée, validée, sauvegardée et exécutée dans le runtime.
- Une map publiée résout ses dépendances dans un paquet portable et constitue
  l'unité de contenu proposée au catalogue du jeu.
- Les deux éditeurs et le runtime lisent le même format de projet versionné.
- Collisions, sérialisation et validation des ressources sont testées automatiquement.
