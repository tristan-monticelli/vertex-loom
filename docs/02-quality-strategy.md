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

La mesure locale du 31 août 2026 donne, sur 600 frames et 10 000 éléments,
`fabric_render_benchmark` à 199,8 FPS p95 (2 487 ms cumulées) et
`fabric_runtime_benchmark` à 37,0 FPS p95 (15 743 ms cumulées). Ces chiffres
établissent une baseline reproductible, mais ne ferment pas la recette release
qui demande aussi démarrage, mémoire et temps de chargement sur deux tailles
de projet.
Le checker vector canvas compare aussi l’occupation et la plage de canaux de sa
capture à la référence versionnée
`tests/fixtures/visual-baselines/asset-studio-vector-canvas-visual.json`.
Cette baseline utilise désormais `beam-border`, après retrait des faux artworks
Eye/Button, et conserve une plage qui exclut explicitement un canvas vide.

## Decision rule

Le test Node `default-assets.test.mjs` décode les trois PNG distribués, exige
RGBA8 2048×2048, vérifie alpha, zone de silhouette et SHA-256, puis contrôle
les champs de provenance. Avec `VERTEX_LOOM_PUBLIC_RELEASE=1`, il exige en plus
une licence résolue et une preuve écrite présente pour chaque image. Le smoke
d’installation recalcule les mêmes empreintes depuis un préfixe isolé et exige
le manifeste, la notice et le bundle de licences.

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
attendu. Le nested clipping est exclu du smoke sous Xvfb Linux et sous Mesa
WGL Windows, dont les rasterizers logiciels ne conservent pas les références
stencil imbriquées ; le clipping simple reste vérifié. Le workflow installe un
bundle Mesa3D logiciel épinglé sur Windows, puis
exécute les E2E Asset/Map Studio et le smoke OpenGL avec llvmpipe ; les tests
headless restent exécutés sur les trois plateformes. Il retourne `77`
lorsqu'aucun contexte n'est disponible.
Le test CTest `asset_studio_texture_e2e` lance également le binaire SDL caché,
importe et sélectionne une texture, persiste un crop non destructif, crée une
seconde ressource et valide le projet résultant.
Le workflow annule automatiquement les runs obsolètes d'une même branche. Les
parcours SDL des studios ont un timeout CTest de 180 secondes et le job
plateforme une limite de 20 minutes afin qu'un blocage de fenêtre, de driver,
de build ou d'installation devienne un échec diagnostiquable sur Windows.
Le workflow publie aussi les captures `asset-studio-vector-canvas-pen.ppm`,
`asset-studio-vector-canvas-handles.ppm`, `asset-studio-vector-canvas-final.ppm`,
les trois couples join/cap et la variante avancée afin que la preuve visuelle
soit récupérable depuis chaque run multiplateforme.
Le même parcours mute un vectoriel déjà créé pour appliquer un fill image et un
stroke image, puis vérifie leur rendu dans les captures finales.
Le test CTest `asset_studio_vector_canvas_e2e` configure un stroke image répété,
capture les frames Pen et poignées du canvas après rendu OpenGL/ImGui, vérifie
un probe pixel de sa zone native avec un nombre minimal de pixels et une plage
de canaux suffisante pour exclure un aplat, puis rejoue le clic sur un coin existant, sa
sélection, `Delete`, la sauvegarde et le reload. Il échoue si une capture est
absente ou plate, si la sélection n'est pas faite ou si le path supprimé
réapparaît après reload.
`fabric_canvas_interaction_tests` vérifie aussi la suppression d’une sélection
multiple de points, l’ordre d’effacement et la conservation de la tête `move`.
Le test CTest `release_product_recipe` enchaîne le parcours complet sur la
fixture textile : édition et sauvegarde dans Asset Studio, réouverture et
validation du projet, puis lancement du Runtime sur la map publiée en mode
smoke. Il constitue la preuve automatisée du flux produit avant les recettes
manuelles de release.
Le test CTest `release_data_robustness` rejette un projet invalide et un projet
dont une ressource vectorielle référencée a été supprimée, tout en conservant
le cas valide comme contrôle de non-régression.
Le test `release_recovery_smoke` copie la fixture, simule une interruption après
autosave, réouvre une nouvelle session, accepte la récupération et vérifie que
la modification sauvegardée survit à un reload sans candidat résiduel.
Le test CTest `release_performance_smoke` exécute les benchmarks renderer et
runtime sur des profils de 100 puis 10 000 éléments, avec seuils FPS et rapports
JSON séparés ; les rapports exposent aussi le temps d'initialisation/rendu, le
temps de chargement runtime et le pic mémoire du processus, mesuré avec les
APIs natives de chaque OS.
Les rapports renderer enregistrent maintenant vendor, renderer et version
OpenGL. La variable `FABRIC_REQUIRE_NATIVE_GL=1` fait échouer explicitement une
recette qui reçoit Mesa, llvmpipe, softpipe ou un renderer inconnu.
Le workflow `platform-studio.yml` expose une exécution manuelle
`native_gpu=true` sur un runner Windows auto-hébergé étiqueté `gpu`; cette gate
exécute tout CTest sans la configuration Mesa et applique cette variable au
benchmark renderer.
Le smoke packaging réinstalle également sur le même préfixe puis vérifie que le
staging peut être entièrement retiré ; l’intégration aux installateurs natifs
reste une validation par plateforme.
Les parcours E2E qui nécessitent une fenêtre retournent le code `77` lorsque
SDL ne peut pas initialiser l'affichage ou le contexte ; CTest les marque alors
explicitement comme ignorés, tandis qu'une assertion de scénario conserve un
échec normal. Un test ignoré ne constitue donc pas une preuve d'exécution
graphique et ne ferme aucune case de couverture visuelle.

