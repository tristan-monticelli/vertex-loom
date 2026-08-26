# Checklist de remise à niveau complète du Studio

## Audit complet du 26 août 2026

### Périmètre et verdict

- [x] Contrats projet, stockage, validation et publication inspectés.
- [x] Boucle `game_runtime`, scènes, progression, triggers, physique, replay,
  audio, caméra et packaging inspectés.
- [x] Parcours Asset Studio et Map Studio inspectés dans le code.
- [x] Architecture C4, ADR, stratégie qualité, hooks et tests inspectés.
- [ ] Parcours graphiques exécutés avec un outil end-to-end : les binaires SDL
  locaux ne sont pas exposés au contrôleur d'applications macOS et le projet ne
  possède pas encore de harnais UX automatisé.

Verdict : **MANQUE**. Le socle de contrats et de stockage est solide, mais le
moteur n'est pas encore cohérent de bout en bout. Des fonctions déclarées dans
les contrats ou les ADR ne sont pas consommées par le runtime, et plusieurs
parcours d'édition peuvent bloquer ou perdre le contexte de travail.

### Backlog restant — état de référence

Cette section regroupe toutes les cases encore ouvertes. Une case ne doit être
cochée qu'après implémentation, test et preuve documentaire ; les sections
détaillées plus bas restent la source de suivi par fichier et par parcours.

- [x] Runtime projet : rendre personnage, spawn, caméra, limites et audio
  authorables depuis les documents, puis les consommer dans Preview Runtime.
- [ ] Transitions documentaires : centraliser le dirty guard, couvrir ouvrir/
  créer/fermer projet, garantir l'atomicité avant publication et tester clean,
  dirty valide/invalide, erreur disque et conservation du document.
- [ ] Suppressions et dépendances : confirmations avec analyse d'impact pour
  nœuds/map/ressources, remplacement de références, cascade explicitée,
  duplication superficielle/profonde, réécriture sélective et tests collisions,
  cycles, fichiers absents et erreurs disque.
- [ ] Textures : tests de navigation à grande échelle et vérification complète
  des performances de recherche et des noms similaires.
- [ ] Entités : confirmation des overrides incompatibles, gizmos de nœuds,
  glisser-déposer artwork vers nœud existant/racine/enfant, sections éditables
  contraintes/IK/déformation/XPBD/state machine.
- [ ] Animations : descripteurs transform/matériau/fill/image-fill/composants,
  geste A→B, sélection multiple, copier/coller, snapping, tangentes/easing,
  tests fill/image-fill, sauvegarde/reload/state-machine/runtime.
- [x] Input et comportements : décider par ADR les contextes/profils et afficher
  les BehaviorGraph consommateurs de chaque action.
- [ ] Vectoriel : contrat C4/ADR, conversion primitive→path, plume complète,
  poignées liées/symétriques/libres, sélection et transform multi-points,
  transform fill indépendante, clips imbriqués, registre animable, CommandStack,
  géométrie de stroke réellement rendue (largeur, join round/bevel/miter,
  cap), stroke image avec répétition/UV et preset pédagogique `beam` livré par
  défaut,
  géométrie, E2E canvas et comparaison des draw packets.
- [ ] Inspecteur : mêmes formulaires création/édition, raisons des boutons
  désactivés, focus premier champ invalide, tooltips, unités, raccourcis,
  tailles minimales, navigation clavier et contraste ; remplacer aussi les
  deux colonnes monolithiques de Map Studio par des panneaux hiérarchisés,
  redimensionnables et focalisés sur la sélection.
- [ ] UX E2E : IDs widgets stables, fixture multi-ressources, parcours texture/
  crop/input/joueur/monstre/path Bézier, diagnostics/screenshots et exécution
  macOS/Windows/Linux.
- [ ] Documentation et clôture : retirer les gates non prouvés UX, confirmer le
  résultat visuel/fonctionnel, puis cocher les lots 1–3, 5, 6, 7 et 8.

### Différences UX/logiques observées à corriger

- [x] Afficher dès l'accueil un état vide guidé : projet courant, ressource
  active, prochaine action et raccourcis disponibles.
- [ ] Garantir qu'ouvrir, créer, importer, dupliquer et fermer utilisent une
  transition unique `Save/Discard/Cancel/Retry`, sans perte de contexte.
- [ ] Montrer dans chaque picker le type, le chemin, la miniature, les
  dimensions, le format, les dépendances et une action d'ouverture.
- [ ] Remplacer tous les IDs libres restants par des sélecteurs typés avec
  recherche, état manquant et bouton de correction.
- [ ] Afficher les erreurs au champ avec cause, contrainte attendue et action
  de correction ; ne pas laisser un bouton désactivé sans explication.
- [ ] Unifier les composants de formulaire entre création et édition et
  conserver toute propriété créée dans l'inspecteur après publication.
- [ ] Rendre l'explorateur droit hiérarchique, redimensionnable, navigable au
  clavier et cohérent entre Asset Studio, Map Studio et scènes.
- [ ] Ajouter une analyse d'impact avant chaque suppression et proposer
  remplacement, cascade explicitée ou annulation.
- [ ] Permettre duplication superficielle/profonde et réécriture sélective des
  références internes.
- [ ] Ajouter gizmos de nœuds et drag-and-drop des artworks vers un nœud
  existant, un nœud racine ou un enfant.
