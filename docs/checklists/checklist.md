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
- [ ] Rendre personnage, spawn, caméra, limites et audio authorables dans le
  projet. Ils sont aujourd'hui principalement injectés par options CLI.
- [ ] Définir un contrat audio projet avec ressources, événements, volume,
  boucle et mixage ; `--audio <wav>` ne représente pas un pipeline de jeu.
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
- [ ] Remplacer les identifiants texte libres de Map Studio par des pickers
  typés et recherchables pour entités, prefabs, mécaniques et événements.
- [x] Permettre d'ouvrir et de créer une map depuis Map Studio sans relancer
  l'outil avec des arguments CLI.
- [ ] Ajouter confirmations et analyse d'impact avant les suppressions de
  nœuds, collisions, triggers, événements et autres ressources.
- [ ] Synchroniser les états dirty de la map et de la mécanique dans un shell
  de document explicite ; ils ont actuellement deux historiques indépendants
  sans garde de fermeture commune.

### Défauts P2 — vectoriel, input et ergonomie détaillée

- [x] Le prompt Input permet maintenant d'ajouter plusieurs actions et
  plusieurs bindings.
- [x] Ajouter Remove et Duplicate dans ce même prompt ; la suppression reste
  protégée lorsqu'il ne reste qu'une action.
- [ ] Remplacer les codes numériques de touches par capture interactive et
  libellés lisibles ; ajouter axes, dead zones et seuils gamepad.
- [x] Un vectoriel natif sélectionné expose nom, parent, clip, transform et une
  partie des paramètres de fill image après création.
- [x] Ajouter Add, Duplicate, Reorder et Delete pour les nœuds vectoriels.
- [ ] Éditer bounds, points, commandes de path et poignées Bézier directement
  sur le canvas du `VectorAsset`. L'éditeur « Pen and attachments » existant
  appartient à `TexturedPath` et ne remplace pas cet éditeur.
- [x] Permettre de changer le type du fill après création et de choisir ou
  remplacer sa texture avec le picker commun.
- [x] Exposer ajout, retrait, couleur, largeur, join et cap du stroke.
- [ ] Afficher les erreurs au niveau du champ dans Map Studio ; plusieurs
  formulaires utilisent encore un statut global et des valeurs texte parsées.
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
- [ ] Lot G : explorateur unifié, matériaux, drawables et animations ciblées.
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
- [ ] Remplacer les contrôles dispersés de `commands_.dirty()` par un service
  commun de transition.
- [ ] Couvrir les actions suivantes :
  - [x] sélectionner une autre ressource ;
  - [x] créer une ressource ;
  - [x] importer une ressource ;
  - [x] dupliquer une ressource ;
  - [ ] ouvrir un projet ;
  - [ ] créer un projet ;
  - [ ] fermer le Studio.
- [x] Sauvegarder automatiquement le document courant lorsqu'il est valide.
- [x] Continuer automatiquement lorsque la sauvegarde réussit.
- [ ] Ne jamais bloquer silencieusement après remplissage d'une modale.
- [x] En cas d'échec, proposer `Retry`, `Discard` et `Cancel`.
- [ ] Garder le document courant et son historique lorsque l'utilisateur annule.
- [ ] Ne jamais publier partiellement la nouvelle ressource avant la résolution
  de la transition.
- [ ] Afficher la ressource et le chemin concernés dans la confirmation.
- [ ] Tester chaque action avec document clean, dirty valide et dirty invalide.
- [ ] Tester l'échec d'écriture et la conservation du document principal.
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
- [ ] Ajouter une barre d'actions contextuelle :
  - [x] New ;
  - [x] Import ;
  - [x] Duplicate ;
  - [x] Rename ;
  - [ ] Replace references ;
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
- [ ] Proposer remplacement des références, suppression en cascade explicitée
  ou annulation.
- [x] Demander confirmation avant toute suppression matérielle.
- [x] Ne jamais supprimer automatiquement une source externe ou partagée.
- [x] Ajouter undo pour les opérations récupérables du registre.
- [ ] Tester les opérations sur chaque type de ressource.
- [ ] Tester collision d'identifiant, cycles, fichiers absents et échec disque.

Gate : toutes les ressources peuvent être parcourues et administrées depuis le
même rail sans saisie manuelle d'identifiants.

## 5 — Textures et browsing cohérent