Le smoke OpenGL des profils shader vérifie quatre invariants pixels : le mode
`Source intacte` avec teinte, holographie et shine à zéro restitue les canaux et
l'alpha utiles du PNG, la couleur holographique reste inactive à `0`,
l'holographie utilise seulement la couleur choisie à `1`, et `Recoloration`
utilise seulement la couleur utilisateur. Il contrôle
aussi un gain de shine identique sur deux texels distincts et produit
`build/test-artifacts/fabric-render-shader-smoke.ppm` pour inspection isolée. La seule présence des
paramètres dans un draw packet ou un document JSON ne constitue pas une preuve
de coloring.
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
Le test `map_studio_ui_accessibility_e2e` applique la même preuve à Map Studio
et produit avant le swap un artefact JSON ainsi qu’une capture PPM ; la preuve
échoue sous 960×640 ou si tous les canaux sont identiques. Il est ignoré avec
le code `77` sans écran SDL.
Le test `asset_studio_ui_drag_e2e` capture les coordonnées des widgets source et
cible, injecte un drag SDL de `beam-border` vers le nœud `root`, puis
exige la mutation confirmée dans `asset-studio-ui-drag.json`.
Les variantes `asset_studio_ui_drag_root_e2e` et
`asset_studio_ui_drag_child_e2e` couvrent respectivement l’ajout d’un nouveau
root et d’un enfant, puis vérifient leur parenté après reload et la destination
déclarée dans l’artefact.
Le test `asset_studio_ui_texture_e2e` importe une texture de fixture, rend le
canvas de crop raster, injecte un geste SDL vers une poignée et vérifie un crop
non destructif borné dans `asset-studio-ui-texture.json`.
Le test `asset_studio_ui_input_e2e` rend le prompt de bindings, prépare les
actions `move` et `attack`, crée le document depuis ce parcours et vérifie sa
relecture avec un binding clavier et un binding gamepad.
Le test `asset_studio_ui_beam_e2e` rend l'assistant Beam sur le fixture projet
réel `studio-beam`, dont `defaultStrokeTexture` pointe vers `beam-thread`,
localise son bouton, injecte un clic souris en trois frames, capture le
formulaire dans `asset-studio-beam-create.ppm`, puis capture un Beam blanc
neutre et recharge le `texturedPath`. Il vérifie la texture héritée,
l'épaisseur, l'opacité et la répétition sans injecter la texture ni appeler
directement la factory à la place de l'action UI. Le test séparé
`asset_studio_ui_beam_holography_e2e` rejoue le même parcours avec une teinte
bleue, une couleur holographique or et de la brillance ; sa capture doit rester
exempte de rose implicite.
Le test géométrique `Beam keeps repeated texture UVs continuous across
external segments` couvre aussi une texture externe sur une ligne, une courbe
et un segment final, avec UV d'arc-length monotones et shader holographique.
Le rail Asset Studio masque par défaut les dépendances visuelles générées
(`*-border`, `*-rail`, `*-composition`) afin que l'utilisateur ouvre le
composant Beam plutôt que son vecteur interne ; la case `Show technical
resources` conserve l'accès expert sans modifier les contrats persistés.
Le test `asset_studio_ui_button_e2e` rend l'assistant Button avec un PNG
original, ses couleurs et paramètres shader, injecte un clic sur le vrai bouton
de création, capture le résultat puis recharge l'Entity et son Material v2. Il
vérifie que la référence reste le PNG choisi et qu'aucune image de substitution
n'est créée.
Les E2E `asset_studio_entity_e2e` et `asset_studio_animation_e2e` exigent aussi
une capture OpenGL dédiée après composition/rechargement ; l’Entity utilise le
fixture réel `studio-beam` et montre trois blocs indépendants (texture, vecteur
et Beam), tandis que l’Animation utilise le même Beam avec deux clés de
position ; les fichiers
`asset-studio-entity-e2e.ppm` et `asset-studio-animation-e2e.ppm` sont inspectés
pour vérifier le résultat visuel, pas seulement le JSON sauvegardé. Le scénario
Entity exige en plus que l'action nominale d'animation et le transform soient
réellement rendus ; le scénario Animation exige le picker nominal de nœud et
une clé rapide, afin qu'un contrôle présent uniquement dans un volet avancé ne
puisse pas satisfaire la recette UX.
Le même E2E Entity initialise un seul état, clique `Add state from clip`, puis
utilise les coordonnées réelles de `Connect from here` et de la nouvelle carte
cible dans le canevas Animation Graph. Après sauvegarde/rechargement, il exige
la transition `idle-to-beam-scroll` et produit
`asset-studio-animation-graph-e2e.ppm` avec les deux cartes et leur flèche. Un
état ou une transition injectés par la session avant l'affichage ne satisfont
pas cette recette.
Le test `asset_studio_behavior_e2e` crée seulement la source IA, ouvre la
ressource Behavior, recherche puis ajoute `Emit event` depuis la palette, et
clique `Connect from output` puis la carte cible. Il sauvegarde et recharge,
exige la connexion `monster-ai-to-emit-event`, exécute le
signal IA `attack`, attend une action et conserve le canevas final dans
`asset-studio-behavior-graph-e2e.ppm`.
Le test `map_studio_mechanic_e2e` retire la liaison `platform.body` vers
`anchor.body` de la fixture, ouvre le canevas Mechanics et la recrée par clics
sur `Connect from output` puis la carte cible. Le même scénario glisse ensuite
le capteur `presence` d'une unité monde dans le canevas spatial. Il sauvegarde,
recharge, exige la connexion et la nouvelle position, compile la simulation,
avance un pas fixe et produit
`map-studio-mechanic-graph-e2e.ppm`. Le long formulaire de preset reste replié
par défaut afin que le graphe soit visible sans scroll préalable.
Le test `asset_studio_entity_animation_workflow_e2e` ne prépare aucun document
Entity ou Animation. Il part du composant visuel `beam`, clique
`Create Entity from visual`, dépose `button-primary` comme enfant, déplace cet
enfant au gizmo, puis clique `Animate selected node...`, `Create animation` et
`Key Position`. Il active `Auto-key`, déplace le playhead à travers son contrôle
et fait glisser le gizmo pour créer la seconde pose. Il lance puis suspend la
lecture, déplace le second losange sur la Timeline et ajoute un événement au
playhead par l'action rapide. Après sauvegarde/rechargement, il exige la cible
Entity, l'enfant avec son parent/drawable/transform, le binding du clip vers cet
enfant, deux clés Position, le temps corrigé et le marqueur au même temps. Son
artefact JSON distingue chaque widget vu, chaque création par clic, l'avancée
effective de la lecture et les données persistées ; une capture PPM conserve le
workspace final.
Le workflow `platform-studio.yml` exécute l’ensemble CTest sur une matrice
macOS/Windows/Linux et archive les artefacts UI et d’échec de chaque runner ;
son runner Linux installe les headers X11, Wayland, GTK3 et audio nécessaires
à SDL2 et au dialogue natif avant la configuration CMake.
la gate multiplateforme n’est fermée qu’après un run vert de cette matrice.
Le test `asset_studio_ui_overrides_e2e` prépare un override d’instance, rend la
modale de perte avec ses deux actions, vérifie l’annulation sans perte puis la
confirmation avec suppression de l’instance dans
`asset-studio-ui-overrides.json`.
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
répétition, UV et transform). Le preset `beam` de la fixture
`tests/fixtures/studio-beam` combine ce ruban texturé avec un calque vectoriel
de bordure ; `fabric_visual_composition_renderer_tests` vérifie les deux
packets et les propriétés du contour, et le résultat reste visible dans le
parcours des presets.
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
conservation byte-for-byte de la source PNG. Les fixtures JSON sont comparées
après parsing avec tolérance numérique et les PNG vérifient taille et en-tête,
tandis qu'Asset Studio et
Preview Runtime
utilisent le même constructeur de packet raster ; un test d'intégration compare
le packet runtime au packet studio attendu et le smoke OpenGL vérifie le pixel
de référence réellement échantillonné après crop.
`VisualComposition v1` couvre le round-trip strict des quatre genres de
calques, les références typées, identifiants stables, ancrages, transforms,
opacité, Z, publication atomique et refus des champs inconnus. Le validateur
headless résout ses dépendances et vérifie chaque crop local contre les
dimensions de la texture référencée. Les fixtures JSON générées par Studio
sont comparées après parsing pour neutraliser les différences de formatage
entre compilateurs ; les assets binaires restent comparés octet par octet.
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
ruban Beam, ainsi que la présence de sa bordure vectorielle, dans le resolver
partagé, puis le remplacement d'une échelle de composant sur une frame
réellement produite par Preview Runtime.
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
Les contrats `MaterialDefinition v2` et `EntityDefinition v4` couvrent
round-trip, migration des entités v1, publication atomique, références typées,
instances de composants visuels, transforms non finies, identifiants dupliqués
et cycles de parentage. Le validateur headless résout aussi variantes, ancres et
overrides des composants portés par les entités. Preview Runtime vérifie le
parcours map → entité → composant jusqu'aux draw packets transformés.
Le contrat `AnimationClip v3` couvre parseur strict, round-trip, publication
atomique, bindings stables, valeurs scalaire/Vec2/couleur/booléen/référence,
interpolations step/linear/cubic, boucle et rejet des valeurs non interpolables.
La pile modulaire d'effets de surface couvre le round-trip de plus de deux
blocs, leur ordre, leur activation, leurs couleurs et leurs bornes. Le smoke
OpenGL compare deux ordres de pile, refuse qu'ils produisent le même pixel et
écrit `build/test-artifacts/fabric-render-effect-stack-smoke.ppm`. Les E2E Beam et Button montrent
la pile directement dans l'inspecteur avec la preview texturée, puis vérifient
sa persistance après reload. Le test de session Button ajoute un quatrième
bloc depuis l'apparence référencée et vérifie son undo, son redo et chaque
rechargement disque intermédiaire.
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
