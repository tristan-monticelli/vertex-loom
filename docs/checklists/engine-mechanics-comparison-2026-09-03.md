# Audit des assets et comparaison multi-moteurs — 2026-09-03

## Portée et méthode

Audit statique du dépôt au 3 septembre 2026, complété par les tests nommés
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

### Méthode UX ajoutée après revue

La présence d'un champ ou d'une commande ne suffit pas à déclarer une capacité
utilisable. L'audit mesure aussi le parcours de production : point d'entrée,
conservation de la sélection, nombre de décisions avant le premier résultat,
édition directe sur canvas, visibilité de la hiérarchie et du temps, feedback,
undo et retour au document. Un parcours est `implémenté` seulement si son
workspace permet de créer, observer et corriger le résultat sans reconstruire
manuellement des identifiants ou changer de contexte sans indication.

L'audit initial révélait deux parcours distincts mais mal raccordés. Les
workspaces Entity et Animation ferment désormais ces ruptures avec des actions
contextuelles, une manipulation directe et une preuve graphique sauvegardée.

| Tâche observée | Parcours Vertex Loom au 2026-09-03 | Coût/rupture | Référence UX convergente | État UX | Cible vérifiable | Priorité |
| --- | --- | --- | --- | --- | --- | --- |
| Créer une Entity depuis un visuel | Cmd/Ctrl-sélection de plusieurs textures, vectoriels ou composants puis `Create Entity from N visuals` ; tous les nœuds sont publiés en une commande | Aucun identifiant n'est ressaisi et le résultat devient immédiatement la ressource active | Godot/Unity/Construct créent ou composent depuis hiérarchie, projet ou canvas ; GameMaker accepte le drag d'asset | implémenté | Maintenir le test de composition multi-visuels | P2 |
| Construire la hiérarchie | Arbre récursif persistant, sélection canvas/arbre/inspecteur synchronisée, Cmd/Ctrl multi-sélection, drag de reparentage, ordre et déplacement groupé atomique | Une cible primaire reste explicite ; les nœuds verrouillés sont exclus du déplacement groupé | Scene/Hierarchy/Outliner/Project bars rendent parent, sélection et drop persistants | implémenté | Maintenir l'E2E graphique de déplacement groupé et le rejet des cycles | P2 |
| Créer une animation d'Entity | Depuis l'Entity, `Add animation clip...` conserve cible et nœud ; le clip créé ouvre immédiatement canvas + dock Timeline | Le menu technique reste disponible pour les clips génériques, sans pénaliser le parcours contextuel | Godot ouvre l'Animation Panel depuis l'AnimationPlayer ; Rive/Spine passent en mode Animate ; Unity ouvre Animation sur l'objet sélectionné | implémenté | Conserver cette transition dans l'E2E Entity→Animation | P2 |
| Poser la première clé | Boutons `Key Position/Rotation/Scale/Pivot` sur le nœud actif ; la valeur courante et le playhead créent la piste typée sans saisir de binding | Le formulaire de binding reste disponible comme outil avancé, pas comme passage obligé | Godot/Unreal ajoutent piste et clé depuis l'icône de la propriété ; Unity/GameMaker enregistrent les changements au playhead | implémenté | Maintenir le keying contextuel et sa sauvegarde E2E | P2 |
| Créer un mouvement A→B | Activer auto-key, déplacer le playhead puis le gizmo du canvas ; les poses successives fusionnent en une étape undo pendant le drag | La pose évaluée et le gizmo partagent le même évaluateur ; le formulaire A→B reste un raccourci avancé | Les éditeurs de référence font avancer le playhead, modifier l'objet, enregistrer la nouvelle pose | implémenté | Maintenir le drag auto-key réel et l'undo fusionné | P2 |
| Lire et corriger une animation | Dock Timeline redimensionnable : transport, playhead, zoom, pistes typées, marqueurs, sélection additive/rectangle, drag des clés et vue courbe interpolation/easing | La courbe échantillonne l'évaluateur commun et ne duplique pas la sémantique runtime | Tous les outils d'animation comparés exposent pistes + playhead + clés dans une timeline dédiée | implémenté | Maintenir la capture graphique avec courbe et sélection | P2 |
| Éditer la machine à états | Panneau dédié `Animation Graph` : états liés à des clips existants, transitions/conditions/priorités/exit time et preview déterministe à paramètres éphémères | Le graphe est séparé de l'inspecteur Entity et le projet E2E sauvegarde, recharge, valide puis affiche deux états et une transition | Godot, Unreal et Rive séparent graph/state machine de la timeline | implémenté | Conserver la simulation non persistante et l'E2E avec affichage réel | P2 |
| Se repérer et cadrer la preview | Coquille stable Project gauche, Viewer central, Inspector droit ; Fit, zoom, grille et fond explicites | Le même cadrage sert raster, vectoriel, Entity, Animation et composition ; probe réel normal et 900 × 600 | Les éditeurs Godot, Unity, Unreal, GameMaker et Construct gardent hiérarchie/projet, vue et propriétés dans des zones stables | implémenté | Maintenir ordre des zones, centre ≥ 320 px et commandes du Viewer dans les probes | P2 |