- [ ] Exposer les contraintes, IK, déformation, XPBD et state machines dans
  des sections réellement éditables.
- [ ] Faire correspondre chaque paramètre de timeline au descripteur runtime,
  y compris transform, matériau, fill, image fill et composants.
- [ ] Ajouter clés A→B, sélection multiple, copier/coller, snapping,
  tangentes et easing selon un contrat versionné.
- [x] Permettre l'édition Bézier directement sur le canvas : plume, insertion,
  suppression, conversion ligne/courbe, ouverture/fermeture, poignées liées,
  symétriques et libres.
- [x] Rendre la géométrie de stroke effective : largeur, joins `round`,
  `bevel`, `miter` et caps doivent modifier les draw packets et le rendu ; le
  renderer les applique désormais dans les draw packets et les chemins OpenGL.
- [x] Ajouter le stroke image avec texture, répétition, UV, offset, échelle et
  déformation ; livrer un preset `beam` préexistant et visible comme exemple.
- [x] Permettre une configuration projet complète du personnage, spawn,
  caméra, limites et audio, puis la charger dans Preview Runtime sans CLI.
- [x] Ajouter des profils/contextes d'input seulement après ADR et afficher
  les BehaviorGraph qui consomment chaque action.
- [ ] Ajouter IDs de widgets stables, fixture multi-ressources, diagnostics et
  screenshots d'échec, puis exécuter les parcours UX sur macOS, Windows et
  Linux.
- [ ] Vérifier aux tailles minimales de fenêtre le focus, le scroll automatique,
  la navigation clavier, les unités, tooltips, raccourcis et le contraste.

### Défauts P0 — corriger avant toute nouvelle fonctionnalité

- [x] **Empêcher l'écrasement destructif des sauvegardes de progression.**
  `game/runtime/main.cpp` conserve désormais l'objet chargé et ses `properties`
  jusqu'au remplacement atomique final.
- [x] Restaurer la scène enregistrée et les propriétés du slot avant de lancer
  le runtime.
- [x] Permettre de reprendre avec `--save-slot` sans imposer une seconde fois
  `--scene`.
- [x] Ajouter un test d'intégration du binaire : slot existant → lancement →
  fermeture → contenu identique hors mutations explicites.
- [x] **Instancier les `MechanicGraph` dans Preview Runtime.** Map Studio les
  édite et les simule, les paquets les publient, mais `PreviewRuntime` ne charge
  ni ne compile les mécaniques référencées par les prefabs.
- [x] Supprimer l'affirmation « Preview Runtime instancie exactement le graphe »
  de l'ADR-0106 et du C4 tant que la tranche runtime n'est pas réellement
  livrée, ou livrer la tranche avant de conserver cette affirmation.
- [x] Ajouter un test paquet → runtime qui prouve le mouvement réel d'une
  plateforme mécanique, pas seulement la compilation isolée de son graphe.
- [x] **Protéger la fermeture de Map Studio.** Les événements de fermeture
  passent désormais par Save/Discard/Cancel pour la map et la mécanique dirty.
- [x] Tester fermeture fenêtre, raccourci système et erreur de sauvegarde avec
  conservation du document principal et de l'autosave.

### Défauts P1 — logique du moteur incomplète ou contradictoire

- [x] Charger toutes les `SceneMapReference` d'une scène au lieu de charger
  uniquement `entry_map`.
- [x] Définir et appliquer la sémantique de `SceneMapReference.layer_id`,
  actuellement sérialisé mais ignoré.
- [x] Appliquer `SceneTransition.entry_point` lors d'une transition ; ce champ
  est actuellement persisté puis ignoré par `SceneRuntimeSession`.
- [x] Ajouter un Scene Studio intégré à Map Studio. Il crée et ouvre les scènes,
  édite maps montées, map d'entrée et transitions, puis couvre undo/redo,
  autosave, récupération, validation et publication de campagne.
- [x] Publier une unité capable de contenir scènes et transitions.
  `ScenePackageManifest v1` ferme les scènes, maps et dépendances atteignables ;
  `fabric_map_package_export --scene` la publie et `game_runtime --package`
  l'exécute sans projet source.
- [x] Refuser un trigger lié à une collision `chain` ; les chains restent des
  segments physiques solides et ne définissent pas de zone fermée.
- [x] Exiger une collision sensor pour chaque trigger et appliquer la même
  règle dans le validateur, Box2D, Map Studio et le runtime.
- [x] Détecter une zone avec les bounds monde de l'acteur plutôt que son point
  central ; `triggerHalfExtents` permet une box physique explicite.
- [x] Permettre aux joueurs, monstres et autres entités de produire des entrées
  et sorties indépendantes, même sans `--character` ; `triggerActor=false`
  exclut explicitement les marqueurs.
- [x] Consommer `TriggerDefinition.properties` en surcharge du payload global
  et exposer l'édition de ces propriétés dans Map Studio.
- [x] Définir un système de comportements attachable aux entités, séparé des
  bindings physiques, puis l'utiliser pour le joueur comme pour les monstres.
- [x] Retirer le couplage runtime aux trois actions codées en dur
  `move_left`, `move_right` et `jump`.
- [x] Ajouter les transformations atomiques d'une instance d'entité A vers B,
  avec politique explicite de transfert d'état.
