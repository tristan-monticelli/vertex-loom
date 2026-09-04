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
| Composer une Entity | Création depuis un visuel prouvée par clic, arbre récursif, sélection, multi-sélection, reparentage et gizmo | L3 | partiel | Le nouveau flux prouve le premier nœud ; la hiérarchie complexe, la réparation et les overrides restent répartis entre plusieurs E2E | P0 | Étendre le même flux à ajout visuel→parentage→transform→save/reload sans préparation API |
| Configurer rig, IK et déformation | Sections numériques dans l'inspecteur, contrats et solveurs testés | L1/L2 | technique | Pas de création visuelle de bones, handles IK, mesh ni peinture de poids comparable à Spine/Rive | P1 | Workspace Rig avec outils canvas, hiérarchie de bones, édition de mesh et poids visualisés |
| Configurer XPBD | Listes/formulaires, overlay et solveur déterministe | L1/L2 | technique | La fixture XPBD est injectée par code avant capture ; pas de pose directe de particules/contraintes | P1 | Mode Physics sur canvas, liens manipulables, presets guidés et diagnostics locaux |
| Créer un clip d'animation | Entity→nouveau clip→première clé→reload est désormais prouvé par clics UI ; timeline, auto-key et courbe existent | L3 | partiel | Deuxième pose, lecture, correction de clé, événement et runtime publié ne sont pas encore dans ce flux transversal | P0 | Étendre l'E2E à deux clés→lecture→édition→save/reload→runtime |
| Éditer une machine à états | Fenêtre `Animation Graph` avec onglets, listes et formulaires de transitions | L2 | partiel | Ce n'est pas un graphe visuel ; les deux états et la transition de l'E2E sont injectés par API | P0 | Canvas de nœuds, connexions directes, sélection synchronisée, simulation et erreurs sur les arêtes |
| Construire un Behavior Graph | Fenêtre avec type/ID de nœud et champs `From node/port`→`To node/port` | L1/L2 | technique | Aucun graphe visuel, ports, drag de connexion, recherche d'action ni breakpoint | P0 | Éditeur nodal visuel inspiré de Blueprint/Construct, IDs masqués, validation immédiate |
| Construire une mécanique | Fenêtre Map Studio avec listes et formulaires body/pivot/joint/motor/sensor/constraint/event | L1/L2 | technique | Nom « graphe » sans canvas de graphe ; connexions et références reposent sur des IDs/handles | P0 | Manipulation sur le canvas de map et graphe secondaire pour événements/signaux |
| Construire une map | Panneaux layers/instances, canvas, inspecteur, collisions, triggers et scènes séparées | L2/L3 | partiel | Overrides et relations restent form-heavy ; pas de tilemap, terrain, brush ni navigation | P1 | Prioriser placement direct, duplication, palette, tilemap et diagnostics de collision avant nouveaux contrats |
| Prévisualiser et publier | PreviewRuntime, validation et package existent avec tests | L1/L2 | partiel | Plusieurs preuves vérifient le chargement interne, pas un scénario auteur complet jusqu'au binaire publié ; gate licences/signature ouverte | P0 | Une fixture asymétrique créée par UI, empaquetée puis exécutée dans le runtime de release |

### Pourquoi les E2E Entity et Animation ne suffisent pas

Dans `editors/asset_studio/main.cpp`, `--e2e-entity` appelle directement
`set_selected_entity_node`, `add_selected_entity_node`,
`set_selected_entity_definition` et injecte la machine à états ainsi que XPBD
avant d'afficher l'interface. `--e2e-animation` appelle directement
`create_animation` et `set_selected_animation_segment` avant d'activer le
gizmo. Les wrappers CMake contrôlent le code retour, l'existence d'une capture
et la validité du projet ; ils ne prouvent pas l'origine UI des mutations.

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

## Tableau maître

