# ADR-0060 — Sauvegarde de progression v1

## Décision

`ProgressSave v1` est un document JSON indépendant du projet d’authoring.
Il contient le build, la scène active et une map de propriétés typées limitée
à booléen, entier, réel, texte, `Vec2` et référence de ressource.

Le fichier est validé puis remplacé atomiquement. Le runtime lui fournit le
chemin issu de `SDL_GetPrefPath`; le contrat reste testable sans SDL et ne
réutilise pas les fichiers du projet source.

## Conséquences

Les propriétés inconnues peuvent être conservées sans langage de script et les
valeurs non finies ou références invalides sont refusées avant écriture.