- [x] Rendre personnage, spawn, caméra, limites et audio authorables dans le
  projet. Ils sont aujourd'hui principalement injectés par options CLI.
- [x] Définir un contrat audio projet avec ressources, événements, volume et
  boucle ; le mixage runtime reste séparé du document.
- [x] Donner un contenu réel aux couches supportées (`visual`, `instances`,
  `collision`, `triggers`) et retirer de l'interface les couches `tiles` et
  `gameplay` tant qu'elles n'ont pas de stockage spécialisé.

### Défauts P1 — parcours Studio

- [x] Remplacer le blocage de `ProjectSession::select_resource` lorsque le
  document est dirty par la transition commune avec sauvegarde automatique.
- [x] Appliquer la même transition aux créations et imports ; la session
  sauvegarde désormais automatiquement le document valide avant de continuer.
- [x] Rendre le matériau sélectionné modifiable. Il est chargeable et
  créable, mais absent de `DirtyDocument`, de `save`, de l'autosave et de
  l'inspecteur d'édition.
- [x] Rendre le drawable d'un nœud d'entité modifiable après création : kind,
  ressource, matériau, composant, variante, overrides, visibilité et verrou.
- [x] Ajouter explicitement une entité cible lors de la création d'une
  animation et permettre de changer cette cible depuis l'inspecteur.
- [x] Ne plus utiliser la dernière entité sélectionnée comme cible implicite de
  preview d'une animation.
- [x] Transformer le Project tree en explorateur de ressources administrable.
  Il expose désormais l’arbre unifié, Duplicate, Rename, Reveal, Copy ID,
  analyse des références et Delete sécurisé dans le rail droit.
- [x] Inclure maps, scènes, mécaniques et replays dans cet explorateur unifié.
- [x] Remplacer les identifiants texte libres de Map Studio par des pickers
  typés et recherchables pour entités, prefabs, mécaniques et événements.
  Les références d’entités/prefabs/mécaniques utilisent le picker commun et les
  événements de triggers et de transitions sont sélectionnés dans les événements
  déclarés des maps montées.
- [x] Permettre d'ouvrir et de créer une map depuis Map Studio sans relancer
  l'outil avec des arguments CLI.
- [x] Ajouter confirmations et analyse d'impact avant les suppressions de
  nœuds, collisions, triggers, événements et autres ressources. Les nœuds
  vectoriels/entités et ressources ont déjà leur protection ; Map Studio
  confirme désormais montages, transitions, collisions, événements et
  triggers, bloque les références entrantes et teste l'undo de collision.
- [x] Synchroniser les états dirty de la map et de la mécanique dans un shell
  de document explicite ; les trois sessions partagent désormais le garde de
  fermeture et la sauvegarde avant package.

### Défauts P2 — vectoriel, input et ergonomie détaillée

- [x] Le prompt Input permet maintenant d'ajouter plusieurs actions et
  plusieurs bindings.
- [x] Ajouter Remove et Duplicate dans ce même prompt ; la suppression reste
  protégée lorsqu'il ne reste qu'une action.
- [x] Remplacer les codes numériques de touches par capture interactive et
  libellés lisibles ; ajouter axes, dead zones et seuils gamepad. Asset Studio
  capture clavier/manette, affiche les libellés SDL et persiste les contraintes
  d'axe, dead-zone, seuil et modificateurs.
- [x] Un vectoriel natif sélectionné expose nom, parent, clip, transform et une
  partie des paramètres de fill image après création.
- [x] Ajouter Add, Duplicate, Reorder et Delete pour les nœuds vectoriels.
- [x] Éditer bounds, points, commandes de path et poignées Bézier directement
  sur le canvas du `VectorAsset`. Le canvas natif expose déplacement d’ancres,
  poignées liées/symétriques/libres, insertion/suppression de commandes et
  transform bounds/scale/pivot ; l’E2E `asset_studio_vector_canvas_e2e` vérifie
  ajout, retrait, édition Bézier et persistance après reload. L’éditeur « Pen
  and attachments » de `TexturedPath` reste un parcours distinct.
- [x] Permettre de changer le type du fill après création et de choisir ou
  remplacer sa texture avec le picker commun.
- [x] Exposer ajout, retrait, couleur, largeur, join et cap du stroke.
- [x] Afficher les erreurs au niveau du champ dans Map Studio ; les valeurs
  texte parsées des payloads, triggers, instances, overrides et paramètres
  mécaniques affichent maintenant la cause, le format attendu et une correction,
  avec diagnostics d'id/nom sur les créations carte et scène. Vérifié par le
  build `map_studio` et les E2E `map_studio_close_e2e`, `map_studio_scene_e2e`,
  `map_studio_transformation_e2e`, ainsi que `fabric_map_session_tests`.
- [ ] Remplacer les deux colonnes monolithiques de Map Studio par des panneaux
  hiérarchisés, redimensionnables et focalisés sur la sélection courante.

### Qualité, architecture et maintenabilité

