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
Les intégrations précèdent chaque étape fonctionnelle. Le benchmark
`./build/fabric_render_benchmark --packets 10000 --frames 600` est requis avant
une release ou une modification du renderer. Les tests Node de
gouvernance restent exécutés par `npm test`. Le contrat projet couvre le
round-trip JSON, les versions non prises en charge, les chemins traversants et
le comportement du validateur headless sur des fixtures valides et invalides.
Il couvre aussi les migrations `v0` vers `v1` puis `v2` et la conservation du manifeste
précédent lorsqu'une sauvegarde invalide est refusée.
La journalisation vérifie la structure JSON Lines et l'échappement des données
non fiables. Le validateur headless vérifie ses sorties humaine et structurée.
La session projet des éditeurs est testée sans fenêtre : ouverture valide,
création complète, refus d'une destination occupée, diagnostics d'échec et
conservation du dernier projet valide. La coquille
graphique est compilée sur les trois plateformes de CI ; son lancement visuel
reste un smoke test de release tant qu'aucun environnement graphique virtuel
n'est configuré. `npm run test:gl` ajoute un smoke test OpenGL opt-in : il crée
un contexte SDL caché, rend un draw packet et vérifie les statistiques ainsi
que la couleur lue ; il teste aussi le clipping stencil lorsqu’un stencil est
disponible et retourne `77` lorsqu'aucun contexte n'est disponible.
Le chargeur PNG couvre un fichier RGBA valide, une extension
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
La pile de commandes couvre exécution, échec, fusion continue, undo, redo,
branche divergente et point dirty. Le stockage couvre remplacement atomique,
refus avant écriture et conservation du principal en cas d’échec. L’autosave
couvre les seuils de 2 secondes d’inactivité et 30 secondes maximum, les
chemins miroir, les versions absentes, invalides ou anciennes et les décisions
de récupération acceptée ou refusée sans interface graphique.
Le pipeline sprite retiré par ADR-0025 ne fait plus partie de la matrice. Les
audits structurels vérifient son absence des contrats et du build.
`VectorAsset v2` couvre la lecture de v1
comme `linkedSvg`, le maintien du chemin source et la sérialisation sans le
champ `format`. Le socle natif couvre round-trip, dimensions, origine,
identifiants dupliqués, rectangle/ellipse, fill couleur/transparent,
publication atomique sans SVG et chargement par le validateur headless. Le fill
image couvre round-trip du cadrage et du transform, opacité, liaison à la
déformation, résolution de texture et refus d’une référence manquante. Les
SVG liés couvrent aussi la conversion NanoSVG explicite vers chemins cubiques,
fills couleur et contours, ainsi que le diagnostic des gradients non supportés.
La session éditeur couvre la conversion liée-vers-native, sa publication,
undo, restauration du SVG lié, redo implicite et rechargement après sauvegarde.
`fabric_asset_preview` est vérifié headless sur les mêmes draw packets que le
renderer et refuse les documents liés ou invalides sans créer de fenêtre.
Les contrats `MaterialDefinition v1` et `EntityDefinition v1` couvrent
round-trip, publication atomique, références typées, transforms non finies,
identifiants dupliqués et cycles de parentage.
Le contrat `AnimationClip v1` couvre parseur strict, round-trip, publication
atomique, bindings stables, valeurs scalaire/Vec2/couleur/booléen/référence,
interpolations step/linear/cubic, boucle et rejet des valeurs non interpolables.
Le registre de descripteurs couvre résolution par binding, propriétés
animables/inscriptibles, doublons, identifiants manquants et bornes inversées.
La timeline couvre insertion, tri, déplacement, suppression protégée de la
dernière clé, durée, boucle et restauration complète par undo/redo.
Le graphe d’états couvre références de clips, états initiaux, conditions
booléennes/numériques, exit time, priorité déterministe et rejet des endpoints
ou identifiants invalides.
Les contraintes couvrent tri par ordre explicite, types supportés, dépendances
source/cible et rejet des cycles.
FABRIK couvre convergence déterministe, racine fixe, cible hors de portée,
chaîne dégénérée, tolérance et nombre maximal d’itérations.
XPBD couvre distance, flexion, aire, pin, collision, compliance, lambda,
quantification, masses et indices invalides sans modifier l’état en cas d’échec.
La déformation maillée couvre mélange pondéré, transforms, triangles invalides,
poids nuls, poses dupliquées et références de poses manquantes.
`MapDocument v1` couvre round-trip, publication atomique, chunks 64 × 64,
calques, prefabs, instances, formes de collision, capteurs, triggers,
événements et propriétés custom strictement typées.
Les événements map couvrent déclarations uniques, payloads typés et rejet des
triggers qui référencent un événement absent.
L’index de chunks couvre 100 000 instances, coordonnées négatives, ordre
déterministe et extraction par viewport.
`fabric_physics` couvre la création/destruction d’un monde Box2D v3.1.1,
pas fixe valide et rejet des pas nuls ou négatifs.
Le chargement physique couvre instanciation de cercle, capsule, polygone,
segments de chaîne et capteur depuis une map validée.
formes et fills futurs devront ajouter les tests de chemins, contours, clips,
images remplissantes, tessellation déterministe et draw packets headless. Les
modèles de prompts
sont testés sans Dear ImGui : defaults et presets, erreurs par champ,
destination exacte, conflits de ressources, isolation de projet/artwork/import,
annulation par reset sans effet et résumé avant publication.
L'espace de travail Asset Studio couvre aussi sans fenêtre l'indexation des
textures et vecteurs, la sélection et le rechargement, l'aperçu temporaire sans
publication, le choix d'une texture par ressource et les mutations natives via
undo, redo, dirty et autosave. Un test de contrôleur vérifie les décisions
Save/Discard/Cancel avant remplacement de session ; les raccourcis et la mise
en page restent dans le smoke test visuel multiplateforme. Le déplacement par
glisser du nœud sélectionné réutilise le même chemin de commande que l’inspecteur
et fusionne les positions continues.
