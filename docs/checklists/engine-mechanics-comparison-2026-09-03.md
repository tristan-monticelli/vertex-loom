# Audit des assets et comparaison multi-moteurs — 2026-09-03

## Portée et méthode

Audit statique du dépôt au 3 septembre 2026, contre-audité le 4 septembre 2026,
complété par les tests nommés
ci-dessous et par les documentations officielles du registre de sources.
`Implémenté` exige un contrat, un parcours d'authoring, une preview et une preuve
runtime publié. `Partiel` signifie qu'au moins une de ces surfaces manque.
`Contrat seulement`, `planifié`, `absent` et `non prouvé` ne sont jamais assimilés
à une livraison. Les comparaisons externes indiquent `✓`, `partiel`, `—` ou `N/A`.
Une inférence est préfixée `Inférence:`.

Les cinq concepts suivants ne sont pas interchangeables :

- chemin visuel texturé : `TexturedPath`, ruban rendu, UV et apparence ;
- rail de déplacement : trajectoire qui pilote la transform d'une instance ;
- spline géométrique : courbe éditable utilisée pour construire une géométrie ;
- contrainte physique : relation résolue entre corps par la simulation ;
- animation guidée par chemin : animation d'une transform ou de bones sur une courbe.

## Contre-audit UX et niveau de preuve — 2026-09-04

La première version du rapport confondait parfois trois faits différents : un
contrat sérialisable, un panneau qui affiche ce contrat et un parcours qu'un
utilisateur peut accomplir de bout en bout. La présence d'une capture PPM ne
prouve pas que les données visibles ont été créées par l'interface.

Le niveau de preuve utilisé désormais est :

- `L4` : action réelle dans l'UI, sauvegarde/rechargement, résultat visuel et
  runtime publié vérifiés ;
- `L3` : action réelle dans l'UI et sauvegarde vérifiées, sans parité publiée
  complète ;
- `L2` : panneau rendu, mais document préparé ou modifié par API interne ;
- `L1` : contrat, sérialisation ou runtime seulement ;
- `L0` : absent.

`Implémenté` exige `L4`. `Partiel` couvre `L2` ou `L3`. `Contrat seulement`
correspond à `L1`. Cette règle corrige les conclusions trop favorables du
3 septembre sans nier les capacités du runtime.

### Verdict d'utilisabilité

Le moteur sait réellement charger, valider, sauvegarder, prévisualiser et
exécuter son modèle 2D : textures/SVG, vectoriels, matériaux, chemins texturés,
compositions, entités, animations, input, behaviors, maps, scènes, mécaniques,
audio, replay et sauvegarde. Ses points les plus solides sont les contrats
typés, la sérialisation, le runtime déterministe, le rendu 2D et les tests
unitaires/intégration. Son principal déficit n'est donc pas l'absence de code
bas niveau, mais l'authoring : les capacités avancées sont souvent exposées par
des listes et formulaires techniques, et plusieurs E2E contournent ces panneaux.

| Tâche utilisateur | Ce qui existe réellement | Niveau | Verdict | Rupture principale | Priorité | Correction recommandée |
| --- | --- | --- | --- | --- | --- | --- |
| Créer une ressource visuelle | Menus nominaux Beam, Button, Artwork et Entity vide ; import PNG/SVG ; création technique de plusieurs autres types | L3 | utilisable | Map, Scene, Mechanic, Replay et Audio n'ont pas le même point d'entrée dans Asset Studio | P1 | Unifier `Créer…` avec catégories, recherche, description et ouverture du bon workspace |
| Composer une Entity | Création depuis un visuel puis ajout d'un enfant par drag, parentage, déplacement au gizmo, animation de l'enfant et reload sont prouvés dans un flux ; arbre récursif, multi-sélection et reparentage ont leurs E2E | L3 | utilisable | Réparation, overrides et compositions plus profondes restent répartis entre plusieurs E2E | P1 | Réunir ensuite réparation et override dans ce flux sans préparation API |
| Configurer rig, IK et déformation | Sélection ordonnée→création IK+cible, overlay et cible déplaçable prouvés ; une action nominale crée un quad de déformation valide compatible XPBD puis le recharge ; édition mesh/poids reste numérique | L3 pour IK, L2 pour mesh initial, L1/L2 sinon | partiel | IK et initialisation mesh utilisables ; pas de création de bones, édition directe du mesh ni peinture de poids comparable à Spine/Rive | P1 | Étendre le Stage Rig aux bones, mesh, poids et contraintes |
| Configurer XPBD | Action nominale tissu 4 points, overlay, solveur et reload réellement cliqués ; listes/formulaires avancés | L2 | partiel | Le preset valide est utilisable sans JSON ; pas de pose directe des particules/contraintes | P1 | Mode Physics sur canvas, liens manipulables et diagnostics locaux |
| Créer un clip d'animation | Entity→nouveau clip→première clé→auto-key→playhead→seconde pose→lecture/pause→correction→événement→reload→paquet→PreviewRuntime est prouvé ; runtime évalue le nœud et l'événement exacts | L4 | utilisable | Placement de la map hôte préparé par le harnais ; courbes avancées et geste multi-clés restent séparés | P1 | Fusionner ensuite le placement Map par geste et la preuve multi-clés |
| Éditer une machine à états | Canevas `Animation Graph`, choix de clip, ajout d'état sans ID, cartes, flèches et inspecteurs avancés | L3 | utilisable | Ajout et connexion sont prouvés par clics/reload ; erreurs locales, layout manuel et runtime publié restent à couvrir | P1 | Ajouter erreurs sur les arêtes, layout manuel et preuve Preview Runtime |
| Construire un Behavior Graph | Palette recherchable, cartes/ports/flèches, connexion, trace colorée, breakpoint éphémère, pause et pas-à-pas | L3 | utilisable | Le parcours UI est prouvé ; layout manuel, erreurs locales sur arêtes et debug du runtime publié manquent | P1 | Ajouter layout, diagnostic d'arête et attachement au Preview Runtime |
| Construire une mécanique | Instance Map sélectionnée→action contextuelle→preview paramétrée ; graphe et canevas spatial côte à côte ; déplacement, taille, rotation et joint directs ; retour Map avec overlay borné, paramètres spatiaux manipulables et double-clic vers le nœud exact ; E2E navigation+gestes→reload→package→runtime | L4 | utilisable | Le parcours principal direct est prouvé ; contraintes avancées et diagnostics locaux restent moins intégrés que les leaders | P1 | Généraliser la navigation diagnostic→nœud et les poignées de contrainte |
| Construire une map | Picker recherché, placement simple/continu, sélection simple/multiple/rectangle, déplacement groupé, duplication bouton/`Ctrl+D`/`Ctrl`+clic, snapping et poignées de polygone de collision ; deux placements continus sont prouvés par clics→reload | L3 | partiel | Overrides et relations restent form-heavy ; pas de tilemap, terrain, brush ni navigation | P1 | Prioriser ensuite tilemap, diagnostics locaux et édition directe des relations |
| Prévisualiser et publier | Workspace Publish Map/Scene : fermeture visible, destination, validation, publication et smoke du paquet exact ; E2E depuis Mechanics | L3 | partiel | Le parcours Studio→paquet→PreviewRuntime est prouvé ; lancement du binaire release, signature et distribution manquent | P0 | Étendre le même artefact au `game_runtime` release et au gate de distribution |

### Pourquoi les E2E Entity et Animation ne suffisent pas

Dans `editors/asset_studio/main.cpp`, `--e2e-entity` appelle directement
`set_selected_entity_node`, `add_selected_entity_node`,
`set_selected_entity_definition` et injecte encore la machine à états avant
d'afficher l'interface. En revanche, mesh et XPBD sont maintenant créés par
deux clics nominaux, validés ensemble et rechargés. `--e2e-animation` appelle directement
`create_animation` et `set_selected_animation_segment` avant d'activer le
gizmo. Ce test ciblé reste L2. Le parcours transversal
`--ui-entity-animation-workflow-test` crée en revanche l'Entity et le clip,
compose l'enfant, pose/corrige les clés et ajoute l'événement par gestes ; il
publie ensuite une map hôte, recharge le paquet exact et exige l'évaluation du
nœud et du marqueur par `PreviewRuntime`.

### Architecture de panneaux à atteindre

| Zone persistante | Entity | Animation | Behavior/Mécaniques |
| --- | --- | --- | --- |
| Explorateur gauche | Assets filtrables et drag source | Clips de l'Entity | Palette de nœuds/actions searchable |
| Hiérarchie | Nœuds, parenté, ordre, visibilité, lock | Même arbre avec pistes | Arbre logique et erreurs |
| Canvas central | Drop, sélection et gizmos | Pose évaluée et gizmos keyables | Nœuds, ports, liens et groupes manipulables |
| Inspecteur droit | Propriétés nominales ; avancé replié | Valeur, interpolation, easing | Propriétés du nœud/lien sélectionné, sans ID brut |
| Dock bas | Diagnostics/dépendances | Transport, dope sheet, marqueurs, courbes | Trace, breakpoints, événements et simulation |

Les fenêtres modales doivent rester limitées au nom, au template et aux
confirmations. La composition, le rig, les graphes et les relations doivent
rester visibles pendant l'édition et conserver sélection, zoom et contexte.

