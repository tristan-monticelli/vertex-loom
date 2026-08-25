# ADR-0099 — Chargement runtime des simulations d’entités

## Statut

Accepté — 2026-08-25

## Décision

Le `PreviewRuntime` charge les champs `deformationMesh` et `xpbd` de chaque
instance d’entité avant toute création de fenêtre. Il conserve une copie de
simulation par instance, construit les poses depuis les transforms des nœuds,
expose une évaluation headless du maillage et exécute quatre itérations XPBD à
chaque pas fixe de `1/60` seconde.

Les draw packets sont déformés lorsque leur nombre de sommets et leur topologie
correspondent au maillage de l’entité. La correspondance est par indice et les
triangles doivent correspondre exactement aux indices du packet ; un packet
incompatible reste inchangé et le compteur runtime ne le compte pas.

Quand `deformationMesh` et `xpbd` sont tous deux présents, le validateur exige
une correspondance 1:1 entre sommets et particules. L’évaluation runtime
retourne alors les positions XPBD quantifiées ; sans XPBD, elle applique les
poses de nœuds aux positions de repos.

## Conséquences

- Une entité invalidement sérialisée est refusée avant SDL, comme les autres
  ressources runtime.
- Les instances partagent le format mais jamais l’état mutable XPBD.
- Les tests headless peuvent vérifier les poses et l’évolution XPBD sans GPU.
