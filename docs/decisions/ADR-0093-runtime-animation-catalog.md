# ADR-0093 — Catalogue d’animations du Preview Runtime

## Statut

Accepté.

## Contexte

Le contrat `AnimationClip v1` et son éditeur existent, mais le Preview Runtime
ne chargeait encore aucun document d’animation. La liaison d’un clip à une
entité de scène n’est pas définie par `MapDocument v1` ou `SceneDocument v1`.

## Décision

Avant `SDL_Init` et toute création de fenêtre, `PreviewRuntime` énumère les
fichiers `assets/animations/*.animation.json` du projet, les charge avec un
chemin relatif et les valide via le parseur du projet. Les clips sont indexés
par `ResourceId` et exposés à l’évaluation headless par identifiant et instant.

Un clip invalide fait échouer le chargement du runtime. Un identifiant absent
retourne une absence d’évaluation plutôt qu’un état synthétique. Cette tranche
ne crée pas de champ de binding clip→entité : cette relation sera ajoutée avec
le contrat de scène ou d’entité qui la porte explicitement.

## Conséquences

- Les animations publiées sont contrôlées avant l’ouverture de la fenêtre.
- Les tests headless peuvent vérifier le chargement et l’interpolation runtime.
- Les documents restent les sources d’autorité ; aucune conversion persistante
  n’est produite.
- Le runtime ne joue pas encore automatiquement une animation sur un nœud.