| Statut | Contrôle | Preuve |
| --- | --- | --- |
| CONFORME | Projet initialisé et doctrine disponible | `.project/project-config.json`, `AGENTS.md`, `CLAUDE.md` |
| CONFORME | Écritures projet atomiques et validation stricte | contrats `fabric_project`, tests de stockage et publication |
| CONFORME | Défenses de chemins et fermeture transitive des paquets | tests traversal, symlink et `map_package_tests.cpp` |
| CONFORME | Undo, autosave et récupération sur plusieurs documents | `ProjectSession`, `MapSession`, `MechanicSession`, `SceneSession` et tests headless |
| PARTIEL | End-to-end graphique | `map_studio_close_e2e` couvre la fermeture réelle et `map_studio_scene_e2e` l'authoring/reload/publication d'une scène ; les autres parcours restent à automatiser |
| CONFORME | Intégration progression dans `game_runtime` | `game_runtime_progress_resume` couvre reprise, conservation, invalidité et amorçage |
| CONFORME | Intégration mécanique dans Preview Runtime | chargement, compilation, pas fixe et mouvement visuel prouvés depuis un paquet |
| CONFORME | Fidélité du modèle Scene au runtime | Scene Studio, composition multi-map, mounts, entry points, transitions et paquet de campagne sont prouvés jusqu'au runtime |
| CONFORME | Architecture fidèle au code mécanique | ADR-0106, C4 et flux d'instance correspondent au runtime testé |
| MANQUE | Modularité des interfaces | `asset_studio/main.cpp` dépasse 4 000 lignes et `map_studio/main.cpp` 2 000 lignes |
| N/A | Backend, compte et réseau | produit local sans backend dans le périmètre actuel |

### Ordre de correction issu du nouvel audit

- [x] Lot A : non-régression des slots et reprise réelle.
- [x] Lot B : garde dirty commune Asset Studio / Map Studio / mécaniques.
- [x] Lot C : exécution runtime des mécaniques publiées.
- [x] Lot D : scènes multi-maps, entry points et paquet de scènes.
- [x] Lot E : triggers cohérents avec les formes et toutes les entités.
- [x] Lot F : BehaviorGraph, monstres et transformations d'entités.
- [x] Lot G : explorateur unifié, matériaux, drawables et animations ciblées.
- [ ] Lot H : vectoriel complet, input avancé et tests UX end-to-end.

Gate : aucun lot suivant ne commence tant que les tests du lot courant ne
prouvent pas le même comportement après sauvegarde, reload et exécution dans le
runtime publié.

## Règle de fermeture

Une case fonctionnelle ne peut être cochée que lorsque le même parcours est :

- défini par un contrat versionné ;
- éditable depuis le Studio propriétaire ;
- prévisualisable sans état de sélection caché ;
- couvert par undo, redo, dirty, autosave, récupération et sauvegarde atomique ;
- validé avant écriture ;
- rechargeable avec un résultat identique ;
- consommé par Map Studio et Preview Runtime lorsqu'il s'agit d'une capacité
  runtime ;
- couvert par des tests headless et un test end-to-end du parcours graphique ;
- documenté dans les C4 et ADR concernés ;
- livré dans un commit fonctionnel dédié.

Une validation technique verte ne ferme pas un gate UX.

## 0 — Verrouiller les parcours et le vocabulaire

- [x] Décrire les parcours de référence avant toute modification de code.
- [x] Définir le parcours « sélectionner une ressource interne ».
- [x] Définir le parcours « créer une ressource sans perdre le document actif ».
- [x] Définir le parcours « dupliquer une ressource et ses dépendances choisies ».
- [x] Définir le parcours « supprimer une ressource après analyse des références ».
- [x] Définir le parcours « créer une entité avec plusieurs artworks ».
- [x] Définir le parcours « ajouter ou remplacer un artwork après création ».
- [x] Définir le parcours « créer une animation pour une entité explicite ».
- [x] Définir le parcours « programmer un joueur et un monstre avec le même
  système logique ».
- [x] Définir le parcours « transformer une instance d'une entité vers une
  autre ».
- [x] Définir le parcours « créer et modifier intégralement un artwork
  vectoriel natif ».
- [x] Employer `Input bindings` uniquement pour les périphériques physiques.
- [x] Employer `Behavior` pour la logique d'une entité, humaine ou non.
- [x] Employer `Transformation` pour un remplacement d'entité avec transfert
  d'état explicite.

Gate : chaque parcours possède des entrées, sorties, erreurs, annulation et
critères de succès observables.

## 1 — Architecture de logique d'entité

- [x] Mettre à jour le C4 Context pour inclure l'authoring de comportements.
- [x] Mettre à jour le C4 Container avant d'ajouter le nouveau contrat.
- [x] Ajouter un diagramme de composants pour l'éditeur et l'évaluateur de
  comportements.
- [x] Ajouter un diagramme de séquence input/IA/événement → comportement →
  action → runtime.
- [x] Écrire un ADR pour `BehaviorGraph v1`.
- [x] Définir un document de comportement générique attachable à une entité.
- [x] Utiliser des identifiants stables pour nœuds, ports, paramètres et
  connexions.
- [x] Définir des ports et propriétés strictement typés.
- [x] Refuser les références absentes, types incompatibles et cycles interdits.
- [x] Définir les sources de signaux suivantes :
  - [x] action joueur ;
  - [x] décision IA ;
  - [x] événement map ;
  - [x] capteur ou trigger ;
  - [x] timer ;
  - [x] état ou propriété d'entité.
- [x] Définir les nœuds de contrôle suivants :
  - [x] condition ;
  - [x] branche ;
  - [x] séquence ;
  - [x] délai ;
  - [x] cooldown ;
  - [x] état ;
  - [x] transition.
