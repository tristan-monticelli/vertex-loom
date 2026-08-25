# ADR-0101 — Document persistant de bindings d’entrée

## Statut

Accepté.

## Décision

Les bindings d’actions sont persistés dans un `InputDocument v1` sous
`assets/input/<id>.input.json`. Le document utilise le `DocumentHeader` commun
avec le type `input`, et contient une liste ordonnée d’actions identifiées par
`ResourceId`.

Chaque action peut déclarer plusieurs couples `device`/`code`. Les périphériques
supportés sont `keyboard` et `gamepad`; les codes doivent être des entiers
supérieurs ou égaux à zéro. Les identifiants invalides, actions dupliquées et
bindings dupliqués sont refusés avant sauvegarde ou chargement.

Le Preview Runtime charge explicitement le document demandé par `--input <id>`.
Sans cette option, il tente `default`; l’absence de ce fichier conserve les
bindings SDL historiques pour compatibilité. Une configuration chargée doit
contenir `move_left`, `move_right` et `jump` lorsque le contrôleur de personnage
est activé.

## Conséquences

- Les éditeurs et le runtime partagent un format lisible et atomiquement
  sauvegardé.
- Le runtime n’a plus besoin de modifier le code pour sélectionner une table.
- Le document ne contient pas encore de profils, de zones mortes ni de
  séquences d’input ; ces extensions devront augmenter le schéma.
