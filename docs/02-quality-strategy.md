# Quality strategy

## Test matrix

| Type | Required? | Tool | Command | Rationale |
| --- | --- | --- | --- | --- |
| Unit | Oui | CTest + Catch2 3.15.3 | `ctest --test-dir build -C Debug` | Physique, contrats, graphe, maths et sérialisation. |
| Integration | Oui | CTest | `ctest --test-dir build -C Debug` | Chargement de projet et contrats du cœur. |
| End-to-end | Oui | CTest + SDL2/OpenGL | `ctest --test-dir build -C Debug --output-on-failure` | Parcours éditeur vers runtime et interactions canvas ; une fenêtre ou un affichage virtuel est requis. |
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
n'est configuré. Deux parcours graphiques locaux sont néanmoins automatisés :
`PreviewRuntime` en mode `smoke_test` est explicitement headless et exécute les simulations,
les animations et la production de draw packets sans initialiser SDL vidéo ;
les assertions OpenGL restent réservées au smoke test graphique dédié.
`map_studio_close_e2e` injecte les fermetures fenêtre et système et vérifie la
modale Save/Discard/Cancel ; `map_studio_scene_e2e` lance un shell OpenGL caché,
crée une scène, monte une map, ajoute une transition, sauvegarde, recharge et
publie son paquet de campagne. `npm run test:gl` ajoute un smoke test OpenGL
opt-in : il crée
un contexte SDL caché, rend un draw packet et vérifie les statistiques ainsi
que la couleur lue ; il teste aussi le clipping stencil lorsqu’un stencil est
disponible, puis un crop raster sur une texture bicolore avec lecture du pixel
attendu. Il retourne `77` lorsqu'aucun contexte n'est disponible.
Le test CTest `asset_studio_texture_e2e` lance également le binaire SDL caché,
importe et sélectionne une texture, persiste un crop non destructif, crée une
seconde ressource et valide le projet résultant.
Les parcours E2E qui nécessitent une fenêtre retournent le code `77` lorsque
SDL ne peut pas initialiser l'affichage ou le contexte ; CTest les marque alors
explicitement comme ignorés, tandis qu'une assertion de scénario conserve un
échec normal. Un test ignoré ne constitue donc pas une preuve d'exécution
graphique et ne ferme aucune case de couverture visuelle.
Le mode `build/asset_studio --ui-test <projet>` rend une frame, écrit le
registre JSON des IDs stables et capture `asset_studio-ui-test.ppm` dans le
projet de test ; il est destiné aux contrôleurs UX et ne modifie pas le
document projet.
Le test CTest `asset_studio_ui_registry_e2e` exécute ce mode deux fois sur la
fixture multi-ressources et exige la présence ainsi que la stabilité des IDs de
ressources et de nœud d’entité ; il est ignoré avec le code `77` si aucun écran
ou contexte SDL n’est disponible.
Le test `asset_studio_ui_focus_e2e` ouvre un prompt matériau avec un nom invalide,
attend l’application du focus ImGui sur le premier champ, puis vérifie l’artefact
`asset-studio-ui-focus.json` et la demande de scroll associée.
Le test `asset_studio_ui_accessibility_e2e` vérifie l’activation de la navigation
clavier ImGui et un ratio de contraste texte/fond d’au moins 4,5:1.
Le test `asset_studio_ui_drag_e2e` capture les coordonnées des widgets source et
cible, injecte un drag SDL de `head-button-artwork` vers le nœud `root`, puis
exige la mutation confirmée dans `asset-studio-ui-drag.json`.
Le test CTest `asset_studio_vector_e2e` sélectionne un artwork natif, convertit
sa primitive en path Bézier, insère et retire un point, convertit un segment,
modifie une poignée liée, vérifie undo/redo, sauvegarde et reload, puis valide
le projet résultant. Il prouve le contrat de données et de session, pas encore
le geste souris complet du canvas.
Le test CTest `asset_studio_vector_canvas_e2e` ouvre toutefois Asset Studio dans
une fenêtre SDL cachée, attend la stabilisation ImGui, injecte un clic gauche
sur le canvas Plume puis un clic droit sur l’ancre créée, déplace ensuite une
ancre avec l’outil Move, déplace une poignée en mode `Free` et recharge le
projet après sauvegarde pour vérifier les trois mutations. Il constitue la
première preuve du canvas réel ; les parcours multi-plateformes, diagnostics et
screenshots restent ouverts.
Avant validation ou publication, Map Studio sauvegarde maintenant dans un ordre
unique les sessions mécanique, map et scène dirty ; un échec dans l’une d’elles
annule l’action de package avant toute écriture de publication.
La fermeture Map Studio réutilise la même fonction et expose explicitement
`Retry save and continue`, avec conservation des documents dirty si la reprise
échoue.
Le scénario Behavior E2E crée aussi un document d’input avec plusieurs actions
et bindings, le recharge, puis utilise le même BehaviorGraph pour une source IA
avant son attachement à une entité.
L'ordre de composition d'Asset Studio rend le canvas OpenGL avant ImGui ; le
panneau de preview reste sans fond opaque uniquement pendant ce rendu afin que
les modals, contrôles et gizmos conservent toujours la priorité visuelle.
Le workflow manuel `workflow_dispatch` exécute aussi
`fabric_runtime_benchmark --instances 10000 --frames 600 --min-fps 60` sur
macOS, Windows et Linux sous Xvfb, puis archive un rapport JSON par plateforme.
Ses résultats servent à fermer le gate de performance ; ils ne sont pas
exécutés sur chaque PR.
Le workflow `platform-studio.yml` compile et exécute CTest sur macOS, Windows
et Linux ; Linux utilise Xvfb pour les scénarios SDL. En cas d’échec, il archive
les rapports texte et captures PPM produits par les éditeurs.
Le parcours `map_studio_close_e2e` couvre aussi une fermeture propre et une
fermeture avec sauvegarde réussie, en plus de Cancel/Discard et de l'échec de
sauvegarde.
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
Le pipeline visuel studio-first couvrira l'identité des octets PNG avant et
après édition, le crop réversible en pixels source, les limites de crop, le
round-trip des compositions et la migration d'une référence texture directe
vers une composition implicite sans changement visible. Les overlays,
composants paramétriques et chemins texturés seront évalués par les mêmes draw
packets en test headless, Asset Studio, Map Studio et Preview Runtime.
Les contrats unitaires de manipulation des poignées Bézier couvrent les modes
liés, symétriques et libres, ainsi que le refus des commandes non cubiques.
Les chemins texturés couvriront courbes ouvertes et fermées, répétition,
étirement, largeur variable, UV continus, raccords, texture manquante et
séparation stricte entre ruban de rendu et collision.
Le stroke image vectoriel est couvert par `fabric_render_tests` (texture,
répétition, UV et transform) et par le preset `beam` de la fixture
`tests/fixtures/studio-beam`, qui reste visible dans le parcours des presets.
Le graphe de mécaniques couvre validation des ports, références, cycles
interdits, ordre déterministe, corps et joints invalides, capteurs, moteurs,
undo/redo et reconstruction du monde après reset de preview. La première
fixture est une plateforme tournante activable par présence et transportant
le personnage sans logique propre à la map.
L'export de map couvrira fermeture transitive des dépendances, ordre
déterministe, chemins portables, absence de ressources externes et chargement
du paquet par Preview Runtime. Une comparaison de scène vérifiera le même
résultat visible avant sauvegarde, après reload et depuis le paquet publié.
Le contrat `MapPackageManifest v1` est testé séparément par round-trip strict,
version de schéma, compatibilité SemVer minimale, ordre des ressources,
collisions de chemins et refus des chemins absolus, traversants ou à séparateur
Windows avant l'ajout du copieur de fermeture transitive.
Le test CTest `game_runtime_progress_resume` lance le binaire en smoke test avec
un chemin de progression injecté. Il vérifie qu'un slot existant impose sa scène
malgré une scène CLI différente, conserve toutes ses propriétés, refuse un slot
invalide sans le remplacer, exige une scène pour un slot absent et amorce
correctement un nouveau slot.
La planification de paquet est testée sur les maps Studio de plateforme et de
tête textile, qui traversent prefab, entité, composition, composant, chemin
texturé, mécanique, vectoriel et texture. L'ordre du parcours ne doit pas
influencer le manifeste sérialisé et les prefabs inline ne doivent pas
dupliquer le fichier map.
Des copies temporaires de ces projets couvrent aussi document ou payload
absent, chemin absolu, lien symbolique sortant, cycle composant/composition et
même identifiant utilisé par deux types. Ces erreurs doivent être retournées
avant la création d'un dossier de paquet.
La publication headless vérifie la copie du manifeste et des payloads,
l'absence d'écrasement d'une destination existante et le rollback d'une
destination nouvellement créée en cas d'échec. Le même contrat est couvert
pour `ScenePackageManifest v1` : transitions cycliques autorisées, fermeture
de toutes les scènes et maps atteignables, round-trip déterministe, chargement
de la scène racine ou d'une scène active, puis transition atomique depuis le
paquet. La commande réelle est
`fabric_map_package_export --scene <id> <projet> <destination>` suivie de
`game_runtime --package <destination> --smoke-test 1`.
Les zones gameplay couvrent séparément le refus des chains et collisions
non-sensor, l'intersection cercle/capsule/polygone avec une box d'acteur, deux
acteurs simultanés, les sorties indépendantes et la fusion déterministe du
payload événement/trigger. Un test Preview Runtime publie une map contenant une
entité `monster-one`, la lance sans personnage CLI et exige l'événement portant
son `actor_id`. Le parcours graphique Scene Studio ajoute aussi collision,
événement, trigger et propriété locale, puis les vérifie après reload et dans
le paquet publié.
Le workflow CI publie une fois `studio-rotating-platform` sur Ubuntu, transfère
le dossier comme artefact et le charge avec `game_runtime --package` sur Ubuntu,
macOS et Windows ; aucune conversion ni réécriture n'est exécutée entre les
plateformes.
Les actions Map Studio valident la version courante de la session : une map
dirty est sauvegardée avant `Validate` ou `Publish`, et l'action est annulée si
la sauvegarde échoue.
Les actions principales désactivées de Map Studio affichent aussi leur
précondition au survol ; cette couverture reste partielle tant que les
contrôles secondaires d’Asset Studio ne sont pas harmonisés.
Asset Studio couvre désormais les préconditions des actions principales du
Resource Explorer, des créations Behavior/Transformation et de la pose de
clés ; les contrôles d’édition fins restent à couvrir avant la fermeture du
gate global.
Les tests actuels couvrent le round-trip `RasterView v1`, les crops hors limites,
les transformations, le filtrage, undo/redo, autosave, récupération et la
conservation byte-for-byte de la source PNG. Asset Studio et Preview Runtime
utilisent le même constructeur de packet raster ; un test d'intégration compare
le packet runtime au packet studio attendu et le smoke OpenGL vérifie le pixel
de référence réellement échantillonné après crop.
`VisualComposition v1` couvre le round-trip strict des quatre genres de
calques, les références typées, identifiants stables, ancrages, transforms,
opacité, Z, publication atomique et refus des champs inconnus. Le validateur
headless résout ses dépendances et vérifie chaque crop local contre les
dimensions de la texture référencée.
`VisualComponent v1` couvre le round-trip des huit types de paramètres,
bounds, ancrages, bindings, variantes et instances. Les tests vérifient la
priorité défaut → variante → instance, la découverte des propriétés animables,
la publication atomique, les types incompatibles, les cibles absentes et les
cycles composant/composition dans le validateur headless.
La conversion primitive → path est testée pour rectangle, ellipse et ligne,
avec refus des lignes dégénérées ; la géométrie convertie reste éditable dans
le personnalisateur Asset Studio. Les insertions et suppressions de commandes
de segment vérifient aussi la conservation des points, la tête `move` et le
minimum de deux commandes ; la plume et
la manipulation directe des points sur canvas sont implémentées, mais restent
à couvrir par un parcours SDL E2E avant de fermer leur gate UX.
Le test `resource index administers every directly creatable resource` exerce
la sélection, duplication, renommage, mise en corbeille, restauration et les
collisions d'identifiant sur les 16 familles indexées : texture, vector,
matériau, entité, animation, input, BehaviorGraph, transformation,
texturedPath, composition visuelle, composant visuel, map, scène, mécanique,
replay et audio.
Le même ensemble de tests couvre les collisions d'identifiant et les
documents absents ; les tests de fondations couvrent les cycles de dépendances
et le test de sauvegarde de session couvre un échec disque sans perdre la
sélection ni le document sale.
La transition de projet applique maintenant le garde commun à `open` et
`create` ; quatre scénarios vérifient qu'un document valide est sauvegardé et
qu'un document invalide conserve le projet actif et empêche la nouvelle cible.
Les transitions d'import et de duplication disposent du même contrôle valide /
invalide, avec vérification qu'aucune nouvelle ressource n'est publiée après
un échec de sauvegarde.
Les opérations de plume testent aussi l'ouverture et la fermeture idempotentes
des contours, sans perdre le dernier segment.
Le contrat vectoriel teste la transformation groupée d'ancres autour du
centroïde et rejette les sélections contenant des index dupliqués ; Asset
Studio expose cette sélection par Shift, le déplacement canvas et les actions
groupées de rotation/échelle.
La sélection d'une autre ressource vérifie également la conservation du
document courant lorsqu'une sauvegarde devient impossible ; avec le parcours
de fermeture Map Studio (`clean`, `save`, `cancel`, `discard`, échec), les
actions de transition listées disposent désormais de scénarios propres,
valides et invalides.
La fixture `tests/fixtures/studio-preset-gallery` est régénérée exclusivement
avec `ProjectSession` et `MapSession`. Son test compare tous les fichiers
octet par octet, valide le graphe puis charge sa map dans Preview Runtime. La
régénération volontaire s'effectue sur une destination absente avec
`FABRIC_UPDATE_STUDIO_PRESET_FIXTURE=1 ./build/fabric_visual_presets_tests`.
La fixture `tests/fixtures/studio-textile-head` suit le même protocole avec
`FABRIC_UPDATE_STUDIO_HEAD_FIXTURE=1` et vérifie en plus la fermeture du graphe
composé, les 21 draw packets, l'évaluation du Beam et les UV du crop raster à
mi-largeur. Elle crée
aussi son entité et sa map par les sessions Studio ; Preview Runtime charge
cette map et chaque packet est apparié par identifiant stable pour comparer
géométrie, UV, indices, couleurs et texture au résultat du resolver direct.
Elle contient également une ressource input, un BehaviorGraph, un matériau,
un document audio et une scène afin de fournir une fixture multi-ressources au
Resource Explorer et aux futurs parcours UX. Ces cinq documents auxiliaires
sont comparés sémantiquement lors de la régénération, car leur sérialisation
canonique réordonne les champs et normalise les flottants ; les documents
produits par les sessions Studio restent comparés octet par octet.
La scène conserve également une plateforme tournante créée par Map Studio,
son prefab, son capteur et l'événement `platform-activate`; le graphe est
validé dans la même fixture afin que la suite puisse ajouter la simulation et
la réaction du personnage sans changer de projet de référence. Le test charge
ensuite ce graphe depuis le disque, matérialise le personnage dans le capteur,
vérifie les états d'activation et les événements de début, puis confirme le
transport physique et la rotation bornée.
Le même test recharge un replay à 61 frames et publie le paquet de map avant
de le recharger directement dans Preview Runtime. Le binaire
`fabric_runtime_benchmark` accepte désormais `--project ... --map ...` ou
`--package ...` ; le workflow CI `textile-reference-benchmark` archive les
rapports p95 de la scène textile sur Ubuntu, macOS et Windows avec un seuil de
60 FPS.
`fabric_preview_runtime_tests` publie aussi un projet temporaire contenant une
mécanique motorisée sans activation externe, exporte son paquet puis charge
uniquement ce paquet. Après 60 frames SDL cachées, il vérifie le nombre exact de
pas mécaniques, la rotation Box2D du corps et le déplacement effectif du draw
packet lié à l'entité du prefab.
La fixture `tests/fixtures/studio-rotating-platform` conserve le graphe créé
par Map Studio et l'entité textile créée par Asset Studio. Son test headless
matérialise le capteur Box2D, place un personnage dynamique, vérifie son
transport par friction, les états actif/inactif et les transitions
`begin/end`. Le build de Map Studio vérifie en plus les overlays de corps,
capteur et personnage consommant exactement ces états de preview. La même
fixture enregistre la mécanique dans un prefab, remplace vitesse et taille de
capteur par identifiant de paramètre, puis vérifie round-trip, compilation
effective, preview du prefab, undo, rejet des noms/types invalides et échec du
validateur de projet face à un override sémantiquement inconnu.
Les commandes d'édition de composition et composant couvrent modification et
duplication de calque, transform, Z, visibilité, ancrages et paramètres. Le
test session vérifie undo/redo, autosave, récupération, sauvegarde et reload
avant que ces commandes soient exposées par l'inspecteur Asset Studio.
La session projet vérifie aussi qu'une création ou une nouvelle sélection
sauvegarde automatiquement le document vectoriel dirty précédent, recharge le
contenu persisté attendu et termine sur la nouvelle ressource sans dirty
résiduel.
La session mécanique vérifie le même invariant pour création et ouverture : le
graphe dirty précédent est rechargé depuis son fichier avec ses nœuds attendus
avant et après la transition.
La session map couvre création et ouverture selon le même scénario et recharge
chaque map précédente depuis son fichier pour prouver la persistance de ses
instances.
Le CTest `map_studio_close_e2e` exécute le vrai binaire avec une fenêtre cachée
sur trois copies temporaires de la fixture textile. Il couvre fermeture de
fenêtre, `SDL_QUIT` produit par le raccourci système et échec de remplacement
atomique ; chaque cas compare les octets du principal et de l'autosave et
confirme que la session dirty reste ouverte après Cancel ou Save échoué.
Les tests de scène publient deux maps montées sous des namespaces distincts,
comparent leurs couches et instances composées, fusionnent un événement
compatible et refusent les mounts dupliqués. `SceneRuntimeSession` résout un
point `sceneEntryPoint` dans la cible, conserve la scène source si le point est
absent et `PreviewRuntime` charge les deux maps puis reçoit le spawn résolu.
`TexturedPath v1` couvre le round-trip strict des commandes ligne/cubique, les
chemins ouverts/fermés, profils de largeur, modes UV, couleur, opacité,
raccords et terminaisons. Les tests vérifient sa publication atomique, sa seule
dépendance texture, la résolution headless des références et l'absence de
collision ou de maillage persisté dans le document d'auteur.
La session d'édition couvre aussi déplacement des attaches et poignées, ajout
de segments, largeur, répétition, offset, couleur et opacité avec undo/redo,
autosave, récupération, sauvegarde et reload. Le packet dérivé depuis l'état
en mémoire vérifie immédiatement UV, teinte et opacité avant écriture disque.
La géométrie dérivée couvre le ruban exact d'une ligne, l'aplatissement Bézier
borné et reproductible, les fermetures avec couture UV, l'interpolation de
largeur, les modes repeat/stretch, les raccords miter/bevel/round et les caps
butt/square/round. Le smoke OpenGL vérifie aussi la teinte des textures et la
répétition longitudinale réellement échantillonnée.
La factory de presets visuels couvre des sorties déterministes pour œil,
bouton, couture et fermeture. Les tests inspectent les primitives natives, les
deux rails texturés, le nombre borné de dents, le curseur et les paramètres
animables, puis publient les quatre bundles dans un même projet et valident le
graphe headless complet. Une destination existante est refusée avant écriture.
Le Beam réutilise le preset couture : son paramètre d'offset est découvert par
le registre de propriétés, sélectionné comme binding de timeline et interpolé
par `AnimationClip v3` sans piste ni valeur spécialisée.
Le resolver animé compose ensuite ces valeurs avec l'instance de composant et
reconstruit ses packets. Les tests vérifient largeur, offset UV et couleur du
Beam dans le resolver partagé, puis le remplacement d'une échelle de composant
sur une frame réellement produite par Preview Runtime.
Le test de session crée aussi une fermeture depuis le même point d'entrée que
le Studio, vérifie l'indexation de ses ressources, puis rouvre le projet et
resélectionne son composant.
Le resolver de preview est testé sur les quatre presets, l'ordre Z, les
transforms, les paramètres de composant, les textures manquantes et les cycles
de composants. Ses paquets sont les mêmes entrées OpenGL que celles du runtime.
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
Les contrats `MaterialDefinition v1` et `EntityDefinition v4` couvrent
round-trip, migration des entités v1, publication atomique, références typées,
instances de composants visuels, transforms non finies, identifiants dupliqués
et cycles de parentage. Le validateur headless résout aussi variantes, ancres et
overrides des composants portés par les entités. Preview Runtime vérifie le
parcours map → entité → composant jusqu'aux draw packets transformés.
Le contrat `AnimationClip v3` couvre parseur strict, round-trip, publication
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
Les règles de canvas couvrent aussi le hit-test après translation, rotation et
échelle, l'ordre de sélection du dessus vers le dessous et le prolongement de
la poignée de rotation dans l'orientation locale du nœud.
Les prompts graphiques de matériau et d'entité sélectionnent leurs références
depuis l'index typé de la session ; les validateurs headless restent la barrière
finale contre une référence absente ou incompatible.
Le contrôleur de crop raster borne chaque déplacement et poignée aux dimensions
de la source ; ses tests vérifient déplacement, agrandissement maximal et taille
minimale sans réécriture des pixels.