| Domaine | Capacité | Preuve Vertex Loom | Studio | Preview | Runtime publié | État | Godot | Unity | Unreal | GameMaker | Construct | Spine | Rive | Écart/impact | Priorité | Recommandation | Sources officielles |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Projet | A01 — manifeste, création, chemins sûrs, sauvegarde atomique | `manifest.cpp`, `document_storage.cpp`, `project_tests`, ADR-0006/0009/0013 | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | N/A | Pas d'écart critique prouvé | P2 | Conserver les fixtures de traversée et crash-recovery | G1, C1 |
| Projet | A02 — registre typé et fermeture transitive des dépendances | `resource_registry.cpp`, `map_package.cpp`, ADR-0019/0141 | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | partiel | ✓ | partiel | partiel | Diagnostic de cycle moins visuel que les grands moteurs | P2 | Afficher le chemin complet du cycle dans les Studios | G1, U1, E1 |
| Import | A03 — PNG intact, crop non destructif, alpha | `texture_asset.cpp`, `raster_view`, `asset_studio_texture_e2e` | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | partiel | partiel | Fidélité couverte, mais recette GPU publique encore externe | P1 | Garder une fixture RGBA asymétrique dans la gate graphique | G1, U1, E1 |
| Import | A04 — SVG lié puis conversion vectorielle native | `svg_vector.cpp`, ADR-0016/0036, tests SVG | oui | oui | oui | implémenté | ✓ | partiel | partiel | partiel | ✓ | ✓ | ✓ | Pas de réimport différentiel documenté | P2 | Ajouter un diff de réimport lié | G1, U1, E1 |
| Surfaces | A05 — Material v2, blend, texture/vector pattern, UV | `material.hpp`, `material_entity_tests`, ADR-0038/0138 | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | partiel | Bibliothèque de matériaux limitée | P2 | Ajouter duplication et aperçu comparatif | G1, U1, E1 |
| Surfaces | A06 — pile Tint/Holography/Shine ordonnée | `shader_profile.hpp`, `opengl_vector_smoke`, ADR-0143 | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | partiel | ✓ | N/A | partiel | Effets limités à trois familles | P2 | N'ajouter un effet qu'avec contrat, UI et test pixel | G1, U1, E1, R2 |
| Surfaces | A07 — source intacte par défaut, recoloration explicite | `BeamColorMode`, tests presets, E2E Beam/Button et smoke GL réels ; ADR-0136/0138 | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | ✓ | Défaut bleu/reflet corrigé ; anciens JSON inchangés | P0 | Maintenir fixture asymétrique et gate sans `SKIP` | G1, U1, E1, R2 |
| Vecteur | A08 — formes, fills, strokes, clips, hiérarchie et Bézier | `vector_asset.hpp`, ADR-0022..0035/0128, canvas E2E | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | partiel | ✓ | ✓ | ✓ | Opérations booléennes non prouvées | P2 | Documenter ou ajouter les booléens si requis par un projet | G1, U1, E1, S1, R1 |
| Chemins | A09 — spline géométrique ligne/cubique, ouverte/fermée | `TexturedPathCommandKind`, `textured_path_geometry_tests` | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | partiel | ✓ | ✓ | Pas de subdivision utilisateur | P2 | Exposer la tolérance uniquement avec budget perf | G2, U2, E2, S2 |
| Chemins | A10 — chemin visuel texturé repeat/mirror/stretch | `TexturedPathUvMode`, tests UV et OpenGL | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | partiel | partiel | N/A | N/A | Couverture graphique de toutes variantes à consolider | P1 | Matrice pixel repeat/mirror/stretch dans la recette GL | G2, U2 |
| Chemins | A11 — largeur, profil, joins, caps, orientation, transparence | `width_profile`, `join`, `cap`, `opacity`, six captures join/cap | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | partiel | partiel | N/A | N/A | Recette publiée non isolée pour chaque combinaison | P1 | Fixture asymétrique et probes par mode | G2, U2 |
| Déplacement | A12 — rail de déplacement d'instance | Aucun contrat de rail cinématique ; ne pas confondre avec A09/A10 | non | non | non | absent | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | partiel | Impossible d'authorer une plateforme ou caméra sur rail | P1 | Ajouter un composant générique PathFollower séparé du rendu | G2, U2, E2, C2, S2 |
| Animation | A13 — animation guidée par chemin | Aucun binding distance/tangente sur spline | non | non | non | absent | ✓ | ✓ | ✓ | partiel | partiel | ✓ | partiel | Courbes visuelles inutilisables comme trajectoires animées | P1 | ADR dédiée, binding `progress`, orientation optionnelle | G2, U2, E2, S2 |
| Composition | A14 — compositions, composants, paramètres, variantes, ancres | `visual_composition.hpp`, `visual_component.hpp`, ADR-0104/0105 | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | partiel | ✓ | Pas de bibliothèque de variantes visuelle avancée | P2 | Conserver le résolveur commun et améliorer seulement l'UX | G1, U1, E1, R1 |
| Entités | A15 — entité/prefab, arbre et overrides d'instance | `entity.hpp`; création du premier nœud depuis un visuel prouvée par clic ; hiérarchie avancée encore fragmentée | partiel | oui | oui | partiel | ✓ | ✓ | ✓ | ✓ | ✓ | partiel | partiel | Contrat/runtime solides, mais création complète de hiérarchie et overrides dans un seul flux non prouvée | P0 | Étendre l'E2E UI au drop, parentage, transform et overrides | G1, U1, E1, G8, U8, C6 |
| Entités | A16 — transformation atomique A→B avec transfert d'état | `entity_transformation.hpp`, ADR-0115, tests runtime | oui | oui | oui | implémenté | partiel | partiel | partiel | partiel | partiel | N/A | N/A | Capacité spécialisée différenciante | P2 | Garder politiques versionnées et replay déterministe | G1, U1, E1 |
| Animation | A17 — clips, clés, interpolation, easing, segments, événements | `animation.hpp` v4, timeline et tests contrat/runtime ; l'E2E crée clip et segment par API avant l'interaction | partiel | oui | oui | partiel | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | Timeline réelle, mais création/édition complète par le panneau non prouvée | P0 | Scénario UI complet avec deux clés, correction, événement, save/reload et runtime | G3, U3, E3, GM1, S1, R1, G8, U8, E8, GM4, S4, R4 |
| Animation | A18 — machine à états d'animation | `animation_state_machine.hpp`; fenêtre à onglets/formulaires ; états/transitions E2E injectés par API | partiel | oui | oui | partiel | ✓ | ✓ | ✓ | partiel | ✓ | partiel | ✓ | Pas de graphe visuel ni création E2E par UI | P0 | Remplacer l'éditeur de listes par un canvas nodal et tester les connexions UI | G3, U3, E3, R1, R5 |
| UX Entity | A39 — composition contextuelle, arbre et édition directe | Flux réel visuel→Entity→Animation sans préparation de document ; arbre/gizmo couverts séparément | partiel | oui | oui | partiel | ✓ | ✓ | ✓ | partiel | ✓ | partiel | partiel | Le chemin nominal initial est prouvé ; composition multi-nœuds et corrections restent fragmentées | P0 | Unifier drop, parentage et transform dans le scénario transversal | G9, U8, E9, GM4, C6, S4, R4 |
| UX Animation | A40 — workspace timeline, création de piste et keying contextuel | Le flux transversal crée clip et première clé par clic ; l'ancien A→B reste préparé par API | partiel | oui | oui | partiel | ✓ | ✓ | ✓ | ✓ | partiel | ✓ | ✓ | Première animation utilisable ; édition complète et runtime publié non prouvés | P0 | Ajouter playhead, seconde pose, lecture, correction et preuve publiée au même E2E | G8, U8, E8, GM4, C7, S4, R4 |
| UX Studio | A41 — coquille de panneaux et viewer pilotable | Project gauche, Viewer central, Inspector droit, Timeline basse ; Fit/zoom/grille/fond et probes graphiques normal/minimum ; ADR-0148 | oui | oui | N/A | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | partiel | partiel | La structure et le cadrage ne changent plus selon le type de ressource ; l'état du Viewer reste volontairement local au Studio | P2 | Maintenir le probe 900 × 600 et appliquer la même grammaire au shell Map Studio | G8, U8, E8, GM4, C6, S4, R4 |
| UX Map | A42 — hiérarchie, canvas et inspecteur de map | Layers/instances, canvas et inspecteur existent ; mécaniques/scènes restent des fenêtres de formulaires | partiel | oui | N/A | partiel | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | N/A | Placement utilisable, mais composition complexe form-heavy et sans tilemap/navigation | P1 | Test de tâche réelle puis palette, duplication, tilemap et édition directe des relations | G8, U8, E8, GM4, C6 |
| Rig | A19 — bones, skinning pondéré et déformation mesh | `entity.hpp`, sérialisation, runtime et tests déformation ; inspecteur numérique | partiel | oui | oui | partiel | ✓ | ✓ | ✓ | partiel | partiel | ✓ | ✓ | Aucun rig canvas ni peinture de poids comparable à Spine/Rive | P1 | Workspace Rig visuel avec mesh, bones, poids et preview de déformation | G4, U3, E3, S1, R1 |
| Rig | A20 — IK FABRIK et ordre des contraintes | ADR-0043/0044/0096, contrats et tests solveur ; édition par formulaires | partiel | oui | oui | partiel | ✓ | ✓ | ✓ | partiel | partiel | ✓ | ✓ | Handles, chaîne et cible IK ne sont pas éditables directement sur le canvas | P1 | Gizmos de chaîne/cible, ordre visible et test UI→runtime | G4, U3, E3, S2, R1 |
| Physique | A21 — XPBD et substeps/interpolation | Solveur, overlay et tests ; la fixture graphique est injectée avant affichage | partiel | oui | oui | partiel | partiel | partiel | partiel | — | — | partiel | partiel | Diagnostic visible mais authoring direct non prouvé | P1 | Éditeur canvas de particules/contraintes et E2E sans préparation API | G5, U4, E4, S1, R1 |
| Input | A22 — actions sémantiques, clavier/gamepad, remap | `input.hpp`, ADR-0055/0101/0119/0123, UI E2E | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | partiel | Touch/gestures non prouvés | P2 | Ajouter seulement avec cible plateforme | G1, U5, E5, C1 |
| Comportement | A23 — BehaviorGraph générique signaux/actions | `behavior_graph.hpp`, runtime et fenêtre de listes/formulaires ; scénario créé via `BehaviorSession` | partiel | oui | oui | partiel | ✓ | ✓ | ✓ | partiel | ✓ | N/A | ✓ | « Graph » sans canvas, ports ni connexions directes ; IDs manuels | P0 | Éditeur nodal avec palette, ports typés, recherche, trace et breakpoints | E10, C8, R5 |
| Maps | A24 — map, layers, verrouillage, ordre, snapping et instances | `map.hpp`, panels Map Studio, contrats/tests | partiel | oui | oui | partiel | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | N/A | Base utilisable, mais flux de production de masse et preuves UI→package incomplets | P1 | Test de tâche auteur et outils palette/duplication/multi-placement | G6, U6, E6, GM1, C3 |
| Maps | A25 — tileset/tilemap/autotiling/terrain | Aucun type Studio ou schéma tile | non | non | non | absent | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | N/A | Création de grands niveaux répétitifs coûteuse | P1 | ADR tilemap après stabilisation du placement actuel | G6, U6, E6, C3 |
| Physique | A26 — collisions éditables, triggers et payloads | ADR-0048/0077..0080/0113, Map Studio E2E | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | bounding box | listeners | Formes/filtrage avancé limités | P2 | Ajouter catégories/masques seulement avec diagnostics | G5, U4, E4, GM2, C4 |
| Mécaniques | A27 — nœuds body/pivot/joint/motor | `MechanicNodeKind`, compilateur/presets ; fenêtre avec listes et champs d'IDs | partiel | oui | oui | partiel | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | N/A | Runtime réel, mais aucune construction graphique des corps, pivots et joints | P0 | Édition directe dans Map Studio, handles visibles et liens par drag | G5, U4, E4, GM2, C4 |
| Mécaniques | A28 — nœuds sensor/constraint/event | contrats, compilateur Box2D et log ; connexions par champs | partiel | oui | oui | partiel | ✓ | ✓ | ✓ | ✓ | ✓ | contraintes | listeners | Observabilité et câblage trop techniques | P0 | Graphe d'événements, handles résolus, dernier signal et erreurs sur connexion | G5, U4, E4, S2, R2 |
| Scènes | A29 — scène, campagnes, transitions et map montée | `scene.hpp`, runtime/tests ; Scene Studio séparé à base de formulaires | partiel | oui | oui | partiel | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | N/A | Pas de vue de campagne ni transition manipulable ; création dispersée entre Studios | P1 | Workspace scènes/campagne avec aperçu de transition et navigation explicite | G1, U1, E1 |
| Caméra | A30 — Camera2D follow, limites et réglages runtime | ADR-0064/0122, runtime tests | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | N/A | Blend multi-caméra absent | P2 | Ajouter seulement si scènes le demandent | G1, U1, E1 |
| Audio | A31 — événements audio, volume, loop et mixer PCM | `AudioDocument v2`, inspecteur d'une ressource existante, mixer/tests et smoke périphérique | partiel | oui | oui | partiel | ✓ | ✓ | ✓ | ✓ | ✓ | timeline | audio events | Pas de création/import audio unifié, waveform, bus visuel ni formats compressés | P1 | Import audio nominal, audition, waveform, bus et preuve UI→package | G1, U1, E1, GM3 |
| Replay | A32 — capture/relecture déterministe | `replay.hpp`, ADR-0057/0058 et tests runtime ; aucun parcours d'auteur identifié | non | oui | oui | contrat seulement | partiel | partiel | partiel | partiel | partiel | N/A | N/A | Fonction runtime sans outil Studio de capture, inspection ou comparaison | P1 | Panneau Replay avec enregistrer, lire, checksum et divergence par frame | G1, U1, E1 |
| Sauvegarde | A33 — progression et reprise | `progress_save.hpp`, ADR-0060/0061 | partiel | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | N/A | UX de slots non prouvée | P2 | Ajouter parcours utilisateur si plusieurs slots deviennent requis | G1, U1, E1, C1 |
| Performance | A34 — batching, culling, benchmark 60 FPS | Build Release natif Apple M1 Pro : renderer 795,993, runtime dense 125,812 et fixture textile 251,251 FPS p95 sur 600 frames ; rapport versionné, ADR-0066..0068 | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | métriques | métriques | Gate locale macOS prouvée sans skip ; Windows/Linux restent exigés séparément par la matrice de release | P2 | Conserver seuil 60 FPS, rapports natifs et refus des skips sur chaque plateforme | G1, U1, E1, S1 |
| Validation | A35 — validation unifiée, erreurs de champ, recovery | validateur CLI, diagnostics et recovery smoke | partiel | oui | oui | partiel | ✓ | ✓ | ✓ | ✓ | ✓ | partiel | partiel | CLI solide ; focus, réparation contextuelle et messages de plusieurs panneaux restent incomplets | P1 | Associer chaque erreur au panneau/champ/action de réparation | G1, U1, E1 |
| Publication | A36 — paquet map/campagne portable, dépendances, version runtime | `map_package.cpp`, fermeture transitive et tests package | partiel | oui | oui | partiel | ✓ | ✓ | ✓ | ✓ | ✓ | export runtime | export `.riv` | Flux auteur→build publié non prouvé et gate licences/signature encore ouverte | P0 | E2E release réel et approbations de redistribution avant tag | G7, U7, E7, C5, S3, R3 |
| Observabilité | A37 — logs JSONL, overlay, diagnostics render/physics | `TraceContext`, logs et tests ; pas de profiler intégré ni navigation complète log→objet | partiel | oui | oui | partiel | ✓ | ✓ | ✓ | partiel | partiel | métriques | listeners | Corrélation locale utile, diagnostic auteur inférieur aux profilers/remote debuggers des moteurs généralistes | P1 | Vue profiler frame, filtres, sélection de ressource et export de trace | G10, U10, E11 |
| Sécurité | A38 — validation imports, chemins relatifs, licences/SBOM | tests traversée, ADR-0144/0146 | oui | oui | oui | partiel | ✓ | ✓ | ✓ | partiel | partiel | export | export | Trois preuves de redistribution restent bloquantes | P0 | Ne pas publier avant statut `approved` réel | G7, U7, E7 |
| Programmation | A43 — scripting utilisateur et API gameplay | Aucun langage, fichier script, VM ou API de plugin chargé par le projet ; BehaviorGraph est fermé sur ses types compilés | non | non | non | absent | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | partiel | Tout nouveau comportement exige une modification et recompilation du moteur | P0 | Définir d'abord sandbox, déterminisme, version d'API et debug ; ne pas détourner les IDs de BehaviorGraph | G1, U1, E10, C8 |
| Extension | A44 — plugins éditeur, importeurs et outils personnalisés | Aucun contrat d'extension ou registre de plugin public recensé | non | non | non | absent | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | partiel | Le Studio ne peut pas être adapté à un pipeline projet sans fork | P1 | ADR d'extensions versionnées, permissions, isolation et compatibilité de schéma | G1, U1, E1, GM5, C1 |
| Navigation | A45 — navmesh/grille, pathfinding et agents | Aucun type navigation, bake, query ou agent runtime recensé | non | non | non | absent | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | N/A | Pas d'IA de déplacement ni validation de zones accessibles | P1 | Introduire navigation 2D indépendante de TexturedPath et du rail cinématique | G11, U11, E12, C2 |
| VFX | A46 — particules et éditeur d'effets | XPBD simule une déformation physique, pas un système de particules VFX ; aucun emitter/renderer VFX | non | non | non | absent | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | partiel | Fumée, impacts, pluie et effets de masse nécessitent des entités manuelles | P1 | Contrat emitter 2D borné, preview déterministe et budget overdraw | G12, U12, E13, GM5, C9 |
| UI jeu | A47 — widgets, layout, texte, focus et accessibilité | Les panneaux ImGui sont des outils Studio ; aucun document UI de jeu, layout responsive ou navigation focus runtime | non | non | non | absent | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | partiel | Menus, HUD et accessibilité ne sont pas authorables dans le produit | P0 | Définir arbre UI runtime, layout, texte, focus/input et preview multi-résolutions | G13, U9, E14, C1 |
| Internationalisation | A48 — polices, fallback, localisation et pseudo-locales | Aucun catalogue, locale, shaping/fallback ou import de police projet recensé | non | non | non | absent | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | partiel | Texte localisé et scripts complexes non pris en charge | P1 | Contrat texte/font/locales, fallback déterministe et pseudo-localisation en Studio | G15, U14, E16 |
| Réseau | A49 — multiplayer, réplication et transport | Aucun protocole, session, réplication ou test réseau recensé | non | non | non | absent | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | N/A | Moteur limité aux expériences locales | P2 | Ne planifier qu'après besoin produit ; séparer transport, autorité et déterminisme | G14, U13, E15, GM6 |
| Debug | A50 — debugger gameplay, breakpoints et inspection live | Logs/overlays existent, mais aucun breakpoint Behavior/animation, pause-step ou inspection de monde live | non | partiel | partiel | absent | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | partiel | Les erreurs de logique imposent lecture de logs et reproduction manuelle | P0 | Breakpoints non persistants, step frame/event et lien trace→nœud→ressource | G10, U10, E11, GM7, C8 |
| Collaboration | A51 — source control, diff/merge sémantique et verrouillage | JSON textuel diffable et écritures atomiques ; aucune intégration VCS ni merge sémantique | non | N/A | N/A | partiel | ✓ | ✓ | ✓ | partiel | partiel | N/A | N/A | Conflits sur gros documents et assets binaires non assistés | P2 | Garder les formats stables puis ajouter diff sémantique avant intégration VCS | G1, U1, E1 |
| Plateformes | A52 — matrice d'export et certification | CMake/builds natifs et package interne ; preuve release complète limitée au macOS local documenté | partiel | partiel | partiel | non prouvé | ✓ | ✓ | ✓ | ✓ | ✓ | runtimes ciblés | runtimes ciblés | Portabilité de code ne vaut pas validation Windows/Linux/mobile/web/console | P0 | Matrice CI réelle par cible, smoke GPU/audio/input et artefacts signés | G7, U7, E7, C5, S3, R3 |
| 3D | A53 — scène, rendu, animation et physique 3D | Périmètre architectural explicitement 2D ; aucun contrat 3D | non | non | non | absent | ✓ | ✓ | ✓ | partiel | — | N/A | N/A | Pas un défaut pour le périmètre 2D actuel ; bloque seulement un futur produit 3D | P2 | Conserver hors périmètre tant qu'un besoin produit ne justifie pas un second moteur | G1, U1, E1 |

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
  collaboration → A51 ; plateformes/3D → A52/A53.

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

