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

`game_runtime --save-slot <slot>` charge le slot avant l’exécution et refuse une
sauvegarde existante invalide. Lorsqu'un slot existe, sa scène et ses propriétés
sont l'état de reprise autoritaire ; une éventuelle option `--scene` ne les
remplace pas. Lorsqu'il est absent, `--scene <id>` est requis pour amorcer la
première sauvegarde.

Le chemin explicite déjà injectable dans `ProgressStore` est exposé par
`game_runtime --save-path <file>` pour les tests d'intégration et les outils
headless. `--save-path` et `--save-slot` sont mutuellement exclusifs et suivent
exactement la même politique de reprise.

Après une exécution réussie, le runtime remplace uniquement le build et la scène
active de l'état chargé, puis publie le document atomiquement. Les propriétés
inconnues restent byte-for-byte équivalentes après parse et sérialisation tant
qu'aucun système gameplay ne les modifie explicitement. Une erreur avant ou
pendant l'exécution ne remplace jamais le slot.
