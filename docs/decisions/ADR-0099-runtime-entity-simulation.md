# ADR-0099 — Chargement runtime des simulations d’entités

## Statut

Accepté — 2026-08-25

## Décision

Le `PreviewRuntime` charge les champs `deformationMesh` et `xpbd` de chaque
instance d’entité avant toute création de fenêtre. Il conserve une copie de
simulation par instance, construit les poses depuis les transforms des nœuds,
expose une évaluation headless du maillage et exécute quatre itérations XPBD à
chaque pas fixe de `1/60` seconde.

Les draw packets restent inchangés dans cette tranche : le maillage simulé est
accessible par l’API runtime et l’intégration de ses sommets au rendu sera une
étape dédiée, afin de conserver une correspondance explicite entre topologie,
triangles et packets.

## Conséquences

- Une entité invalidement sérialisée est refusée avant SDL, comme les autres
  ressources runtime.
- Les instances partagent le format mais jamais l’état mutable XPBD.
- Les tests headless peuvent vérifier les poses et l’évolution XPBD sans GPU.
