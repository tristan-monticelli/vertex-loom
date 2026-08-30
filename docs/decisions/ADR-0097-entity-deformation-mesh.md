# ADR-0097 — Persistance du maillage de déformation d’une entité

## Statut

Accepté — 2026-08-25

## Décision

`EntityDefinition v1` peut contenir un `deformationMesh` optionnel. Le maillage
stocke les positions de repos, les influences pondérées par identifiant de
nœud et les triangles par indices. Une entité sans déformation conserve
`deformationMesh: null`.

Le parseur refuse les formes JSON incorrectes, les indices non entiers et les
poids non numériques. Le validateur réutilise `validate_deformation_mesh` et
vérifie que chaque influence cible un nœud existant. Le solveur reste séparé du
format et sera intégré dans un incrément ultérieur.

## Conséquences

- Les meshes sont sauvegardés et rechargés sans rasterisation persistante.
- Les documents historiques sans ce champ restent compatibles.
- La topologie et les influences sont déterministes et inspectables headless.