### Architecture de panneaux cible

| Zone persistante | Entity workspace | Animation workspace |
| --- | --- | --- |
| Explorateur gauche | Assets filtrables et drag source | Clips de l'Entity et ressources compatibles |
| Arbre gauche/centre | Nœuds, parenté, ordre, visibilité, lock | Même arbre en lecture/sélection ; pistes sous chaque nœud |
| Canvas central | Sélection directe, déplacement, rotation, scale, drop | Preview au playhead, gizmos enregistrables, onion skin optionnel |
| Inspecteur droit | Propriétés du nœud sélectionné ; clé à côté des propriétés animables | Valeur de la clé/piste sélectionnée, interpolation et easing |
| Dock bas | Diagnostics et dépendances sur demande | Transport, playhead, dope sheet, marqueurs et courbes |

Les modales ne doivent plus porter la composition. Elles restent limitées au nom,
au choix d'un template ou à une confirmation destructive. Les réglages de
binding brut, tangentes, composition additive et création A→B restent
disponibles dans un volet `Avancé`, sans bloquer le parcours direct.

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
| Entités | A15 — entité/prefab, arbre et overrides d'instance | `entity.hpp`, arbre récursif, multi-sélection et réparation de drawable typée, `map.hpp`, ADR-0071/0075/0089/0147 | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | partiel | partiel | Composition, hiérarchie, overrides et références manquantes disposent d'un parcours direct ; les contrats restent partagés avec le runtime | P2 | Maintenir validation de cycle, réparation typée et atomicité des groupes | G1, U1, E1, G8, U8, C6 |
| Entités | A16 — transformation atomique A→B avec transfert d'état | `entity_transformation.hpp`, ADR-0115, tests runtime | oui | oui | oui | implémenté | partiel | partiel | partiel | partiel | partiel | N/A | N/A | Capacité spécialisée différenciante | P2 | Garder politiques versionnées et replay déterministe | G1, U1, E1 |
| Animation | A17 — clips, clés, interpolation, easing, segments, événements | `animation.hpp` v4, cues Audio typées résolues, timeline, tests contrat/runtime et E2E graphique, ADR-0039..0043/0121/0125/0126 | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | ✓ | Clés, courbes et marqueurs audio conservent un contrat commun Studio→runtime ; v3 migre sans son implicite | P2 | Maintenir résolution des événements Audio, fermeture de paquet et compatibilité v3 | G3, U3, E3, GM1, S1, R1, G8, U8, E8, GM4, S4, R4 |
| Animation | A18 — machine à états d'animation | Panneau `Animation Graph`, `animation_state_machine.hpp`, E2E Entity avec affichage réel + save/reload/validation, ADR-0039/0042/0045 | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | partiel | ✓ | partiel | ✓ | États, transitions typées et preview déterministe couverts ; `previewEntity` est une référence Studio souple pour éviter le cycle inverse | P2 | Maintenir la preuve E2E et les dépendances de clés comme références fortes | G3, U3, E3, R1 |
| UX Entity | A39 — composition contextuelle, arbre et édition directe | Création multi-visuels, arbre récursif avec reparentage, sélection synchronisée et déplacement groupé canvas vérifié en OpenGL réel ; ADR-0130/0147 | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | partiel | ✓ | partiel | partiel | Le parcours nominal ne passe plus par la modale de composition ; les commandes groupées restent undoables et validées atomiquement | P2 | Maintenir capture Entity, save/reload et sélection primaire explicite | G8, U8, E8, GM4, C6, S4, R4 |
| UX Animation | A40 — workspace timeline, création de piste et keying contextuel | Key rapide, auto-key par gizmo sur pose évaluée, undo fusionné, sélection rectangle et courbes interpolation/easing ; E2E OpenGL avec save/reload ; ADR-0041/0147 | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | partiel | ✓ | ✓ | Le parcours nominal se fait depuis le nœud, le playhead et le canvas ; les bindings bruts restent avancés | P2 | Maintenir le scénario E2E réel et l'évaluateur unique de courbe | G8, U8, E8, GM4, C6, S4, R4 |
| UX Studio | A41 — coquille de panneaux et viewer pilotable | Project gauche, Viewer central, Inspector droit, Timeline basse ; Fit/zoom/grille/fond et probes graphiques normal/minimum ; ADR-0148 | oui | oui | N/A | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | partiel | partiel | La structure et le cadrage ne changent plus selon le type de ressource ; l'état du Viewer reste volontairement local au Studio | P2 | Maintenir le probe 900 × 600 et appliquer la même grammaire au shell Map Studio | G8, U8, E8, GM4, C6, S4, R4 |
| Rig | A19 — bones, skinning pondéré et déformation mesh | `entity.hpp`, ADR-0046/0097, tests déformation | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | partiel | partiel | ✓ | ✓ | Outils de peinture de poids moins complets | P1 | Ajouter métriques et édition visuelle des poids | G4, U3, E3, S1, R1 |
| Rig | A20 — IK FABRIK et ordre des contraintes | ADR-0043/0044/0096, tests contraintes | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | partiel | partiel | ✓ | ✓ | Bibliothèque de contraintes réduite | P2 | Étendre seulement sur cas utilisateur mesuré | G4, U3, E3, S2, R1 |
| Physique | A21 — XPBD et substeps/interpolation | Overlay canvas des particules et cinq familles, résumé erreur max/RMS/énergie, preset corde/tissu, `xpbd_tests`, E2E Entity réel, ADR-0098/0100/0139 | oui | oui | oui | implémenté | partiel | partiel | partiel | — | — | partiel | partiel | Contrat, solveur, interpolation et diagnostic d'auteur sont reliés ; les contraintes dures restent volontairement hors énergie compliante | P2 | Maintenir la fixture visuelle et les métriques déterministes sans les persister | G5, U4, E4, S1, R1 |
| Input | A22 — actions sémantiques, clavier/gamepad, remap | `input.hpp`, ADR-0055/0101/0119/0123, UI E2E | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | partiel | Touch/gestures non prouvés | P2 | Ajouter seulement avec cible plateforme | G1, U5, E5, C1 |
| Comportement | A23 — BehaviorGraph générique signaux/actions | `behavior_graph.hpp`, ADR-0114, scenario E2E | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | partiel | ✓ | N/A | ✓ | Debug pas-à-pas non prouvé | P1 | Ajouter trace de ports et breakpoint non persistant | G1, U1, E1, C1, R1 |
| Maps | A24 — map, layers, verrouillage, ordre, snapping et instances | `map.hpp`, ADR-0047/0070..0083/0134 | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | N/A | Peinture de masse moins productive | P2 | Mesurer avant d'ajouter brush/multi-placement | G6, U6, E6, GM1, C3 |
| Maps | A25 — tileset/tilemap/autotiling/terrain | Aucun type Studio ou schéma tile | non | non | non | absent | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | N/A | Création de grands niveaux répétitifs coûteuse | P1 | ADR tilemap après stabilisation du placement actuel | G6, U6, E6, C3 |
| Physique | A26 — collisions éditables, triggers et payloads | ADR-0048/0077..0080/0113, Map Studio E2E | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | bounding box | listeners | Formes/filtrage avancé limités | P2 | Ajouter catégories/masques seulement avec diagnostics | G5, U4, E4, GM2, C4 |
| Mécaniques | A27 — nœuds body/pivot/joint/motor | `MechanicNodeKind`, `mechanic_graph.cpp`, presets | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | N/A | Variétés de joints limitées au schéma actuel | P2 | Ajouter un type uniquement avec preset et runtime | G5, U4, E4, GM2, C4 |
| Mécaniques | A28 — nœuds sensor/constraint/event | mêmes contrats, compilateur Box2D, debug log | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | contraintes | listeners | Éditeur de graphe moins observable | P1 | Afficher handles résolus et dernier événement | G5, U4, E4, S2, R2 |
| Scènes | A29 — scène, campagnes, transitions et map montée | `scene.hpp`, ADR-0054/0111/0112, scene E2E | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | N/A | Transitions visuelles avancées absentes | P2 | Garder contrat générique avant effets dédiés | G1, U1, E1 |
| Caméra | A30 — Camera2D follow, limites et réglages runtime | ADR-0064/0122, runtime tests | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | N/A | Blend multi-caméra absent | P2 | Ajouter seulement si scènes le demandent | G1, U1, E1 |
| Audio | A31 — événements audio, volume, loop et mixer PCM | `AudioDocument v2`, panneau Audio atomique, `audio_mixer_tests`, Preview Runtime, smoke périphérique macOS réel du 4 septembre 2026 ; ADR-0062/0121 | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | timeline | audio events | Bus, panoramique, atténuation et boucle sont reliés ; formats compressés et rééchantillonnage restent volontairement hors contrat | P2 | Maintenir le test mixer déterministe et exécuter le smoke périphérique caché sur chaque plateforme de release | G1, U1, E1, GM3 |
| Replay | A32 — capture/relecture déterministe | `replay.hpp`, ADR-0057/0058, runtime tests | oui | oui | oui | implémenté | partiel | partiel | partiel | partiel | partiel | N/A | N/A | Vérification longue durée non prouvée | P2 | Ajouter checksum d'état sur session longue | G1, U1, E1 |
| Sauvegarde | A33 — progression et reprise | `progress_save.hpp`, ADR-0060/0061 | partiel | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | N/A | N/A | UX de slots non prouvée | P2 | Ajouter parcours utilisateur si plusieurs slots deviennent requis | G1, U1, E1, C1 |
| Performance | A34 — batching, culling, benchmark 60 FPS | Build Release natif Apple M1 Pro : renderer 795,993, runtime dense 125,812 et fixture textile 251,251 FPS p95 sur 600 frames ; rapport versionné, ADR-0066..0068 | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | métriques | métriques | Gate locale macOS prouvée sans skip ; Windows/Linux restent exigés séparément par la matrice de release | P2 | Conserver seuil 60 FPS, rapports natifs et refus des skips sur chaque plateforme | G1, U1, E1, S1 |
| Validation | A35 — validation unifiée, erreurs de champ, recovery | validateur CLI, ADR-0008/0020/0085, recovery smoke | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | partiel | partiel | Certains éditeurs restent moins guidés | P2 | Étendre focus/réparation aux types restants | G1, U1, E1 |
| Publication | A36 — paquet map/campagne portable, dépendances, version runtime | `map_package.cpp`, ADR-0103/0141, package tests | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | ✓ | ✓ | export runtime | export `.riv` | Signature/contenu public encore gated | P0 | Fermer licences, signature et runner GPU avant tag | G7, U7, E7, C5, S3, R3 |
| Observabilité | A37 — logs JSONL, overlay, diagnostics render/physics | `TraceContext` session/ressource propagé Map Studio→PreviewRuntime et runtime publié, tests JSONL/runtime, ADR-0010/0145, benchmark | oui | oui | oui | implémenté | ✓ | ✓ | ✓ | partiel | partiel | métriques | listeners | Chargement, échec et résumé runtime partagent désormais la corrélation locale, sans télémétrie réseau | P2 | Maintenir `sessionId` sur une session entière et `resourceId` à chaque changement de map/scène | G1, U1, E1 |
| Sécurité | A38 — validation imports, chemins relatifs, licences/SBOM | tests traversée, ADR-0144/0146 | oui | oui | oui | partiel | ✓ | ✓ | ✓ | partiel | partiel | export | export | Trois preuves de redistribution restent bloquantes | P0 | Ne pas publier avant statut `approved` réel | G7, U7, E7 |

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