## Audit UX transversal et décision de refactoring — 2026-09-04

### Réponse directe

Un refactoring global de l'UX est nécessaire. Une réécriture globale ne l'est
pas. Les sessions métier, commandes undoables, validateurs, formats JSON et
renderers sont des fondations réutilisables ; le problème est leur orchestration
dans l'éditeur. La cible est fixée par
[ADR-0151](../decisions/ADR-0151-progressive-editor-ux-modularization.md) et ses
tranches sont détaillées dans le
[plan de refactoring UX](../plans/editor-ux-modularization-2026-09-04.md).

Les preuves structurelles sont mesurables au 4 septembre après extraction de
Behavior, Animation et Rig/Physics Entity : `asset_studio/main.cpp` contient
désormais 10 795 lignes et 1 365 appels
ImGui ; `map_studio/main.cpp` contient 3 335 lignes et 422 appels ImGui. Le
premier mélange encore bootstrap, état UI, plusieurs workspaces,
orchestration des sessions et E2E ; le second a déjà sorti Scene, Mechanics,
Publish, Resource Picker et le canvas Map. Asset Studio
force trois panneaux par coordonnées ; Map Studio compose encore trois enfants
pour la map, mais Map, Scene et Mechanics partagent la même surface et le même
historique de documents. Animation Graph occupe désormais le Task
Workspace contextuel de l'Entity ; Behavior Graph et Entity Transformation
occupent le Stage de leur document. Il n'existe ni
onglets de documents, ni historique retour/avant global, ni palette de
commandes, ni état de sélection transversal unique.

### Comparaison de réalisation UX

`Fort` signifie qu'une documentation officielle décrit le parcours comme une
surface nominale ; cela ne mesure ni la qualité esthétique ni la profondeur du
runtime. `N/A` conserve le périmètre spécialisé de Spine et Rive.

| Dimension UX | Vertex Loom réalisé | Godot | Unity | Unreal | GameMaker | Construct | Spine | Rive | Verdict |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Projet et ressources | arbre filtrable de 16 types, import et actions ; deux Studios séparés | Fort | Fort | Fort | Fort | Fort | N/A | partiel | Regrouper la navigation et router vers le bon workspace, sans fusionner les contrats |
| Hiérarchie → canvas → inspecteur | cohérent pour Entity ; partiel et dense pour Map | Fort | Fort | Fort | Fort | Fort | Fort | Fort | Convergence externe claire ; une sélection stable partagée est P0 |
| Composition Entity/prefab | drag, parentage, multi-sélection et gizmo prouvés | Fort | Fort | Fort | partiel | Fort | partiel | partiel | Parcours nominal utilisable ; variantes, overrides et réparation doivent rester contextuels |
| Animation de propriétés | deux poses, auto-key, playhead, box-select, déplacement/scale multi-clés, courbes et événement | Fort | Fort | Fort | Fort | partiel | Fort | Fort | Bonne base ; la preuve publiée du flux auteur manque |
| Machine d'états et logique | graphes Animation, Behavior et Mechanic visibles ; debug faible | Fort | Fort | Fort | partiel | Fort | N/A | Fort | Ajouter recherche contextuelle, erreurs sur liens, trace et breakpoints avant plus de nœuds |
| Construction de niveau | picker recherché, placement continu, multi-sélection, duplication, calques, snapping et points de collision directs ; overrides/triggers denses | Fort | Fort | Fort | Fort | Fort | N/A | N/A | Les bases objet ne sont plus l'écart ; restent tilemap/terrain/navigation et édition directe des joints |
| Rig, IK, mesh et poids | contrats/solveurs et inspecteurs numériques | Fort | Fort | Fort | partiel | partiel | Fort | Fort | Le runtime précède fortement l'authoring ; workspace canvas dédié requis |
| Preview, debug et publication | preview/package/tests disponibles mais parcours auteur publié incomplet | Fort | Fort | Fort | Fort | Fort | preview/export | preview/export | Unifier Play/Pause/Step, diagnostics cliquables et preuve UI→package→runtime |
| Adaptation de l'espace | large/minimum testés ; positions forcées et préférences non persistées | Fort | Fort | Fort | Fort | Fort | Fort | Fort | Shell adaptatif et préférences locales nécessaires ; éviter la configurabilité illimitée au départ |
| Découverte et commandes | recherche de ressources et palettes de graphes ; actions dispersées | Fort | Fort | Fort | Fort | Fort | Fort | Fort | Registre d'actions et palette de commandes partagés, mêmes raccourcis et raisons de blocage |
| Accessibilité | navigation clavier et contraste testés ; libellés mixtes anglais/français | partiel | partiel | partiel | partiel | partiel | partiel | partiel | La preuve actuelle est minimale ; focus, ordre, mise à l'échelle et localisation restent à couvrir |
| Architecture de l'éditeur | deux grands points d'entrée et état statique local | docks/plugins documentés | fenêtres dockables documentées | éditeurs/modules/plugins documentés | interne non prouvé | vues/bars/addons documentés | interne non prouvé | interne non prouvé | Refactor Vertex Loom requis ; ne pas inférer l'architecture des outils propriétaires depuis leur apparence |

Sources de cette synthèse : G9/G16–G18, U8/U15–U17, E9/E17–E19,
GM4/GM5/GM8/GM9, C6/C8/C10/C11, S4–S6 et R4–R6.

### Parcours complets à accepter

| Parcours | État actuel | Condition de sortie |
| --- | --- | --- |
| Projet → import → visuel → Entity | L3 | mêmes actions dans un navigateur partagé, retour/avant et reload sans perte de sélection |
| Entity → enfant → clip → deux poses → événement | L4 pour l'animation publiée | remplacer la map hôte du harnais par un placement Map Studio dans le même flux et étendre la preuve graphique aux gestes multi-clés |
| Entity → Animation Graph → Behavior | L3 | conserver l'Entity et le nœud sélectionnés, montrer la trace et revenir à la propriété fautive |
| Map → placement → collision/trigger → mécanique | L3 pour le placement, L2/L3 ensuite | étendre le même E2E aux collisions/triggers, puis sélection unique entre canvas, inspecteur et graphe et joints manipulables sur la map |
| Map → Scene → campagne → Publish | L2 | onglets/historique partagés, dépendances visibles, validation cliquable et runtime de release lancé |
| Entity → Rig/IK/XPBD → Animation | L2/L3 pour IK et presets mesh/XPBD, L1/L2 sinon | création de bones, édition mesh/poids et contraintes sur canvas, preview puis reload sans préparation API |
| Erreur → diagnostic → réparation | L1/L2 | chaque erreur ouvre le document, sélectionne l'objet et focalise le champ ou handle concerné |

### Ordre de refactoring retenu

1. `P0 — fondation sans changement visuel` : extraire état UI, actions,
   diagnostics et widgets partagés ; interdire de nouveaux éditeurs complets
   dans les deux `main.cpp`.
2. `P0 — continuité` : shell commun, onglets/historique, sélection par ID,
   Resource Browser, Inspector et palette de commandes ; E2E de navigation.
3. `P0 — production` : porter Entity/Animation/Logic, puis Map/Scene ; ajouter
   édition directe des mécaniques et preuve UI→package→runtime.
4. `P1 — outils spécialisés` : Rig/IK/XPBD, tilemap/navigation, Audio, Replay
   et debug visuel sur les mêmes composants.
5. `P2 — extension` : plugins, collaboration et personnalisation poussée après
   stabilisation des frontières du shell.

Ce plan reprend les principes convergents des autres moteurs sans importer leur
complexité générale. Chaque tranche doit garder les E2E existants verts et ne
modifier aucun schéma persistant.

## Tableau maître

