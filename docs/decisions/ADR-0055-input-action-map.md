# ADR-0055 — Table d’actions d’entrée

## Statut

Accepté.

## Décision

Les actions de gameplay sont identifiées par des chaînes stables et peuvent
recevoir plusieurs bindings clavier ou manette. `InputActionMap` conserve les
états `held`, `pressed` et `released` par frame, ignore les répétitions clavier
pour `pressed` et reste indépendant de SDL afin d’être testé headless.
