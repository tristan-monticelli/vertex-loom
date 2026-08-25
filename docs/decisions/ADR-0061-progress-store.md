# ADR-0061 — Stockage utilisateur de progression

## Décision

`ProgressStore` résout les slots avec `SDL_GetPrefPath` pour séparer les
sauvegardes utilisateur du projet d’authoring. Un chemin explicite reste
injectable pour les tests et les outils headless.

Le nom de slot est limité à un nom de fichier sans séparateur. Les documents
sont lus et écrits via `ProgressSave v1`; toute validation échouée empêche la
lecture applicative ou le remplacement atomique.

## Conséquences

Le runtime jouable peut choisir plusieurs slots portables sans dépendre du
répertoire du projet. Aucun fichier utilisateur n’est créé par une simple
lecture d’un slot absent.

`game_runtime --scene <id> --save-slot <slot>` charge le slot avant l’exécution
et refuse une sauvegarde existante invalide ; après une exécution réussie, il
publie atomiquement la scène active et le build runtime dans ce slot.