| Domaine | Capacité | Preuve Vertex Loom | Studio | Preview | Runtime publié | État | Godot | Unity | Unreal | GameMaker | Construct | Spine | Rive | Écart/impact | Priorité | Recommandation | Sources officielles |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Projet | A01 — manifeste, création, chemins sûrs, sauvegarde atomique | `manifest.cpp`, `document_storage.cpp`, `project_tests`, ADR-0006/0009/0013 | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | N/A | Pas d'écart critique prouvé | P2 | Conserver les fixtures de traversée et crash-recovery | G1, C1 |
| Projet | A02 — registre typé et fermeture transitive des dépendances | `resource_registry.cpp`, `map_package.cpp`, ADR-0019/0141 | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | partiel | ✓ | partiel | partiel | Diagnostic de cycle moins visuel que les grands moteurs | P2 | Afficher le chemin complet du cycle dans les Studios | G1, U1, E1 |
| Import | A03 — PNG intact, crop non destructif, alpha | `texture_asset.cpp`, `raster_view`, `asset_studio_texture_e2e` | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | partiel | partiel | Fidélité couverte, mais recette GPU publique encore externe | P1 | Garder une fixture RGBA asymétrique dans la gate graphique | G1, U1, E1 |
| Import | A04 — SVG lié puis conversion vectorielle native | `svg_vector.cpp`, ADR-0016/0036, tests SVG | oui | oui | oui | implémenté | ✓ | partiel | partiel | partiel | ✓ | ✓ | ✓ | Pas de réimport différentiel documenté | P2 | Ajouter un diff de réimport lié | G1, U1, E1 |
| Surfaces | A05 — Material v2, blend, texture/vector pattern, UV | `material.hpp`, `material_entity_tests`, ADR-0038/0138 | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | partiel | Bibliothèque de matériaux limitée | P2 | Ajouter duplication et aperçu comparatif | G1, U1, E1 |
| Surfaces | A06 — pile Tint/Holography/Shine ordonnée | `shader_profile.hpp`, `opengl_vector_smoke`, ADR-0143 | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | partiel | ✓ | N/A | partiel | Effets limités à trois familles | P2 | N'ajouter un effet qu'avec contrat, UI et test pixel | G1, U1, E1, R2 |
| Surfaces | A07 — source intacte par défaut, recoloration explicite | `BeamColorMode`, tests presets, E2E Beam/Button et `opengl_vector_smoke` ; fixture asymétrique rouge/verte, comparaison pixel-à-pixel des deux côtés, intensités zéro et variations de couleurs mesurées ; ADR-0136/0138 | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | ✓ | Défaut bleu/reflet corrigé, anciens JSON inchangés et invariance des pixels source prouvée par le smoke GL réel | P0 | Conserver la fixture asymétrique et le gate GL sans `SKIP`; étendre la même matrice au paquet publié | G1, U1, E1, R2 |
| Vecteur | A08 — formes, fills, strokes, clips, hiérarchie et Bézier | `vector_asset.hpp`, ADR-0022..0035/0128, canvas E2E | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | partiel | ✓ | ✓ | ✓ | Opérations booléennes non prouvées | P2 | Documenter ou ajouter les booléens si requis par un projet | G1, U1, E1, S1, R1 |
| Chemins | A09 — spline géométrique ligne/cubique, ouverte/fermée | `TexturedPathCommandKind`, `textured_path_geometry_tests` | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | partiel | ✓ | ✓ | Pas de subdivision utilisateur | P2 | Exposer la tolérance uniquement avec budget perf | G2, U2, E2, S2 |
| Chemins | A10 — chemin visuel texturé repeat/mirror/stretch | `TexturedPathUvMode`, tests UV et OpenGL | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | partiel | partiel | N/A | N/A | Couverture graphique de toutes variantes à consolider | P1 | Matrice pixel repeat/mirror/stretch dans la recette GL | G2, U2 |
| Chemins | A11 — largeur, profil, joins, caps, orientation, transparence | `width_profile`, `join`, `cap`, `opacity`, six captures join/cap | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | partiel | partiel | N/A | N/A | Recette publiée non isolée pour chaque combinaison | P1 | Fixture asymétrique et probes par mode | G2, U2 |
| Déplacement | A12 — rail de déplacement d'instance | `MapInstance.pathFollower` est éditable dans l'Inspector Map, chargé et évalué à chaque pas fixe du Preview Runtime ; le test recharge aussi le paquet exporté et vérifie le document de chemin | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | partiel | Le parcours création → Preview → paquet est couvert ; il manque seulement le geste E2E qui configure le rail dans l'interface | P1 | Ajouter un scénario UI qui configure le rail puis recharge le paquet exact | G2, U2, E2, S2 |
| Animation | A13 — animation guidée par chemin | `path_follower.hpp/.cpp` fournit échantillonnage position+tangente et progression vitesse/boucle ; aucun binding Map/Animation persistant | non | non | non | contrat seulement | ✓ | ✓ | ✓ | partiel | partiel | ✓ | partiel | La brique géométrique est prête, mais les courbes visuelles ne pilotent encore aucune instance | P1 | Ajouter le binding `progress`, orientation optionnelle et raccord Map/Preview Runtime | G2, U2, E2, S2 |
| Composition | A14 — compositions, composants, paramètres, variantes, ancres | `visual_composition.hpp`, `visual_component.hpp`, ADR-0104/0105 | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | partiel | ✓ | Pas de bibliothèque de variantes visuelle avancée | P2 | Conserver le résolveur commun et améliorer seulement l'UX | G1, U1, E1, R1 |
| Entités | A15 — entité/prefab, arbre et overrides d'instance | `entity.hpp` ; un flux UI crée l'Entity, dépose et sélectionne un enfant, le déplace, l'anime et recharge ; arbre récursif, drag/reparentage et multi-sélection testés | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | partiel | partiel | Création de hiérarchie nominale solide ; réparation et overrides restent dans des scénarios séparés | P1 | Intégrer réparation et override au workflow transversal | G1, U1, E1, G8, U8, C6 |
| Entités | A16 — transformation atomique A→B avec transfert d'état | `entity_transformation.hpp`, ADR-0115, tests runtime | oui | oui | oui | implémenté | partiel | partiel | partiel | partiel | partiel | N/A | N/A | Capacité spécialisée différenciante | P2 | Garder politiques versionnées et replay déterministe | G1, U1, E1 |
| Animation | A17 — clips, clés, interpolation, easing, segments, événements | `animation.hpp` v4 ; le flux UI crée deux poses, lit/pause, corrige la seconde clé et ajoute un événement, puis le paquet exact est chargé : PreviewRuntime évalue le nœud enfant et le marqueur ; `AnimationTimeline` déplace et scale atomiquement une sélection | oui | oui | oui | partiel | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | Parcours publié L4 ; courbes avancées, placement Map par geste et scale multi-clés dans ce même E2E restent ouverts | P1 | Ajouter le placement Map et le geste multi-clés au scénario transversal | G3, U3, E3, GM1, S1, R1, G8, U8, E8, GM4, S4, R4 |
| Animation | A18 — machine à états d'animation | `animation_state_machine.hpp` ; le canevas choisit un clip, crée une carte sans ID puis relie les deux états par clics avant reload et capture OpenGL ; le workspace affiche la validation structurée avant preview | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | partiel | ✓ | partiel | ✓ | Ajout et connexion nominaux utilisables, diagnostics locaux visibles ; layout manuel et runtime publié restent à prouver | P1 | Ajouter layout manuel et test UI→Preview Runtime | G3, U3, E3, R1, R5 |
| UX Entity | A39 — composition contextuelle, arbre et édition directe | Le flux réel visuel→Entity dépose un enfant, conserve sa sélection, corrige son transform, l'anime et recharge ; arbre récursif, groupe et reparentage ont leurs E2E | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | partiel | partiel | Le chemin nominal multi-nœuds est prouvé ; réparation et overrides restent plus avancés | P1 | Ajouter les états de réparation locale et l'override contextuel au même parcours | G9, U8, E9, GM4, C6, S4, R4 |
| UX Animation | A40 — workspace timeline, création de piste et keying contextuel | Le flux transversal crée le clip et deux poses, lit/pause, déplace le second losange, ajoute un événement, recharge et prouve l'évaluation depuis le paquet ; box-select et `Alt`+glisser scalent atomiquement le groupe | oui | oui | oui | partiel | ✓ | ✓ | ✓ | ✓ | partiel | ✓ | ✓ | Timeline et sortie publiée utilisables ; le geste graphique de scale multi-clés reste dans un test séparé | P1 | Intégrer le scale multi-clés et le placement Map au même E2E | G8, U8, E8, GM4, C7, S4, R4 |
| UX Studio | A41 — coquille de panneaux et viewer pilotable | Project gauche, Viewer central, Inspector droit, Timeline basse ; Fit/zoom/grille/fond et probes graphiques normal/minimum ; ADR-0148 | oui | oui | N/A | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | partiel | partiel | La structure et le cadrage ne changent plus selon le type de ressource ; l'état du Viewer reste volontairement local au Studio | P2 | Maintenir le probe 900 × 600 et appliquer la même grammaire au shell Map Studio | G8, U8, E8, GM4, C6, S4, R4 |
| UX Map | A42 — hiérarchie, canvas et inspecteur de map | Layers/instances, canvas et inspecteur existent ; Map, Scene et Mechanics partagent la surface principale et la navigation de documents | partiel | oui | N/A | partiel | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | N/A | Navigation cohérente, mais composition complexe encore form-heavy et sans tilemap/navigation | P1 | Test de tâche réelle puis palette, duplication, tilemap et édition directe des relations | G8, U8, E8, GM4, C6 |
| Rig | A19 — bones, skinning pondéré et déformation mesh | `entity.hpp`, sérialisation et runtime ; action UI réelle créant un quad valide pondéré au root et compatible avec le tissu XPBD, puis sauvegarde/reload ; inspecteur numérique | partiel | oui | oui | partiel | ✓ | ✓ | ✓ | partiel | partiel | ✓ | ✓ | Base mesh utilisable, mais aucun rig canvas ni peinture de poids comparable à Spine/Rive | P1 | Workspace Rig visuel avec édition directe du mesh, bones, poids et preview de déformation | G4, U3, E3, S1, R1 |
| Rig | A20 — IK FABRIK et ordre des contraintes | Sélection root→tip, création atomique chaîne+cible, overlay os/cible et déplacement par gizmo ; E2E clic→reload | oui | oui | oui | partiel | ✓ | ✓ | ✓ | partiel | partiel | ✓ | ✓ | IK de base directement éditable ; ordre des contraintes et preview runtime publié restent à montrer | P1 | Ajouter reorder visuel, solve preview et preuve package→runtime | G4, U3, E3, S2, R1 |
| Physique | A21 — XPBD et substeps/interpolation | Solveur et overlay ; action nominale créant par clic un tissu 4 points avec les cinq familles, compatible avec le mesh, puis validation et reload | partiel | oui | oui | partiel | partiel | partiel | partiel | — | — | partiel | partiel | Preset guidé prouvé, mais pose directe des particules/contraintes absente | P1 | Éditeur Physics canvas avec poignées, liens et diagnostics locaux | G5, U4, E4, S1, R1 |
| Input | A22 — actions sémantiques, clavier/gamepad, remap | `input.hpp`, ADR-0055/0101/0119/0123, UI E2E | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | partiel | Touch/gestures non prouvés | P2 | Ajouter seulement avec cible plateforme | G1, U5, E5, C1 |
| Comportement | A23 — BehaviorGraph générique signaux/actions | `behavior_graph.hpp` ; palette, connexion, breakpoint, évaluation, pause et trace sur cartes prouvés par E2E, puis reload | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | partiel | ✓ | N/A | ✓ | Debug Studio utilisable ; runtime publié, layout manuel et diagnostics d'arête manquent | P1 | Relier la trace au Preview Runtime et ajouter erreurs locales sur arêtes | E10, C8, R5 |
| Maps | A24 — map, layers, verrouillage, ordre, snapping et instances | `map.hpp` ; Map Studio fournit picker recherché, placement simple/continu, sélection multiple/rectangle, déplacement groupé, duplication bouton/`Ctrl+D`/`Ctrl`+clic et poignées de collision ; l'E2E clique deux positions, exige IDs/contexte, sauvegarde et recharge | partiel | oui | oui | partiel | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | N/A | Les gestes objet de base sont L3 ; tilemap et le flux complet collision→mécanique→package restent incomplets | P1 | Étendre le scénario à collision/trigger/mécanique, puis introduire le tilemap sur cette grammaire | G6, U6, E6, GM1, C6, C12 |
| Maps | A25 — tileset/tilemap/autotiling/terrain | Aucun type Studio ou schéma tile | non | non | non | absent | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | N/A | Création de grands niveaux répétitifs coûteuse | P1 | ADR tilemap après stabilisation du placement actuel | G6, U6, E6, C3 |
| Physique | A26 — collisions éditables, triggers et payloads | ADR-0048/0077..0080/0113, Map Studio E2E | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | bounding box | listeners | Formes/filtrage avancé limités | P2 | Ajouter catégories/masques seulement avec diagnostics | G5, U4, E4, GM2, C4 |
| Mécaniques | A27 — nœuds body/pivot/joint/motor | `MechanicNodeKind`, compilateur/presets ; l'instance Map sélectionnée ouvre sa preview avec overrides ; déplacement, taille, rotation et couronne de joint ; l'overlay exact expose les paramètres spatiaux du prefab par gestes undoables et ouvre le nœud exact au double-clic ; E2E recharge, simule, publie et lance le paquet | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | N/A | Navigation, authoring, paramètres d'overlay et accès aux propriétés protégées sont directs | P2 | Étendre cette navigation aux diagnostics de compilation | G5, U4, E4, GM2, C4 |
| Mécaniques | A28 — nœuds sensor/constraint/event | contrats, compilateur Box2D et log ; capteur déplaçable sur canevas avec E2E glisser→reload ; connexions par graphe | partiel | oui | oui | partiel | ✓ | ✓ | ✓ | ✓ | ✓ | contraintes | listeners | Capteur spatial prouvé ; contraintes, observabilité et câblage événementiel restent techniques | P0 | Handles de contrainte, dernier signal visible et erreurs locales sur connexion | G5, U4, E4, S2, R2 |
| Scènes | A29 — scène, campagnes, transitions et map montée | `scene.hpp`, `SceneWorkspace` ; le harnais Studio prépare par session puis prouve publication→`SceneRuntimeSession` | partiel | oui | oui | partiel | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | N/A | Le paquet est réellement rechargé, mais l'authoring du test ne vient pas encore de gestes UI et il manque une vue de campagne | P1 | Ajouter aperçu de campagne, navigation explicite et gestes UI sans réintroduire une fenêtre isolée | G1, U1, E1 |
| Caméra | A30 — Camera2D follow, limites et réglages runtime | ADR-0064/0122, runtime tests | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | N/A | Blend multi-caméra absent | P2 | Ajouter seulement si scènes le demandent | G1, U1, E1 |
| Audio | A31 — événements audio, volume, loop et mixer PCM | `AudioDocument v2`, inspecteur d'une ressource existante, mixer/tests et smoke périphérique | partiel | oui | oui | partiel | ✓ | ✓ | ✓ | ✓ | ✓ | timeline | audio events | Pas de création/import audio unifié, waveform, bus visuel ni formats compressés | P1 | Import audio nominal, audition, waveform, bus et preuve UI→package | G1, U1, E1, GM3 |
| Replay | A32 — capture/relecture déterministe | `replay.hpp`, ADR-0057/0058 et tests runtime ; aucun parcours d'auteur identifié | non | oui | oui | contrat seulement | partiel | partiel | partiel | partiel | partiel | N/A | N/A | Fonction runtime sans outil Studio de capture, inspection ou comparaison | P1 | Panneau Replay avec enregistrer, lire, checksum et divergence par frame | G1, U1, E1 |
| Sauvegarde | A33 — progression et reprise | `progress_save.hpp`, ADR-0060/0061 | partiel | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | N/A | UX de slots non prouvée | P2 | Ajouter parcours utilisateur si plusieurs slots deviennent requis | G1, U1, E1, C1 |
| Performance | A34 — batching, culling, benchmark 60 FPS | Build Release natif Apple M1 Pro : renderer 795,993, runtime dense 125,812 et fixture textile 251,251 FPS p95 sur 600 frames ; rapport versionné, ADR-0066..0068 | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | métriques | métriques | Gate locale macOS prouvée sans skip ; Windows/Linux restent exigés séparément par la matrice de release | P2 | Conserver seuil 60 FPS, rapports natifs et refus des skips sur chaque plateforme | G1, U1, E1, S1 |
| Validation | A35 — validation unifiée, erreurs de champ, recovery | validateur CLI, diagnostics et recovery smoke | partiel | oui | oui | partiel | ✓ | ✓ | ✓ | ✓ | ✓ | partiel | partiel | CLI solide ; focus, réparation contextuelle et messages de plusieurs panneaux restent incomplets | P1 | Associer chaque erreur au panneau/champ/action de réparation | G1, U1, E1 |
| Publication | A36 — paquet map/campagne portable, dépendances, version runtime | Workspace Publish Map/Scene ; E2E Mechanics→validation→fermeture visible→paquet→PreviewRuntime smoke | oui | oui | oui | partiel | ✓ | ✓ | ✓ | ✓ | ✓ | export runtime | export `.riv` | Parcours Studio et paquet exact prouvés ; binaire release, licences et signature restent ouverts | P0 | Lancer le même artefact avec `game_runtime` release puis gate distribution | G7, U7, E7, C5, S3, R3 |
| Observabilité | A37 — logs JSONL, overlay, diagnostics render/physics | `TraceContext`, logs et tests ; pas de profiler intégré ni navigation complète log→objet | partiel | oui | oui | partiel | ✓ | ✓ | ✓ | partiel | partiel | métriques | listeners | Corrélation locale utile, diagnostic auteur inférieur aux profilers/remote debuggers des moteurs généralistes | P1 | Vue profiler frame, filtres, sélection de ressource et export de trace | G10, U10, E11 |
| Sécurité | A38 — validation imports, chemins relatifs, licences/SBOM | tests traversée, ADR-0144/0146 | oui | oui | oui | partiel | ✓ | ✓ | ✓ | partiel | partiel | export | export | Trois preuves de redistribution restent bloquantes | P0 | Ne pas publier avant statut `approved` réel | G7, U7, E7 |
| Programmation | A43 — scripting utilisateur et API gameplay | Aucun langage, fichier script, VM ou API de plugin chargé par le projet ; BehaviorGraph est fermé sur ses types compilés | non | non | non | absent | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | partiel | Tout nouveau comportement exige une modification et recompilation du moteur | P0 | Définir d'abord sandbox, déterminisme, version d'API et debug ; ne pas détourner les IDs de BehaviorGraph | G1, U1, E10, C8 |
| Extension | A44 — plugins éditeur, importeurs et outils personnalisés | Aucun contrat d'extension ou registre de plugin public recensé | non | non | non | absent | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | partiel | Le Studio ne peut pas être adapté à un pipeline projet sans fork | P1 | ADR d'extensions versionnées, permissions, isolation et compatibilité de schéma | G1, U1, E1, GM5, C1 |
| Navigation | A45 — navmesh/grille, pathfinding et agents | Aucun type navigation, bake, query ou agent runtime recensé | non | non | non | absent | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | N/A | Pas d'IA de déplacement ni validation de zones accessibles | P1 | Introduire navigation 2D indépendante de TexturedPath et du rail cinématique | G11, U11, E12, C2 |
| VFX | A46 — particules et éditeur d'effets | XPBD simule une déformation physique, pas un système de particules VFX ; aucun emitter/renderer VFX | non | non | non | absent | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | partiel | Fumée, impacts, pluie et effets de masse nécessitent des entités manuelles | P1 | Contrat emitter 2D borné, preview déterministe et budget overdraw | G12, U12, E13, GM5, C9 |
| UI jeu | A47 — widgets, layout, texte, focus et accessibilité | Les panneaux ImGui sont des outils Studio ; aucun document UI de jeu, layout responsive ou navigation focus runtime | non | non | non | absent | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | partiel | Menus, HUD et accessibilité ne sont pas authorables dans le produit | P0 | Définir arbre UI runtime, layout, texte, focus/input et preview multi-résolutions | G13, U9, E14, C1 |
| Internationalisation | A48 — polices, fallback, localisation et pseudo-locales | Aucun catalogue, locale, shaping/fallback ou import de police projet recensé | non | non | non | absent | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | partiel | Texte localisé et scripts complexes non pris en charge | P1 | Contrat texte/font/locales, fallback déterministe et pseudo-localisation en Studio | G15, U14, E16 |
| Réseau | A49 — multiplayer, réplication et transport | Aucun protocole, session, réplication ou test réseau recensé | non | non | non | absent | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | N/A | Moteur limité aux expériences locales | P2 | Ne planifier qu'après besoin produit ; séparer transport, autorité et déterminisme | G14, U13, E15, GM6 |
| Debug | A50 — debugger gameplay, breakpoints et inspection live | Behavior Studio fournit breakpoint éphémère, pause et pas-à-pas de trace ; logs/overlays ailleurs | partiel | partiel | partiel | partiel | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | partiel | Le debug Behavior local est prouvé, mais pas l'inspection du monde ni le runtime publié | P0 | Relier breakpoint/trace au Preview Runtime, puis inspection monde et frame-step | G10, U10, E11, GM7, C8 |
| Collaboration | A51 — source control, diff/merge sémantique et verrouillage | JSON textuel diffable et écritures atomiques ; aucune intégration VCS ni merge sémantique | non | N/A | N/A | partiel | ✓ | ✓ | ✓ | partiel | partiel | N/A | N/A | Conflits sur gros documents et assets binaires non assistés | P2 | Garder les formats stables puis ajouter diff sémantique avant intégration VCS | G1, U1, E1 |
| Plateformes | A52 — matrice d'export et certification | CMake/builds natifs et package interne ; preuve release complète limitée au macOS local documenté | partiel | partiel | partiel | non prouvé | ✓ | ✓ | ✓ | ✓ | ✓ | runtimes ciblés | runtimes ciblés | Portabilité de code ne vaut pas validation Windows/Linux/mobile/web/console | P0 | Matrice CI réelle par cible, smoke GPU/audio/input et artefacts signés | G7, U7, E7, C5, S3, R3 |
| 3D | A53 — scène, rendu, animation et physique 3D | Périmètre architectural explicitement 2D ; aucun contrat 3D | non | non | non | absent | ✓ | ✓ | ✓ | partiel | — | N/A | N/A | Pas un défaut pour le périmètre 2D actuel ; bloque seulement un futur produit 3D | P2 | Conserver hors périmètre tant qu'un besoin produit ne justifie pas un second moteur | G1, U1, E1 |
| UX globale | A54 — navigation unifiée, documents, retour/avant et restauration du contexte | Map Studio possède onglets/historique communs ; l'E2E part d'une instance sélectionnée, ouvre sa mécanique résolue et conserve la Map dans l'historique ; Asset Studio reste une application séparée | partiel | N/A | N/A | partiel | ✓ | ✓ | ✓ | ✓ | ✓ | partiel | partiel | Map→Mechanics est prouvé, mais le trajet Entity/Animation→Map traverse encore deux shells | P0 | Unifier le shell Asset/Map puis prouver le retour complet avec sélection, zoom et playhead | G16, U15, E17, GM8, C10, R6 |
| UX globale | A55 — sélection logique synchronisée entre arbre, canvas, timeline, graphe et inspecteur | Behavior est stable par ID ; `EditorContext` conserve pour Entity et les instances Map le primaire et la sélection multiple ; Scene suit montages et transitions par leurs IDs ; collisions et autres graphes gardent encore des index locaux | partiel | N/A | N/A | partiel | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | Entity, instances Map et objets Scene ne dérivent plus après réorganisation ; les collisions sans ID et le propriétaire transversal restent incomplets | P0 | Donner une identité stable aux objets Map restants puis porter la sélection Scene dans le contexte commun | G9, U15, E9, GM8, C6, S5, R6 |
| UX globale | A56 — registre d'actions et palette de commandes contextuelle | Save, Undo/Redo, visuels→Entity, Entity→Animation, ouverture Animation Graph et création+attachement Behavior partagent registre, disponibilité et palette ; le picker Behavior reste visible en mode guidé | partiel | N/A | N/A | partiel | ✓ | ✓ | ✓ | ✓ | ✓ | partiel | partiel | Le parcours visuel→Entity→Animation/Logic devient découvrable et cohérent, mais publication et les autres créations n'ont pas encore toutes un propriétaire d'action unique | P0 | Migrer ensuite les actions métier par parcours et exiger une preuve bouton + palette + raison de blocage | G16, U15, E17, GM8, C10, S5, R6 |
| Inspecteur | A57 — divulgation progressive, recherche, multi-édition, revert et favoris | Sections repliables et quelques pickers ; pas de recherche globale de propriétés, favoris ou revert uniforme ; multi-édition limitée | partiel | N/A | N/A | partiel | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | Les fonctions avancées restent longues à trouver et les écarts aux défauts sont peu visibles | P1 | Inspector partagé, recherche, propriétés modifiées, revert et édition commune de sélection | G9, U15, E9, GM8, C6, S5, R6 |
| UX globale | A58 — layout adaptatif, panneaux repliables et préférences locales | Tailles minimale testées ; positions forcées, largeurs statiques en mémoire et fenêtres auxiliaires | partiel | N/A | N/A | partiel | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | Le canvas Map devient trop petit et l'espace n'est pas restauré entre sessions | P1 | Deux layouts bornés large/compact puis persistance locale ; garder le Stage prioritaire | G16, U15, E18, GM8, C10, S5, R6 |
| Outils canvas | A59 — modes contextuels et manipulation directe des relations | Gizmos Entity/vectoriels, IK chaîne+cible, placement Map et déplacement/taille/rotation/joint Mechanics ; le mesh possède une initialisation nominale valide, mais mesh, poids, XPBD et contraintes restent sans manipulation directe | partiel | oui | oui | partiel | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | IK, initialisation mesh et transformations Mechanics sont prouvés, mais plusieurs systèmes avancés restent derrière des formulaires | P0 | Édition mesh/poids et Physics canvas, puis poignées de contrainte | G6, U8, E18, GM9, C6, S6, R6 |
| Debug UX | A60 — diagnostics cliquables, trace, breakpoints et inspection live | Behavior colore la trace et fournit breakpoint/pause/step éphémères ; erreurs et overlays des autres workspaces restent textuels | partiel | partiel | partiel | partiel | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | partiel | Première boucle de debug prouvée ; navigation diagnostic→objet et inspection live globale manquent | P0 | Généraliser cible navigable et brancher la trace Behavior au Preview Runtime | G10, U10, E11, GM7, C8, R5 |
| Architecture UX | A61 — shell et workspaces modulaires testables | Map, Mechanics, Behavior, Animation, publication Animation et toutes les surfaces Entity possèdent leurs modules ; Visual Component, le panneau de layers Visual Composition, le panneau Pen Textured Path, l’inspecteur Raster View, le canvas Raster Crop et le mode couleur partagé possèdent désormais leur état/gestes, tandis que la pile d’effets avancée et l’animation de texture restent dans `asset_studio/main.cpp` | partiel | N/A | N/A | partiel | ✓ | ✓ | ✓ | non prouvé | partiel | non prouvé | non prouvé | Le parcours Entity et six sous-surfaces Visual sont isolés du shell ; pile avancée, preview Textured Path et plusieurs harnais restent couplés | P0 | Extraire ensuite la pile d’effets avancée et l’animation Textured Path avec des callbacks communs | G18, U17, E19, GM5, C11, S5, R6 |
| Accessibilité UX | A62 — focus, clavier, échelle et langue cohérente | contraste et navigation ImGui testés ; focus partiel ; libellés français/anglais mélangés et aucune localisation du Studio | partiel | N/A | N/A | partiel | ✓ | ✓ | ✓ | ✓ | ✓ | partiel | partiel | Utilisation clavier et compréhension inégales selon panneau et taille d'écran | P1 | Catalogue de libellés, ordre de focus, échelle UI et scénarios clavier par parcours | G9, U15, E9, GM8, C10, S5, R6 |

