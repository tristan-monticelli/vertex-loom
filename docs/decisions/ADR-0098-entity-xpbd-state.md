# ADR-0098 — Persistance de l’état XPBD d’une entité

## Statut

Accepté — 2026-08-25

## Décision

`EntityDefinition v1` peut contenir un système `xpbd` optionnel. Le document
persiste les particules, les cinq familles de contraintes prises en charge par
le solveur (distance, flexion, aire, pin et collision) et leurs lambdas afin de
permettre une reprise déterministe d’un état simulé.

Les indices sont des indices de particules stables dans le document. Le
parseur exige les six tableaux, des nombres finis et des indices non signés ;
le validateur réutilise `validate_xpbd_system`. Les paramètres de fréquence et
d’itérations restent des paramètres d’exécution ; la validation de document
utilise le pas nominal de 1/60 seconde.

## Conséquences

- Un état XPBD peut être sauvegardé et rechargé sans conversion intermédiaire.
- Les documents historiques sans champ `xpbd` restent compatibles.
- L’intégration avec le runtime, la quantification de replay et les commandes
  d’édition restent des incréments séparés.
