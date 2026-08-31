# ADR-0135 — Package release CMake/CPack

- Statut : accepté
- Date : 2026-08-31

## Contexte

Les exécutables desktop étaient compilables mais le dépôt ne fournissait pas
de cible d’installation ni de vérification du contenu distribué. La checklist
release ne pouvait donc pas distinguer un build local d’un artefact installable.

## Décision

CMake installe les outils, les studios et le runtime quand ils sont activés,
ainsi qu’un projet exemple validé. CPack produit des archives TGZ et ZIP. Un
test CTest installe dans un préfixe temporaire, vérifie les exécutables et les
ressources, valide le projet exemple, lance `game_runtime --help` et exige une
archive CPack.

Le smoke test ne prétend pas valider à lui seul les mises à jour,
désinstallations ou intégrations natives de chaque système d’exploitation ;
ces contrôles restent des gates release séparés.

## Conséquences

- Le packaging est reproductible depuis la configuration CMake.
- Les archives contiennent un exemple utilisable et inspectable.
- Les tests CI peuvent détecter une installation incomplète avant publication.