## Couverture exhaustive interne

### 17 types de ressources

| Type recensé | Lignes |
| --- | --- |
| projet/manifeste | A01, A02, A30, A38 |
| texture | A03, A07, A10 |
| vector | A04, A08 |
| material | A05, A06, A07 |
| entity | A15, A16, A19, A21, A39 |
| animation | A17, A18, A40 |
| input | A22 |
| behavior | A23 |
| transformation | A16 |
| texturedPath | A09, A10, A11 |
| visualComposition | A14 |
| visualComponent | A14 |
| map | A24, A25, A26 |
| scene | A29 |
| mechanic | A27, A28 |
| replay | A32 |
| audio | A31 |

Le compte est dérivé de `StudioResourceKind` (16 valeurs) plus le manifeste
projet. Le menu `draw_kind` expose bien les 16 valeurs de l'explorateur.

### Nœuds, enums et paramètres publics

- Nœuds mécaniques : `body`, `pivot`, `joint`, `motor` → A27 ; `sensor`,
  `constraint`, `event` → A28.
- Valeurs mécaniques : booléen, entier, scalaire, texte, vec2, ressource et
  handles body/pivot/joint → A27/A28.
- TexturedPath : commandes move/line/cubic, closed → A09 ; texture,
  repeat/mirror/stretch, `uvScale`, `uvOffset`, `textureMetrics` → A10 ; width,
  widthProfile, color, opacity, miter/round/bevel, butt/round/square,
  miterLimit → A11.