- [x] Définir les actions suivantes :
  - [x] écrire une propriété ;
  - [x] émettre un événement ;
  - [x] lancer ou changer une animation ;
  - [x] appliquer un mouvement ou une impulsion ;
  - [x] activer une mécanique ;
  - [x] demander une transformation d'entité.
- [x] Ne coder aucune branche spéciale `player` ou `monster` dans l'évaluateur.
- [x] Permettre à une même action sémantique d'être produite par un joueur, une
  IA ou un événement.
- [x] Ajouter la sérialisation, le parseur strict et les migrations prévues.
- [x] Ajouter la résolution au graphe de ressources et aux paquets de map.
- [x] Ajouter une session d'édition avec CommandStack et sauvegarde atomique.
- [x] Ajouter un éditeur de graphe dans Asset Studio.
- [x] Ajouter une preview pas-à-pas avec journal borné des signaux et actions.
- [x] Évaluer le même graphe dans Preview Runtime.
- [x] Tester un comportement piloté par le joueur.
- [x] Tester le même comportement piloté par une IA de monstre.
- [x] Tester le même comportement piloté par un événement de map.
- [x] Tester le déterminisme après sauvegarde, reload et replay.

Gate : le Studio programme un joueur et un monstre sans ajouter de code propre
à l'un des deux dans le runtime.

## 2 — Transformation d'une entité vers une autre

- [x] Écrire un ADR pour `EntityTransformation v1`.
- [x] Choisir si la transformation est une action du BehaviorGraph ou une
  ressource réutilisable référencée par cette action.
- [x] Définir explicitement l'entité source et l'entité destination.
- [x] Définir une politique de transfert pour :
  - [x] transform monde ;
  - [x] identifiant d'instance ;
  - [x] couche et ordre Z ;
  - [x] vitesse et état physique ;
  - [x] propriétés d'instance compatibles ;
  - [x] paramètres de comportement ;
  - [x] animation et temps courant ;
  - [x] cooldowns et timers ;
  - [x] caméra et suivi éventuel.
- [x] Définir les propriétés incompatibles comme reset, mapping explicite ou
  erreur de validation.
- [x] Rendre l'opération atomique dans le runtime.
- [x] Empêcher une frame intermédiaire sans entité valide.
- [x] Préserver ou reconstruire proprement collision et mécanique.
- [x] Exposer un formulaire typé dans Asset Studio.
- [x] Permettre la sélection des entités source et destination depuis le
  Resource Explorer.
- [x] Prévisualiser la transformation dans Asset Studio et Map Studio.
- [x] Tester aller simple, aller-retour et chaîne de transformations.
- [x] Tester les références manquantes et cycles non autorisés.
- [x] Tester sauvegarde, reload, replay et publication de map.

Gate : une action configurée dans le Studio transforme une instance A vers B
avec la politique de transfert choisie, puis produit le même résultat après
reload et dans Preview Runtime.

## 3 — Transition uniforme entre documents

- [x] Écrire un ADR pour la politique de changement de document actif.
- [x] Remplacer les contrôles dispersés de `commands_.dirty()` par un service
  commun de transition.
- [x] Couvrir les actions suivantes :
  - [x] sélectionner une autre ressource ;
  - [x] créer une ressource ;
  - [x] importer une ressource ;
  - [x] dupliquer une ressource ;
  - [x] ouvrir un projet ;
  - [x] créer un projet ;
  - [x] fermer le Studio.
- [x] Sauvegarder automatiquement le document courant lorsqu'il est valide.
- [x] Continuer automatiquement lorsque la sauvegarde réussit.
- [x] Ne jamais bloquer silencieusement après remplissage d'une modale.
- [x] En cas d'échec, proposer `Retry`, `Discard` et `Cancel`.
- [x] Garder le document courant et son historique lorsque l'utilisateur annule.
- [x] Ne jamais publier partiellement la nouvelle ressource avant la résolution
  de la transition.
- [x] Afficher la ressource et le chemin concernés dans la confirmation.
- [x] Tester chaque action avec document clean, dirty valide et dirty invalide.
- [x] Tester l'échec d'écriture et la conservation du document principal.
- [x] Tester la création immédiatement après une édition non sauvegardée.

Gate : créer ou sélectionner une ressource sauvegarde l'ancienne sans blocage
si elle est valide et ne perd jamais les changements en cas d'échec.

## 4 — Resource Explorer unifié

- [x] Déplacer ou reproduire l'explorateur dans le rail droit selon la maquette
  validée.
- [x] Garder un seul composant de navigation réutilisable dans tous les prompts.
- [x] Afficher dossiers logiques, types, noms, identifiants et états dirty.
- [x] Ajouter recherche insensible à la casse et filtres par type.
- [x] Ajouter navigation clavier et sélection persistante.
- [x] Ajouter une barre d'actions contextuelle :
  - [x] New ;
  - [x] Import ;
  - [x] Duplicate ;
  - [x] Rename ;
  - [x] Replace references ;
  - [x] Reveal on disk ;
  - [x] Copy ID ;
  - [x] Copy path ;
  - [x] Delete.
- [x] Ajouter les mêmes actions dans un menu contextuel.
- [x] Définir une commande générique de duplication par type de ressource.
- [x] Générer un nouvel identifiant stable et un nouveau chemin à la duplication.
- [ ] Permettre de choisir la copie superficielle ou la duplication de certaines
  dépendances.
