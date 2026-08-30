# ADR-0055 — Table d’actions d’entrée

## Statut

Accepté.

## Décision

Les actions de gameplay sont identifiées par des chaînes stables et peuvent
recevoir plusieurs bindings clavier ou manette. `InputActionMap` conserve les
états `held`, `pressed` et `released` par frame, ignore les répétitions clavier
pour `pressed` et reste indépendant de SDL afin d’être testé headless.

`configure` remplace la table complète uniquement si toutes les définitions et
bindings sont valides ; en cas d’échec, la table et ses états précédents restent
inchangés. Preview Runtime accepte cette table via
`PreviewRuntimeOptions::input_actions` et conserve ses bindings par défaut si
aucune table n’est fournie.