- Shader/Material : profile, classification, primary/effect color, intensity,
  repetition, tint/holography/shine enabled/color/amount/patternScale, opacity,
  blend, texture, vectorPattern, UV transform → A05/A06/A07.
- VisualComponent : scalar, angle, integer, boolean, text, vec2, color,
  resource, bounds, anchors, variants, defaults, overrides, animatable → A14.
- Entity/animation/rig : node transform/drawable/material, prefab overrides,
  clips, keys, easing, interpolation, composition, markers, state transitions,
  bones, weights, IK/constraints, XPBD/substeps → A15/A17/A18/A19/A20/A21.
- Map/runtime : layers, order, locks, prefab/entity overrides, collision,
  triggers/payload, scene transition, camera follow/limits, audio volume/loop,
  input bindings, replay/progress → A22/A24/A26/A29/A30/A31/A32/A33.
- Pipeline : create/import, inspect, save/reload/recovery, preview, validate,
  package, published runtime, benchmark/logging → A01/A03/A35/A36/A34/A37.
- Capacités transverses comparées : scripting/extensions → A43/A44 ; navigation
  → A45 ; VFX → A46 ; UI/localisation → A47/A48 ; réseau → A49 ; debug → A50 ;
  collaboration → A51 ; plateformes/3D → A52/A53 ; navigation et sélection UX,
  actions, inspecteur, layout, outils canvas, debug, architecture et
  accessibilité des Studios → A54–A62.

