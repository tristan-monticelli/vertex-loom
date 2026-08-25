# ADR-0056 — Contrôleur de personnage hybride

## Statut

Accepté.

## Décision

Le contrôleur de personnage conserve les actions de locomotion dans
`InputActionMap` et délègue le déplacement à un corps dynamique de
`PhysicsWorld`. La première tranche fixe les actions `move_left`, `move_right`
et `jump`, la vitesse horizontale et la vitesse de saut ; l’état exposé est
`grounded` ou `airborne`.