- [x] Utiliser le picker recherchable commun dans tous les champs texture.
- [x] Remplacer la combo simple du prompt vectoriel.
- [x] Permettre de changer la texture d'un fill image après création.
- [x] Permettre de choisir une texture pour un matériau après création.
- [x] Permettre d'ajouter ou remplacer une texture sur un nœud d'entité.
- [ ] Afficher miniature, dimensions, format, chemin et dépendances.
- [ ] Ajouter `Open in Resource Explorer` depuis chaque référence texture.
- [x] Conserver la source PNG byte-for-byte.
- [x] Garder crop, pivot, transform et filtrage non destructifs.
- [ ] Rendre tous les paramètres de `RasterView` éditables après import.
- [ ] Afficher simultanément source complète, crop actif et résultat final.
- [ ] Tester navigation avec beaucoup de textures et noms similaires.
- [ ] Tester changement de texture avec undo, redo, save et reload.

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
- [ ] Implémenter le geste valeur A au temps A → valeur B au temps B.
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
- [ ] Ajouter contextes ou profils si leur besoin est confirmé par ADR.
- [ ] Afficher les BehaviorGraph qui consomment chaque action sémantique.
- [x] Permettre le remapping sans changer le comportement de l'entité.
- [x] Tester plusieurs bindings par action et plusieurs documents d'input.
- [x] Tester clavier, gamepad, axes, modificateurs, duplication et suppression.

Gate : l'utilisateur définit librement ses actions physiques, puis les branche
sur un comportement sans modifier le runtime.

## 10 — Personnalisateur vectoriel complet

- [ ] Mettre à jour le C4 et écrire les ADR manquants avant les nouveaux
  contrats ou commandes.
- [x] Ajouter un arbre ordonné de nœuds vectoriels.
- [x] Ajouter les actions Add, Duplicate, Reorder et Delete.
- [x] Ajouter les primitives rectangle, ellipse, ligne et path.
- [ ] Permettre de changer une primitive en path éditable lorsque possible.
- [ ] Ajouter un outil plume pour :
  - [ ] ajouter un point ;
  - [ ] insérer un point sur un segment ;
  - [ ] déplacer un point ;
  - [ ] supprimer un point ;
  - [ ] convertir ligne en courbe ;
  - [ ] convertir courbe en ligne ;
  - [ ] ouvrir un contour ;
  - [ ] fermer un contour.
- [ ] Afficher et modifier les poignées Bézier directement sur le canvas.
- [ ] Supporter poignées liées, symétriques et libres.
- [ ] Ajouter sélection simple et multiple de points.
- [ ] Ajouter déplacement, rotation et échelle d'une sélection de points.
- [x] Exposer les bounds des primitives.
- [x] Exposer le type de fill après création.
- [x] Permettre fill `none`, couleur et image à tout moment.
- [x] Permettre de changer texture, fit, offset, scale, rotation, pivot,
  opacité et déformation du fill image.
- [ ] Rendre la transform du fill indépendante de celle de la forme.
- [x] Exposer ajout, retrait et modification du stroke.
- [x] Exposer couleur, largeur, join et cap du stroke.
- [x] Exposer parent, clip, visibilité, verrouillage et ordre des nœuds.
- [ ] Afficher les clips imbriqués fidèlement sur le canvas.
- [ ] Relier toutes les propriétés animables au registre de descripteurs.
- [ ] Faire passer chaque geste par CommandStack avec fusion continue.
- [ ] Tester chaque outil en undo, redo, autosave, récupération et reload.
- [ ] Ajouter des tests géométriques pour insertion et suppression de points.
- [ ] Ajouter des tests end-to-end des interactions canvas.
- [ ] Comparer les draw packets Asset Studio, Map Studio et Preview Runtime.

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
- [ ] Ajouter une fixture de projet contenant plusieurs ressources de chaque
  type.
- [ ] Automatiser les parcours suivants :
  - [ ] parcourir et sélectionner une texture ;
  - [ ] modifier un crop puis créer une autre ressource ;
  - [x] vérifier l'autosave de l'ancienne ressource ;
  - [x] dupliquer et renommer une ressource ;
  - [x] analyser puis annuler une suppression ;
  - [x] ajouter un artwork à une entité existante ;
  - [x] créer une animation ciblant cette entité ;
  - [ ] créer plusieurs actions et plusieurs bindings ;
  - [ ] programmer un comportement de joueur ;
  - [ ] programmer un comportement de monstre ;
  - [x] transformer une entité A vers B ;
  - [ ] créer et éditer un path Bézier ;
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
- [ ] Ajouter ou mettre à jour les diagrammes de composants concernés.
- [x] Ajouter un ADR par nouveau contrat ou politique persistante.
- [ ] Supprimer les affirmations de gate validé qui ne disposent pas d'une
  preuve UX reproductible.
- [x] Réconcilier cette checklist avec `remaining-roadmap.md`.
- [x] Réconcilier cette checklist avec `studio-first-vertical-slices.md`.
- [x] Exécuter `npm run validate` après chaque étape fonctionnelle.
- [ ] Exécuter le smoke OpenGL après toute modification du renderer.
- [ ] Exécuter les benchmarks après toute modification significative du rendu
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