- [ ] Réécrire uniquement les références internes choisies dans la copie.
- [x] Analyser les références entrantes avant suppression.
- [x] Bloquer la suppression lorsqu'elle casserait le projet sans stratégie
  choisie.
- [x] Proposer remplacement des références, suppression en cascade explicitée
  ou annulation.
- [x] Demander confirmation avant toute suppression matérielle.
- [x] Ne jamais supprimer automatiquement une source externe ou partagée.
- [x] Ajouter undo pour les opérations récupérables du registre.
- [x] Tester les opérations sur chaque type de ressource.
- [x] Tester collision d'identifiant, cycles, fichiers absents et échec disque.

Gate : toutes les ressources peuvent être parcourues et administrées depuis le
même rail sans saisie manuelle d'identifiants.

## 5 — Textures et browsing cohérent

- [x] Utiliser le picker recherchable commun dans tous les champs texture.
- [x] Remplacer la combo simple du prompt vectoriel.
- [x] Permettre de changer la texture d'un fill image après création.
- [x] Permettre de choisir une texture pour un matériau après création.
- [x] Permettre d'ajouter ou remplacer une texture sur un nœud d'entité.
- [x] Afficher miniature, dimensions, format, chemin et dépendances.
- [x] Ajouter `Open in Resource Explorer` depuis chaque référence texture.
- [x] Conserver la source PNG byte-for-byte.
- [x] Garder crop, pivot, transform et filtrage non destructifs.
- [x] Rendre tous les paramètres de `RasterView` éditables après import.
- [x] Afficher simultanément source complète, crop actif et résultat final.
- [x] Tester navigation avec beaucoup de textures et noms similaires.
- [x] Tester changement de texture avec undo, redo, save et reload.

Gate : une texture peut être trouvée, ouverte, remplacée, recadrée et réutilisée
depuis chaque contexte sans saisir son identifiant.

## 6 — Matériaux entièrement éditables

- [x] Ajouter `material` aux documents dirty supportés par ProjectSession.
- [x] Ajouter le chemin courant, autosave et récupération des matériaux.
- [x] Ajouter une commande de mutation validée du matériau sélectionné.
- [x] Exposer après création :
  - [x] nom ;
  - [x] couleur ;
  - [x] opacité ;
  - [x] blend mode ;
  - [x] texture ;
  - [x] motif vectoriel ;
  - [x] offset UV ;
  - [x] échelle UV ;
  - [x] rotation UV ;
  - [x] pivot UV si le contrat le conserve.
- [x] Prévisualiser chaque changement immédiatement.
- [x] Afficher les entités et composants qui utilisent le matériau.
- [x] Ajouter undo, redo, dirty, autosave, récupération et sauvegarde atomique.
- [x] Tester chaque propriété individuellement et en combinaison.
- [x] Tester le matériau dans Asset Studio, Map Studio et Preview Runtime.

Gate : aucun paramètre disponible à la création ne devient immuable après
publication.

## 7 — Entités et artworks dans le rail droit

- [x] Remplacer la liste plate par un véritable arbre de nœuds d'entité.
- [x] Ajouter les actions de nœud : Add, Duplicate, Reparent, Reorder et Delete.
- [x] Demander confirmation avant suppression d'un nœud et afficher ses enfants.
- [x] Exposer un inspecteur complet du drawable de chaque nœud.
- [x] Permettre après création les kinds `none`, `texture`, `vector` et
  `visualComponent`.
- [x] Permettre `Add`, `Replace`, `Clear` et `Open` sur la ressource visuelle.
- [x] Permettre d'ajouter, remplacer ou retirer un matériau compatible.
- [x] Exposer variante, ancre et overrides d'un composant visuel.
- [x] Préserver les références compatibles lors d'un changement de kind.
- [ ] Demander confirmation avant d'effacer des overrides incompatibles.
- [x] Exposer transform, pivot, Z, visibilité et verrouillage du nœud.
- [ ] Ajouter sélection et gizmos de nœud dans le canvas d'entité.
- [ ] Permettre glisser-déposer d'un artwork depuis le Resource Explorer vers :
  - [ ] un nœud existant ;
  - [ ] un nouveau nœud racine ;
  - [ ] un nouveau nœud enfant.
- [ ] Exposer les contraintes, IK, déformation, XPBD et state machine dans des
  sections avancées éditables plutôt que seulement dans le contrat JSON.
- [x] Attacher explicitement un BehaviorGraph à l'entité.
- [x] Tester une entité multi-nœuds combinant texture, vectoriel et composant.
- [x] Tester duplication, reparentage, changement d'artwork et reload.

Gate : une entité vide peut être entièrement assemblée et modifiée depuis le
rail droit sans recréation ni édition JSON.

## 8 — Animations avec cible explicite

- [x] Écrire un ADR remplaçant la cible implicite issue de l'ancienne sélection.
- [x] Ajouter un sélecteur d'entité au prompt de création d'animation.
- [x] Permettre une animation générique sans cible uniquement comme choix
  explicite.
- [x] Persister une cible de preview ou une association versionnée adaptée.
- [x] Afficher et modifier la cible dans l'inspecteur du clip.
- [x] Ne jamais conserver silencieusement une entité sélectionnée précédemment.
- [x] Lister les nœuds réels de l'entité cible au lieu de demander `Node id` en
  texte libre.