### Groupes d'ADR

| Groupe | ADR | Lignes |
| --- | --- | --- |
| Gouvernance, format, validation, stockage | 0001–0020 | A01–A04, A35, A37 |
| Vecteur et rendu | 0022–0037 | A04, A08 |
| Matériaux, animation, rig | 0038–0046 | A05–A07, A17–A20 |
| Map, runtime, input, replay, audio, caméra, perf | 0047–0085 | A22, A24–A34 |
| Prompts, entités, preview, simulation | 0086–0103 | A14–A21, A24–A28 |
| Composition, chemins, comportements, publication | 0104–0135 | A09–A16, A22–A23, A29, A31, A36 |
| Beam/Button/surfaces/release/UX | 0136–0150 | A06–A07, A10–A11, A21, A34, A36–A42 |
| Modularisation progressive des Studios | 0151 | A54–A62 |

## Registre affirmation → source officielle

Consulté les 3 et 4 septembre 2026. Les capacités cochées signifient que la
documentation officielle décrit une capacité comparable, pas une équivalence
de qualité ou de format.

| ID | Affirmation utilisée | Source officielle |
| --- | --- | --- |
| G1 | Godot 4 possède un pipeline 2D intégré (rendu, physique, animation, ressources). | [Godot — 2D](https://docs.godotengine.org/en/stable/tutorials/2d/index.html) |
| G2 | `PathFollow2D` échantillonne un `Path2D`, expose progression et rotation. | [Godot — PathFollow2D](https://docs.godotengine.org/en/stable/classes/class_pathfollow2d.html) |
| G3 | AnimationTree fournit des machines/blends autour d'animations. | [Godot — AnimationTree](https://docs.godotengine.org/en/stable/tutorials/animation/animation_tree.html) |
| G4 | Godot documente skeletons 2D, bones et déformation. | [Godot — 2D skeletons](https://docs.godotengine.org/en/stable/tutorials/animation/2d_skeletons.html) |
| G5 | Godot fournit corps, collisions, joints et moteur physique 2D. | [Godot — Physics introduction](https://docs.godotengine.org/en/stable/tutorials/physics/physics_introduction.html) |
| G6 | TileMapLayer gère calques, collisions, navigation, terrains et motifs. | [Godot — Using TileMaps](https://docs.godotengine.org/en/stable/tutorials/2d/using_tilemaps.html) |
| G7 | Godot exporte projets et ressources vers des paquets exécutables. | [Godot — Exporting projects](https://docs.godotengine.org/en/stable/tutorials/export/exporting_projects.html) |
| U1 | Unity 6 organise scènes, GameObjects, composants, assets et prefabs. | [Unity 6 — Manual](https://docs.unity3d.com/6000.0/Documentation/Manual/index.html) |
| U2 | SpriteShape fournit outil et runtime pour mondes 2D fondés sur splines. | [Unity 6 — 2D SpriteShape](https://docs.unity3d.com/6000.0/Documentation/Manual/com.unity.2d.spriteshape.html) |
| U3 | Unity fournit animation, Animator et 2D Animation. | [Unity 6 — Animation](https://docs.unity3d.com/6000.0/Documentation/Manual/AnimationSection.html) |
| U4 | Unity expose physique et joints 2D. | [Unity 6 — Physics 2D](https://docs.unity3d.com/6000.0/Documentation/Manual/Physics2D.html) |
| U5 | Input System sépare actions et périphériques. | [Unity Input System](https://docs.unity3d.com/Packages/com.unity.inputsystem@1.11/manual/Actions.html) |
| U6 | Tilemap permet de créer des niveaux 2D à base de tuiles. | [Unity 6 — Tilemaps](https://docs.unity3d.com/6000.0/Documentation/Manual/tilemaps/tilemaps.html) |
| U7 | Build Profiles configurent et produisent les builds joueurs. | [Unity 6 — Build Profiles](https://docs.unity3d.com/6000.0/Documentation/Manual/build-profiles.html) |
| E1 | Unreal organise assets, Actors, Components et Blueprints. | [Unreal Engine — Get Started](https://dev.epicgames.com/documentation/en-us/unreal-engine/get-started) |
| E2 | Unreal permet aux acteurs/components de suivre une spline. | [Unreal Engine — Blueprint Spline Components](https://dev.epicgames.com/documentation/en-us/unreal-engine/blueprint-spline-components-overview-in-unreal-engine) |
| E3 | Control Rig crée des rigs et anime des contrôles dans Sequencer. | [Unreal Engine — Control Rig Quick Start](https://dev.epicgames.com/documentation/en-us/unreal-engine/how-to-create-control-rigs-in-unreal-engine) |
| E4 | Unreal documente moteur physique, collisions et contraintes. | [Unreal Engine — Physics](https://dev.epicgames.com/documentation/en-us/unreal-engine/physics-in-unreal-engine) |
| E5 | Enhanced Input repose sur actions et contextes de mapping. | [Unreal Engine — Enhanced Input](https://dev.epicgames.com/documentation/en-us/unreal-engine/enhanced-input-in-unreal-engine) |
| E6 | Paper 2D fournit sprites, flipbooks et tile maps. | [Unreal Engine — Paper 2D](https://dev.epicgames.com/documentation/en-us/unreal-engine/paper-2d-in-unreal-engine) |
| E7 | Unreal documente packaging et publication par plateforme. | [Unreal Engine — Packaging](https://dev.epicgames.com/documentation/en-us/unreal-engine/packaging-your-project) |
| GM1 | GameMaker Sequences combine objets, instances, tracks, clés et événements. | [GameMaker — Sequences](https://manual.gamemaker.io/monthly/en/GameMaker_Language/GML_Reference/Asset_Management/Sequences/Sequences.htm) |
| GM2 | GameMaker intègre Box2D avec fixtures, joints, collisions et debug. | [GameMaker — Physics](https://manual.gamemaker.io/monthly/en/GameMaker_Language/GML_Reference/Physics/Physics.htm) |
| GM3 | Les Sequences acceptent pistes son, emitters et effets. | [GameMaker — Sound in Sequences](https://manual.gamemaker.io/monthly/en/The_Asset_Editors/Sequence_Properties/Sound_in_Sequences.htm) |
| C1 | Construct 3 documente projets, objets, behaviors, événements et export. | [Construct 3 — Manual](https://www.construct.net/en/make-games/manuals/construct-3) |
| C2 | Pathfinding calcule et peut suivre une liste de nœuds. | [Construct 3 — Pathfinding](https://www.construct.net/en/make-games/manuals/construct-3/behavior-reference/pathfinding) |
| C3 | Tilemap fournit une grille de tuiles éditable. | [Construct 3 — Tilemap](https://www.construct.net/en/make-games/manuals/construct-3/plugin-reference/tilemap) |
| C4 | Physics est fondé sur Box2D et expose joints/forces/collisions. | [Construct 3 — Physics](https://www.construct.net/en/make-games/manuals/construct-3/behavior-reference/physics) |
| C5 | Construct exporte vers plusieurs plateformes web/app. | [Construct 3 — Exporting projects](https://www.construct.net/en/make-games/manuals/construct-3/tips-and-guides/exporting) |
| S1 | Spine couvre bones, meshes, weights, skins, animations et événements. | [Spine — User Guide](https://esotericsoftware.com/spine-user-guide) |
| S2 | Les contraintes Spine sont ordonnées ; la path constraint translate/oriente des bones sur un path. | [Spine — Constraints](https://esotericsoftware.com/spine-constraints), [Path constraints](https://esotericsoftware.com/spine-path-constraints) |
| S3 | Spine exporte données runtime et atlas/textures. | [Spine — Export](https://esotericsoftware.com/spine-export) |
| R1 | Rive fournit artboards, bones, contraintes, clés et machines à états. | [Rive — Keys](https://rive.app/docs/editor/keys), [State machines](https://rive.app/docs/runtimes/state-machines) |
| R2 | Les runtimes Rive avancent les machines et exposent événements/data binding. | [Rive — State machine playback](https://rive.app/docs/runtimes/state-machines), [Events](https://rive.app/docs/runtimes/rive-events) |
| R3 | `.riv` est le format exporté consommé par les runtimes. | [Rive — Exporting](https://rive.app/docs/editor/exporting) |
| G8 | Le Scene dock conserve l'arbre et l'Inspector édite la sélection ; l'Animation Panel affiche pistes, playhead et clés, et une propriété peut créer sa piste et sa clé. | [Godot — Nodes and Scenes](https://docs.godotengine.org/en/stable/getting_started/step_by_step/nodes_and_scenes.html), [Godot — Animation features](https://docs.godotengine.org/en/stable/tutorials/animation/introduction.html) |
| U8 | Un Prefab se crée depuis la hiérarchie vers le Project ; l'Animation window enregistre les propriétés de l'objet sélectionné au playhead. | [Unity 6 — Creating Prefabs](https://docs.unity3d.com/6000.0/Documentation/Manual/CreatingPrefabs.html), [Unity 6 — Animating a GameObject](https://docs.unity3d.com/6000.0/Documentation/Manual/animeditor-AnimatingAGameObject.html) |
| E8 | Sequencer accepte un Actor sélectionné ou glissé, crée des pistes filtrées et peut ajouter piste et clé depuis une propriété ou un raccourci. | [Unreal — Sequencer Track List](https://dev.epicgames.com/documentation/en-us/unreal-engine/sequencer-track-list-in-unreal-engine), [Unreal — Keyframing](https://dev.epicgames.com/documentation/en-us/unreal-engine/creating-animation-keyframes-in-unreal-engine) |
| GM4 | Le Sequence Editor réunit Canvas, Track Panel et Dope Sheet ; un asset glissé crée sa piste et l'enregistrement ajoute les paramètres modifiés. | [GameMaker — Sequence Editor](https://manual.gamemaker.io/monthly/en/The_Asset_Editors/Sequences.htm), [Track Panel](https://manual.gamemaker.io/lts/en/The_Asset_Editors/Sequence_Properties/The_Track_Panel.htm) |
| C6 | Le Layout View autorise sélection, hiérarchie, drop et édition synchronisée dans la Properties Bar ; ses capacités d'animation restent plus spécialisées. | [Construct 3 — Layout View](https://www.construct.net/en/make-games/manuals/construct-3/interface/layout-view), [Interface](https://www.construct.net/en/make-games/manuals/construct-3/overview/the-interface) |
| S4 | Spine garde l'animation active, la hiérarchie et les clés visibles dans Tree, Graph ou Dopesheet en mode Animate. | [Spine — Keys and timeline](https://esotericsoftware.com/spine-keys) |
| R4 | Rive réserve en mode Animate une timeline avec liste des animations, playhead, durée, snap, pistes filtrables et contrôles de lecture. | [Rive — Timeline](https://rive.app/docs/editor/animate-mode/timeline) |
| G9 | L'Inspector de Godot suit la sélection du Scene dock, filtre les propriétés, accepte le drag de ressources et expose le retour à la valeur par défaut. | [Godot — Inspector Dock](https://docs.godotengine.org/en/stable/tutorials/editor/inspector_dock.html) |
| G10 | Le profiler Godot mesure le temps par frame et catégorie depuis le debugger. | [Godot — Profiler](https://docs.godotengine.org/en/stable/tutorials/scripting/debug/the_profiler.html) |
| G11 | La navigation 2D Godot fournit maps, régions, liens, obstacles et agents. | [Godot — Navigation 2D](https://docs.godotengine.org/en/stable/tutorials/navigation/navigation_introduction_2d.html) |
| G12 | Godot fournit des systèmes de particules 2D CPU et GPU. | [Godot — Particle systems 2D](https://docs.godotengine.org/en/stable/tutorials/2d/particle_systems_2d.html) |
| G13 | Les nœuds Control de Godot composent l'UI jeu avec ancres, containers, focus et thèmes. | [Godot — GUI](https://docs.godotengine.org/en/stable/tutorials/ui/index.html) |
| G14 | L'API multijoueur haut niveau Godot gère peers, RPC et autorité. | [Godot — High-level multiplayer](https://docs.godotengine.org/en/stable/tutorials/networking/high_level_multiplayer.html) |
| G15 | Godot documente traductions, locales et internationalisation. | [Godot — Internationalizing games](https://docs.godotengine.org/en/stable/tutorials/i18n/internationalizing_games.html) |
| U9 | UI Toolkit sert à créer des interfaces runtime et éditeur avec documents, styles et contrôles. | [Unity 6 — UI Toolkit](https://docs.unity3d.com/6000.0/Documentation/Manual/UIElements.html) |
| U10 | Unity Profiler collecte et analyse les performances de l'éditeur et des players connectés. | [Unity 6 — Profiler](https://docs.unity3d.com/6000.0/Documentation/Manual/Profiler.html) |
| U11 | Le package AI Navigation fournit navmeshes, obstacles dynamiques et liens. | [Unity — AI Navigation](https://docs.unity3d.com/Packages/com.unity.ai.navigation@2.0/manual/index.html) |
| U12 | Le Particle System Unity expose modules et rendu d'effets. | [Unity 6 — Particle systems](https://docs.unity3d.com/6000.0/Documentation/Manual/ParticleSystems.html) |
| U13 | Unity documente ses services et packages multijoueurs. | [Unity — Multiplayer](https://docs.unity.com/ugs/en-us/manual/mps-sdk/manual) |
| U14 | Le package Localization gère chaînes, assets, locales et pseudo-localisation. | [Unity — Localization](https://docs.unity3d.com/Packages/com.unity.localization@1.5/manual/index.html) |
| E9 | L'interface Unreal synchronise World Outliner, viewport et Details panel. | [Unreal — Editor interface](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-editor-interface) |
| E10 | Blueprint est le système de scripting visuel nodal d'Unreal. | [Unreal — Blueprints Visual Scripting](https://dev.epicgames.com/documentation/en-us/unreal-engine/blueprints-visual-scripting-in-unreal-engine) |
| E11 | Unreal fournit Insights et des outils de profilage CPU, GPU et mémoire. | [Unreal — Introduction to performance profiling](https://dev.epicgames.com/documentation/en-us/unreal-engine/introduction-to-performance-profiling-and-configuration-in-unreal-engine) |
| E12 | Le Navigation System Unreal génère un Navigation Mesh pour le pathfinding des agents. | [Unreal — Navigation System](https://dev.epicgames.com/documentation/en-us/unreal-engine/navigation-system-in-unreal-engine) |
| E13 | Niagara est le système de création et de rendu VFX d'Unreal. | [Unreal — Niagara overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/overview-of-niagara-effects-for-unreal-engine) |
| E14 | UMG fournit widgets et UI runtime avec designer visuel. | [Unreal — UMG UI Designer](https://dev.epicgames.com/documentation/en-us/unreal-engine/umg-ui-designer-for-unreal-engine) |
| E15 | Unreal documente réplication, RPC et modèle client-serveur. | [Unreal — Networking overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/networking-overview-for-unreal-engine) |
| E16 | Unreal fournit un pipeline de localisation de texte et ressources. | [Unreal — Localization overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/localization-overview-for-unreal-engine) |
| GM5 | GameMaker fournit des éditeurs dédiés notamment aux objets, paths, rooms, sequences, shaders, sons, sprites, tile sets, particules et courbes. | [GameMaker — Asset Editors](https://manual.gamemaker.io/monthly/en/The_Asset_Editors/The_Asset_Editors.htm) |
| GM6 | GameMaker expose sockets et fonctions réseau asynchrones. | [GameMaker — Networking](https://manual.gamemaker.io/monthly/en/GameMaker_Language/GML_Reference/Networking/Networking.htm) |
| GM7 | Le debugger GameMaker fournit breakpoints, pas-à-pas et inspection. | [GameMaker — Debugging](https://manual.gamemaker.io/monthly/en/IDE_Tools/Debugging.htm) |
| C7 | La Timeline Construct expose animations, pistes, clés, lecture, scrub et auto-key depuis Layout/Properties. | [Construct — Timeline Bar](https://www.construct.net/en/make-games/manuals/construct-3/interface/bars/timeline-bar) |
| C8 | Event Sheet édite visuellement conditions/actions et fournit recherche et breakpoints. | [Construct — Event Sheet View](https://www.construct.net/en/make-games/manuals/construct-3/interface/event-sheet-view) |
| C9 | L'objet Particles Construct émet et paramètre des particules 2D. | [Construct — Particles](https://www.construct.net/en/make-games/manuals/construct-3/plugin-reference/particles) |
| R5 | En mode State Machine, Rive remplace la timeline par un graphe visuel d'états et transitions. | [Rive — State Machine](https://rive.app/docs/editor/state-machine/state-machine) |
| G16 | Godot organise FileSystem, Scene, Inspector et panneaux inférieurs autour du viewport ; l'animation et le debug apparaissent dans le contexte du projet. | [Godot — First look at the editor](https://docs.godotengine.org/en/stable/getting_started/introduction/first_look_at_the_editor.html) |
| G17 | Le TileMap editor fournit sélection, peinture, ligne, rectangle, remplissage, motifs, terrains et placeholders de références absentes. | [Godot — Using TileMaps](https://docs.godotengine.org/en/stable/tutorials/2d/using_tilemaps.html) |
| U15 | Unity 6 relie Hierarchy et Scene, et l'Inspector affiche les propriétés de la sélection d'objets, assets ou composants. | [Unity 6 — Hierarchy](https://docs.unity3d.com/6000.0/Documentation/Manual/Hierarchy.html), [Inspector](https://docs.unity3d.com/6000.0/Documentation/Manual/UsingTheInspector.html) |
| U16 | Le mode Prefab isole l'édition de l'asset tout en conservant son contexte et ses objets dans Hierarchy/Scene. | [Unity 6 — Editing a Prefab in Prefab Mode](https://docs.unity3d.com/6000.0/Documentation/Manual/EditingInPrefabMode.html) |
| E17 | Unreal est réalisé comme une suite d'éditeurs spécialisés — Level, Blueprint, Physics Asset, Sequencer et Animation — partageant des conventions d'outils. | [Unreal — Tools and Editors](https://dev.epicgames.com/documentation/unreal-engine/tools-and-editors-in-unreal-engine) |
| E18 | Le Level Editor combine viewport, Content Browser, Outliner, Details et modes d'édition spécialisés. | [Unreal — Level Editor](https://dev.epicgames.com/documentation/unreal-engine/level-editor-in-unreal-engine) |
| GM8 | L'Inspector GameMaker suit assets, couches, instances et pistes, accepte la multi-sélection et peut rester verrouillé sur une cible. | [GameMaker — Inspector](https://manual.gamemaker.io/monthly/en/IDE_Tools/The_Inspector.htm) |
| GM9 | Le Sequence Editor crée une piste et une clé lorsqu'un asset est placé dans son canvas, puis anime ses paramètres au playhead. | [GameMaker — Sequences](https://manual.gamemaker.io/monthly/en/Quick_Start_Guide/Sequences.htm) |
| C10 | Construct organise menu/toolbar, onglets de vues, vue centrale, Properties, Project et Layers bars ; les bars sont réarrangeables et dockables. | [Construct 3 — Interface](https://www.construct.net/en/make-games/manuals/construct-3/overview/the-interface) |
| S5 | Spine centre la navigation sur un Tree hiérarchique filtrable et synchronise sélection et clés entre viewport, dopesheet et graph. | [Spine — Tree view](https://en.esotericsoftware.com/spine-tree), [Dopesheet](https://us.esotericsoftware.com/spine-dopesheet) |
| S6 | Spine expose les poids dans une vue dédiée avec bones colorés, sélection de vertex, pinceaux, auto, smooth et test de déformation. | [Spine — Weights view](https://us.esotericsoftware.com/spine-weights) |
| R6 | Rive n'affiche que les outils contextuels : Hierarchy, Stage et Inspector partagent la sélection ; la Timeline apparaît en Animate et cède sa place au graphe d'état. | [Rive — Interface Overview](https://rive.app/docs/editor/interface-overview/overview) |
| G18 | Les plugins Godot peuvent ajouter des outils, nœuds et docks à l'éditeur sans recompiler le moteur. | [Godot — Making plugins](https://docs.godotengine.org/en/stable/tutorials/plugins/editor/making_plugins.html) |
| U17 | `EditorWindow` permet d'implémenter des fenêtres Unity spécialisées flottantes ou dockées comme onglets. | [Unity 6 — EditorWindow](https://docs.unity3d.com/6000.0/Documentation/ScriptReference/EditorWindow.html) |
| E19 | Unreal encapsule outils éditeur et runtime dans des modules, et ses plugins peuvent ajouter menus, commandes, sous-modes et types de fichiers. | [Unreal — Modules](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-modules), [Plugins](https://dev.epicgames.com/documentation/en-us/unreal-engine/plugins-in-unreal-engine) |
| C11 | L'Addon SDK de Construct étend l'éditeur et le runtime avec plugins, behaviors, effets, thèmes et importeurs personnalisés. | [Construct — Addon SDK](https://www.construct.net/en/make-games/manuals/addon-sdk) |
| C12 | Le Layout View conserve un outil d'édition direct, une couche active, la sélection multiple/rectangle et une Properties Bar synchronisée ; le Tilemap se peint directement dans cette même surface. | [Construct — Layout View](https://www.construct.net/en/make-games/manuals/construct-3/interface/layout-view) |

### Registre d'inférences UX — vérifié le 4 septembre 2026

- Les sources officielles décrivent des faits de produit, pas une mesure
  comparative de « bonne UX ». Les verdicts du tableau sont donc des
  **inférences** tirées de la convergence des parcours documentés.
- G9, U15, E9, GM8, C6, S5 et R6 étayent l'inférence qu'une sélection nominale
  doit rester visible et éditable entre hiérarchie, canvas et inspecteur.
- E17/E18, G16, C10 et R6 étayent l'inférence qu'un outil spécialisé doit
  remplacer ou compléter la surface centrale et son dock contextuel, sans
  ouvrir un second parcours déconnecté.
- C3/C12 et G17 étayent l'inférence qu'un geste de production répétitif doit
  conserver son outil actif ; le choix exact d'un bouton `Placer en continu`
  est une décision Vertex Loom, pas une fonction attribuée à tous les moteurs.
- S6 montre un niveau de référence plus exigeant pour le rig : binding depuis
  la sélection, couleurs de bones, box-select, édition directe/brush, auto,
  smooth et test de déformation dans le même contexte. L'écart mesh/poids de
  Vertex Loom est donc confirmé, pas seulement déduit d'une liste de fonctions.

## Conclusion et ordre de correction

La régression A07 a été corrigée et vérifiée sur affichage réel : les nouveaux
Beam et Button préservent la source sans effet ; la recoloration reste
explicite. Les parcours Entity, Animation Graph, Behavior Graph et Mechanic
Graph ont atteint L3 : ils sont réellement manipulables, sauvegardés et
rechargés par l'UI. Le parcours Entity→Animation atteint L4 pour le clip : le
paquet exact est chargé et son nœud/événement sont évalués par PreviewRuntime.
Son placement Map reste toutefois construit par le harnais et doit rejoindre
les gestes UI.

Le déficit dominant est désormais transversal. A54–A61 montrent que Map Studio,
Behavior, l'ensemble Animation et Rig/Physics Entity délèguent leurs
workspaces, mais que Visual, la hiérarchie/inspecteur Entity et une grande
partie de leurs E2E restent assemblés dans le point
d'entrée Asset Studio, avec des sélections, actions et diagnostics encore
locaux. Le refactoring progressif d'ADR-0151 reste donc un prérequis P0, pas un
nettoyage facultatif.

Ordre de correction recommandé :

1. `P0 — stabiliser l'éditeur` : état, actions, widgets et diagnostics partagés,
   puis shell commun, navigation et sélection stable.
2. `P0 — finir le flux produit` : porter Entity/Animation/Logic et Map/Scene,
   manipuler mécaniques sur canvas et prouver UI→Preview→package→runtime.
3. `P1 — rendre la production efficace` : rig/IK/XPBD sur canvas, Map palette et
   tilemap, import/édition Audio, navigation, VFX, UI de jeu et localisation.
4. `P2 — élargir seulement sur besoin produit` : réseau, collaboration intégrée,
   3D et familles supplémentaires de contraintes/effets.

Le gate juridique A38 et la matrice de plateformes A52 restent bloquants avant
une publication publique. Les capacités absentes ne doivent pas être ajoutées
avant les refactors UX P0 : accumuler de nouveaux contrats augmenterait encore
l'écart entre ce que le moteur sait exécuter et ce qu'un utilisateur sait créer.