### Groupes d'ADR

| Groupe | ADR | Lignes |
| --- | --- | --- |
| Gouvernance, format, validation, stockage | 0001–0020 | A01–A04, A35, A37 |
| Vecteur et rendu | 0022–0037 | A04, A08 |
| Matériaux, animation, rig | 0038–0046 | A05–A07, A17–A20 |
| Map, runtime, input, replay, audio, caméra, perf | 0047–0085 | A22, A24–A34 |
| Prompts, entités, preview, simulation | 0086–0103 | A14–A21, A24–A28 |
| Composition, chemins, comportements, publication | 0104–0135 | A09–A16, A22–A23, A29, A31, A36 |
| Beam/Button/surfaces/release/UX | 0136–0148 | A06–A07, A10–A11, A21, A34, A36–A41 |

## Registre affirmation → source officielle

Consulté le 3 septembre 2026. Les capacités cochées signifient que la
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

## Conclusion et ordre de correction

La régression A07 a été corrigée et vérifiée sur affichage réel : les nouveaux
Beam et Button préservent la source sans effet ; la recoloration reste
explicite. A39 et A40 sont fermés par des parcours directs et des scénarios E2E
avec affichage réel, sauvegarde et rechargement. Le gate P0 juridique A38 reste
externe : aucune approbation de redistribution ne peut être fabriquée. Les
capacités absentes — rail cinématique, animation par chemin et tilemap —
nécessitent des ADR séparées ; cet audit ne les implémente pas implicitement.
