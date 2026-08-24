# Quality strategy

## Test matrix

| Type | Required? | Tool | Command | Rationale |
| --- | --- | --- | --- | --- |
| Unit | Oui | CTest + Catch2 3.15.3 | `ctest --test-dir build -C Debug` | Physique, contrats, graphe, maths et sérialisation. |
| Integration | Oui | CTest | `ctest --test-dir build -C Debug` | Chargement de projet et contrats du cœur. |
| End-to-end | Plus tard | À sélectionner | null | Parcours éditeur vers runtime. |
| Contract | Oui | validateurs C++ | `ctest --test-dir build -C Debug` | Schémas et versions de ressources. |
| Property | Plus tard | À sélectionner | null | Invariants de physique après stabilisation. |
| Snapshot | Non | — | null | Variations GPU ; scènes de référence manuelles. |
| Performance | Oui | benchmarks C++ | null | Boucle, renderer et chargement. |
| Security | Oui | validation locale | `ctest --test-dir build -C Debug` | Chemins et imports invalides. |
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
La session projet des éditeurs est testée sans fenêtre : ouverture valide,
création complète, refus d'une destination occupée, diagnostics d'échec et
conservation du dernier projet valide. La coquille
graphique est compilée sur les trois plateformes de CI ; son lancement visuel
reste un smoke test de release tant qu'aucun environnement graphique virtuel
n'est configuré. Le chargeur PNG couvre un fichier RGBA valide, une extension
incorrecte et un contenu corrompu sans initialiser de fenêtre. L'import de
texture couvre le round-trip du document, les chemins traversants, la copie
persistante d'un PNG valide, le refus d'un contenu corrompu et d'un identifiant
existant, la conservation du dernier import réussi et le rejet d'une source
projet manquante par le validateur headless. `npm run
validate` couvre aussi le round-trip du contrat `VectorAsset`, le refus des
chemins SVG traversants, le décodage borné d'un aperçu SVG, la publication sans
remplacement, la conservation du dernier import vectoriel réussi et le rejet
d'une source SVG manquante par le validateur headless. `npm run validate`
regroupe les validations documentaires,
Node et C++ ;
`npm run validate:cpp` exécute uniquement la configuration, le build et CTest.
Les nouvelles suites C++ utilisent Catch2 ; les exécutables de test historiques
restent inchangés. Le manifeste couvre les migrations `v0 -> v1 -> v2`, la
valeur par défaut `pixelsPerUnit = 100` et le rejet des valeurs non finies ou
non positives. Le registre couvre résolution typée, doublons, documents
manquants et cycles, puis le validateur headless applique ces contrôles à tous
les documents de ressources connus sans créer de fenêtre.