## Conclusion et ordre de correction

La régression A07 a été corrigée et vérifiée sur affichage réel : les nouveaux
Beam et Button préservent la source sans effet ; la recoloration reste
explicite. En revanche, A15/A17/A18/A23/A27/A28/A39/A40 ne peuvent pas être
considérés comme terminés côté utilisateur : leurs contrats et runtimes sont
plus avancés que leurs parcours Studio et que leur niveau de preuve.

Ordre de correction recommandé :

1. `P0 — rendre le cœur utilisable` : Entity de zéro à sauvegarde, Animation de
   zéro à lecture, graphes visuels Animation/Behavior/Mechanic, debug pas-à-pas
   et E2E UI→Preview→package→runtime sans préparation API.
2. `P1 — rendre la production efficace` : rig/IK/XPBD sur canvas, Map palette et
   tilemap, import/édition Audio, navigation, VFX, UI de jeu et localisation.
3. `P2 — élargir seulement sur besoin produit` : réseau, collaboration intégrée,
   3D et familles supplémentaires de contraintes/effets.

Le gate juridique A38 et la matrice de plateformes A52 restent bloquants avant
une publication publique. Les capacités absentes ne doivent pas être ajoutées
avant les refactors UX P0 : accumuler de nouveaux contrats augmenterait encore
l'écart entre ce que le moteur sait exécuter et ce qu'un utilisateur sait créer.