- [x] Construire les propriétés depuis le registre de descripteurs de la cible.
- [ ] Exposer transform, matériau, fill, image fill et paramètres de composant.
- [x] Signaler immédiatement les bindings devenus invalides.
- [x] Ajouter une action pour réparer ou remplacer un binding invalide.
- [x] Permettre de créer le clip depuis l'entité sélectionnée.
- [x] Permettre d'ajouter un clip à l'entité depuis son rail.
- [x] Implémenter le geste valeur A au temps A → valeur B au temps B.
- [ ] Ajouter sélection multiple, copier/coller et snapping de clés.
- [ ] Ajouter tangentes et easing après décision de contrat.
- [x] Prévisualiser le clip sur l'entité dès sa création.
- [x] Tester création ciblée, changement de cible et cible absente.
- [ ] Tester changements de fill et transform d'image dans la timeline.
- [ ] Tester sauvegarde, reload, state machine et runtime.

Gate : créer une animation avec une entité affiche immédiatement cette entité
et ne dépend jamais de l'ordre antérieur des clics.

## 9 — Inputs physiques réellement personnalisables

- [x] Conserver `InputDocument` séparé du BehaviorGraph.
- [x] Ajouter des noms lisibles de touches et boutons à côté des codes persistés.
- [x] Permettre un nombre quelconque d'actions et de bindings.
- [x] Ajouter Add, Duplicate, Reorder et Remove dans la modale de création.
- [x] Générer des identifiants uniques au lieu de répéter `action`.
- [x] Empêcher qu'un nouvel item invalide rende toute la modale incompréhensible.
- [x] Ajouter capture interactive de la prochaine touche ou du prochain bouton.
- [x] Ajouter axes, seuils et dead zones.
- [x] Ajouter combinaisons et modificateurs.
- [x] Décider par ADR si des contextes ou profils sont nécessaires ; aucun
  besoin n'est confirmé pour le contrat d'input v2 actuel.
- [x] Afficher les BehaviorGraph qui consomment chaque action sémantique.
- [x] Permettre le remapping sans changer le comportement de l'entité.
- [x] Tester plusieurs bindings par action et plusieurs documents d'input.
- [x] Tester clavier, gamepad, axes, modificateurs, duplication et suppression.

Gate : l'utilisateur définit librement ses actions physiques, puis les branche
sur un comportement sans modifier le runtime.

## 10 — Personnalisateur vectoriel complet

- [x] Mettre à jour le C4 et écrire les ADR manquants avant les nouveaux
  contrats ou commandes. Le C4 de l’éditeur documente désormais le clipboard
  et le snapping de timeline ; `ADR-0126` et `ADR-0127` documentent ces
  changements et la suppression protégée des collisions.
- [x] Ajouter un arbre ordonné de nœuds vectoriels.
- [x] Ajouter les actions Add, Duplicate, Reparent, Reorder et Delete.
- [x] Ajouter les primitives rectangle, ellipse, ligne et path.
- [x] Permettre de changer une primitive en path éditable lorsque possible ;
  les rectangles, ellipses et lignes sont convertis en commandes de path, et
  les formes dégénérées sont refusées.
- [x] Ajouter un outil plume pour :
  - [x] ajouter un point ;
  - [x] insérer un point sur un segment ;
  - [x] déplacer un point ;
  - [x] supprimer un point ;
  - [x] convertir ligne en courbe ;
  - [x] convertir courbe en ligne ;
  - [x] ouvrir un contour ;
  - [x] fermer un contour.
- [x] Afficher et modifier les poignées Bézier directement sur le canvas.
- [x] Supporter poignées liées, symétriques et libres.
- [x] Ajouter sélection simple et multiple de points.
- [x] Ajouter déplacement, rotation et échelle d'une sélection de points.
- [x] Exposer les bounds des primitives.
- [x] Exposer le type de fill après création.
- [x] Permettre fill `none`, couleur et image à tout moment.
- [x] Permettre de changer texture, fit, offset, scale, rotation, pivot,
  opacité et déformation du fill image.
- [x] Rendre la transform du fill indépendante de celle de la forme.
- [x] Exposer ajout, retrait et modification du stroke.
- [x] Exposer couleur, largeur, join et cap du stroke.
- [x] Rendre effectivement largeur, join `round`/`bevel`/`miter` et cap dans
  le renderer ; les draw packets et les chemins OpenGL reflètent ces paramètres.
- [x] Ajouter un stroke image (texture, répétition, UV, offset et échelle) et
  fournir un preset `beam` préexistant pour expliquer le résultat attendu.
- [x] Exposer parent, clip, visibilité, verrouillage et ordre des nœuds.
- [x] Afficher les clips imbriqués fidèlement sur le canvas. Le renderer
  OpenGL construit les niveaux stencil de toute la chaîne parent/enfant,
  signale les cycles/références absentes et le smoke test
  `fabric_render_gl_smoke` vérifie l’intersection rendue.
- [x] Relier toutes les propriétés animables au registre de descripteurs.
- [x] Faire passer chaque geste par CommandStack avec fusion continue.
- [x] Tester chaque outil déjà livré en undo, redo, autosave, récupération et
  reload.
