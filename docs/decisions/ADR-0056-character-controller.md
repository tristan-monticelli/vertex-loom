# ADR-0056 — Contrôleur de personnage hybride

## Statut

Accepté.

## Décision

Le contrôleur de personnage reçoit un `CharacterControlFrame` normalisé et
délègue le déplacement à un corps dynamique de `PhysicsWorld`. Il ne connaît
aucun identifiant d'action. Le runtime peut construire ce frame depuis trois
actions sémantiques choisies explicitement, tandis que le BehaviorGraph reste
la voie générique pour programmer toute entité. L’état exposé est `grounded` ou
`airborne`.

La vitesse verticale du corps est conservée entre les mises à jour du
contrôleur afin que la gravité et l’impulsion de saut restent entièrement
gérées par Box2D.
