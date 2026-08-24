# Quality strategy

## Test matrix

| Type | Required? | Tool | Command | Rationale |
| --- | --- | --- | --- | --- |
| Unit | Oui | CTest | `ctest --test-dir build` | Physique, collisions, maths et sérialisation. |
| Integration | Oui | CTest | `ctest --test-dir build` | Chargement de projet et contrats du cœur. |
| End-to-end | Plus tard | À sélectionner | null | Parcours éditeur vers runtime. |
| Contract | Oui | validateurs C++ | `ctest --test-dir build` | Schémas et versions de ressources. |
| Property | Plus tard | À sélectionner | null | Invariants de physique après stabilisation. |
| Snapshot | Non | — | null | Variations GPU ; scènes de référence manuelles. |
| Performance | Oui | benchmarks C++ | null | Boucle, renderer et chargement. |
| Security | Oui | validation locale | `ctest --test-dir build` | Chemins et imports invalides. |
| Mutation | Non | — | null | Trop coûteux avant stabilisation de la physique. |

## Decision rule

Les tests unitaires et de contrat s'exécutent à chaque modification du cœur.
Les intégrations précèdent chaque étape fonctionnelle. Les benchmarks sont
requis avant une release ou une modification du renderer. Les tests Node de
gouvernance restent exécutés par `npm test`. Le contrat projet couvre le
round-trip JSON, les versions non prises en charge, les chemins traversants et
le comportement du validateur headless sur des fixtures valides et invalides.
Il couvre aussi la migration `v0` vers `v1` et la conservation du manifeste
précédent lorsqu'une sauvegarde invalide est refusée.
La journalisation vérifie la structure JSON Lines et l'échappement des données
non fiables. Le validateur headless vérifie ses sorties humaine et structurée.
`npm run validate` regroupe les validations documentaires, Node et C++ ;
`npm run validate:cpp` exécute uniquement la configuration, le build et CTest.