- [x] Ajouter des tests géométriques pour insertion et suppression de points ;
  les tests vérifient les points de segment, la tête `move` et le minimum de
  deux commandes.
- [x] Ajouter des tests end-to-end des interactions canvas.
- [x] Comparer les draw packets Asset Studio, Map Studio et Preview Runtime.

Gate : l'utilisateur crée et personnalise complètement un artwork natif,
incluant paths Bézier, fill et stroke, sans modifier le JSON ni préparer un SVG
externe.

## 11 — Cohérence générale de l'inspecteur

- [ ] Aucun paramètre proposé dans un prompt ne doit disparaître après création.
- [ ] Utiliser les mêmes composants de formulaire pour création et édition.
- [x] Afficher la validation au niveau du champ concerné.
- [ ] Afficher la raison exacte d'un bouton désactivé.
- [x] Conserver une barre de statut, mais ne pas y cacher une erreur bloquante.
- [ ] Ajouter focus et scroll automatiques vers le premier champ invalide.
- [ ] Ajouter tooltips pour les propriétés techniques.
- [ ] Afficher les unités pour toutes les valeurs numériques.
- [x] Utiliser les noms visibles comme interaction principale et les IDs comme
  information secondaire.
- [ ] Ajouter raccourcis clavier et commandes de menu cohérents.
- [x] Rendre les actions destructives visuellement distinctes.
- [ ] Vérifier le comportement aux tailles minimales de fenêtre.
- [ ] Vérifier navigation clavier et contraste.

Gate : les mêmes conventions de sélection, édition, validation et actions sont
utilisées pour tous les types de ressources.

## 12 — Tests UX et end-to-end

- [x] Choisir et documenter un outil d'automatisation pour SDL2/Dear ImGui
  (CTest avec fenêtres SDL cachées).
- [x] Mettre à jour `docs/02-quality-strategy.md` avec la commande réelle.
- [ ] Ajouter un mode de test avec IDs de widgets stables.
- [x] Ajouter une fixture de projet contenant plusieurs ressources de chaque
  type : `tests/fixtures/studio-textile-head` couvre désormais les ressources
  indexées, y compris input, behavior, matériau, audio et scène.
- [ ] Automatiser les parcours suivants :
  - [x] parcourir et sélectionner une texture ;
  - [x] modifier un crop puis créer une autre ressource ;
  - [x] vérifier l'autosave de l'ancienne ressource ;
  - [x] dupliquer et renommer une ressource ;
  - [x] analyser puis annuler une suppression ;
  - [x] ajouter un artwork à une entité existante ;
  - [x] créer une animation ciblant cette entité ;
  - [x] créer plusieurs actions et plusieurs bindings ;
  - [x] programmer un comportement de joueur ;
  - [x] programmer un comportement de monstre ;
  - [x] transformer une entité A vers B ;
  - [x] créer et éditer un path Bézier ;
  - [x] changer fill, texture et stroke après création.
- [ ] Capturer diagnostics et screenshots lors d'un échec.
- [ ] Exécuter les parcours sur macOS, Windows et Linux.
- [x] Garder les tests headless de contrats en complément, pas en remplacement.

Gate : les parcours qui ont échoué lors de l'audit sont exécutés
automatiquement sur les trois plateformes.

## 13 — Documentation et fermeture

- [x] Mettre à jour `docs/00-project-brief.md` si le périmètre BehaviorGraph et
  transformations change les critères de succès.
- [x] Mettre à jour C4 Context et Container avant chaque changement structurel.
- [x] Ajouter ou mettre à jour les diagrammes de composants concernés.
- [x] Ajouter un ADR par nouveau contrat ou politique persistante.
- [ ] Supprimer les affirmations de gate validé qui ne disposent pas d'une
  preuve UX reproductible.
- [x] Réconcilier cette checklist avec `remaining-roadmap.md`.
- [x] Réconcilier cette checklist avec `studio-first-vertical-slices.md`.
- [x] Exécuter `npm run validate` après chaque étape fonctionnelle.
- [x] Exécuter le smoke OpenGL après toute modification du renderer.
- [x] Exécuter les benchmarks après toute modification significative du rendu
  ou du runtime.
- [x] Auditer le diff, les voisins, les régressions et les effets de bord.
- [x] Vérifier `AGENTS.md`, `CLAUDE.md`, `.codex/hooks.json` et les hooks modifiés.
- [x] Vérifier qu'aucune suppression n'a été réalisée sans confirmation.
- [x] Faire un commit conventionnel par étape fonctionnelle vérifiée.
- [ ] Confirmer le résultat visuel et fonctionnel avant de déclarer le chantier
  terminé.

## Ordre d'exécution recommandé

- [ ] Lot 1 : transition uniforme entre documents.
- [ ] Lot 2 : Resource Explorer et duplication générique.
- [ ] Lot 3 : textures, matériaux et édition complète des drawables d'entité.
- [x] Lot 4 : cible explicite des animations.
- [ ] Lot 5 : BehaviorGraph et inputs physiques séparés.
- [ ] Lot 6 : transformation d'entité.
- [ ] Lot 7 : personnalisateur vectoriel complet.
- [ ] Lot 8 : tests end-to-end multiplateformes et réconciliation documentaire.

Le chantier est terminé uniquement lorsque les huit lots et tous leurs gates
sont cochés avec preuves de tests et commits associés.
