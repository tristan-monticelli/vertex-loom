# ADR-0151 — Modularisation progressive de l’UX des Studios

## Statut

Accepté — 2026-09-04

## Contexte

Les corrections Entity, Animation, Behavior et Mechanic ont rendu plusieurs
parcours nominaux utilisables, mais elles ont aussi révélé une limite
structurelle. `editors/asset_studio/main.cpp` regroupe 12 770 lignes et
`editors/map_studio/main.cpp` 4 612 lignes. Ces deux unités mélangent boucle
d’application, état de présentation, navigation, panneaux, modales, rendu,
orchestration des sessions et harnais E2E. Des composants analogues — pickers,
erreurs de champ, raisons de désactivation, recherche et sélection — sont
réimplémentés dans les deux exécutables.

Cette organisation rend possible une correction locale, mais elle ne garantit
pas la cohérence d’une tâche qui traverse plusieurs ressources. Une map reste
éditée dans un écran différent des Entity et Animation qu’elle référence ; les
éditeurs Scene, Mechanics, Behavior Graph et Animation Graph s’ouvrent comme
fenêtres auxiliaires avec leurs propres états. Le contexte actif, la navigation
et les diagnostics n’ont donc pas encore une source de vérité UI commune.

Les documentations officielles de Godot, Unity, Unreal, GameMaker, Construct,
Spine et Rive convergent sur un modèle utile : navigateur ou hiérarchie,
surface centrale orientée tâche, inspecteur suivant la sélection et panneau
temporel/logique contextuel. Elles spécialisent les outils sans demander à
l’auteur de manipuler d’abord le schéma persistant.

## Décision

Le refactoring UX est nécessaire. Il sera progressif et préservera les
contrats, les fichiers projet et les sessions métier existantes.

La cible introduit un shell partagé pour les deux Studios avec :

- un `EditorContext` éphémère propriétaire du projet ouvert, des documents,
  de la sélection, de l’historique de navigation, du workspace actif et des
  diagnostics ;
- un registre d’actions contextuelles pour Save, Preview, Validate, Publish,
  Undo/Redo, création et ouverture, avec disponibilité et raison de blocage ;
- des composants partagés `ResourceBrowser`, `DocumentTabs`, `Hierarchy`,
  `Stage`, `Inspector`, `TaskDock`, `Diagnostics` et pickers typés ;
- des workspaces enregistrés par intention : Visual, Entity, Animation,
  Logic, Map, Scene, Rig/Physics et Publish ;
- une seule sélection logique synchronisée entre navigateur, hiérarchie,
  canvas, timeline, graphe et inspecteur ;
- une navigation retour/avant et des onglets de documents qui conservent le
  contexte sans dupliquer les documents persistants ;
- une palette de commandes recherchable qui invoque les mêmes actions que les
  menus, raccourcis et boutons ;
- des layouts adaptatifs sauvegardés comme préférences locales, jamais dans les
  ressources du projet.

Un workspace reçoit uniquement un contexte et des commandes de session. Il ne
lit ni n’écrit directement les fichiers et ne possède pas de seconde version
du document. Les IDs techniques restent disponibles dans un inspecteur avancé,
mais ne sont pas demandés dans un parcours nominal lorsque le système peut les
générer ou les résoudre.

Une intention métier visible sur plusieurs surfaces possède une seule action
dans le registre. Le bouton contextuel, la palette et un éventuel raccourci
invoquent son identifiant ; sa disponibilité et sa raison de blocage sont
calculées au moment de l'invocation. La création d'une Animation depuis le
ou des visuels sélectionnés vers une Entity sont les premières actions métier
portées sur ce contrat. Elles résolvent la sélection courante, ouvrent les
parcours de création existants et ne dupliquent aucune mutation de session.
L'ouverture et la fermeture d'Animation Graph suivent le même contrat : le
workspace ne conserve que son état ouvert et son état courant, tandis que
`toggle_animation_graph` porte l'intention et sa disponibilité.

`EditorContext` conserve pour chaque document l'identifiant de sélection
primaire et, lorsqu'elle existe, la sélection multiple ordonnée. Les indices de
tableau utilisés par un widget ne sont qu'une projection recalculée depuis ces
identifiants au début du frame. Une réorganisation, un undo/redo ou le retour
dans l'historique ne peut donc pas déplacer silencieusement l'inspecteur vers un
autre objet. Si l'objet primaire disparaît, le premier objet encore sélectionné
devient primaire ; si toute la sélection disparaît, le workspace choisit son
fallback visible et le republie dans le contexte.

La migration suit quatre tranches vérifiables :

1. extraire l’état d’application, les actions et les widgets partagés sans
   changement visuel ;
2. porter Resource Browser, navigation, sélection, Inspector et diagnostics
   sur le shell commun ;
3. porter Entity/Animation/Logic puis Map/Scene en conservant chaque E2E ;
4. ajouter les workspaces directs Rig/Physics et Publish seulement sur cette
   base, puis retirer les anciens chemins lorsqu’ils ne sont plus appelés.

La migration Entity commence par `EntityHierarchyWorkspace`. Ce composant
possède uniquement l'arbre, la sélection groupée et les gestes structurels ;
il reçoit l'état canvas et délègue toute mutation à `ProjectSession`. Les
coordonnées et drapeaux du harnais graphique passent par un petit adaptateur de
probe injecté, sans état de test global dans le composant.

`EntityNodeProperties` constitue la seconde frontière. Il reçoit une copie de
travail du nœud sélectionné, applique les contrôles communs et délègue chaque
validation à `ProjectSession`. Artwork, matériaux et overrides restent une
surface distincte afin que l'extraction ne recrée pas un inspecteur universel.

`EntityArtworkInspector` constitue la troisième frontière. Le choix du type de
drawable reste visible en mode guidé : une Entity vide ne doit pas exiger de
connaître le mode avancé pour recevoir son premier artwork. Le module porte la
réparation des références, la confirmation destructive des overrides, le
matériau, l'apparence, les variantes, ancres et valeurs typées. Il reçoit les
adaptateurs de sélection et d'ouverture de ressource sans posséder la preview
GPU ni l'état du shell.

`EntityWorkflowPanel` possède enfin l'état guidé/avancé et la tête de parcours
Entity → Animation/Logic. Il invoque les actions enregistrées au lieu de
dupliquer leurs mutations, et délègue le raccord Behavior à `ProjectSession`.
Le shell ne conserve que les adaptateurs de probe nécessaires aux E2E.

L'intention `create_behavior_for_entity` complète ce parcours. Elle mémorise
l'Entity cible, prépare un identifiant disponible, ouvre la création Behavior,
puis attache la ressource validée avant d'ouvrir son graphe. Bouton et palette
invoquent la même action ; une création manuelle reste indépendante.

`VisualComponentInspector` constitue la première frontière du workspace
Visual. Il possède les identifiants de sélection d'ancre et de paramètre ainsi
que leur remise à zéro lors d'un changement de document. Il reçoit le picker
typé partagé et ne persiste une modification qu'au travers de
`ProjectSession`; le shell conserve le preview et le routage de document.

`VisualCompositionLayerPanel` poursuit cette frontière sans absorber le crop
raster ni le preview. Il possède la sélection de layer et le brouillon d'ajout
(type et ressource), résout ces valeurs par identifiants au changement de
document et délègue l'ajout/duplication à `ProjectSession`.

`TexturedPathPenPanel` poursuit la même règle pour la géométrie d'un chemin.
Il possède l'identifiant du document et l'index de commande sélectionné,
protège les opérations contre un chemin vide ou le minimum de points, et
délègue l'édition des points, poignées, ajouts et suppressions à
`ProjectSession`. Le style shader, le preview d'animation et le crop raster
restent hors de ce module afin de conserver une frontière testable.

## Alternatives rejetées

- Réécriture complète immédiate : elle ferait varier simultanément structure,
  comportement et preuve, avec un risque élevé de casser la compatibilité.
- Continuer à ajouter des panneaux dans les deux `main.cpp` : cela augmente la
  duplication et ne résout pas les tâches traversant plusieurs ressources.
- Fusionner tous les contrats en un document unique : la continuité UX ne
  requiert pas de migration destructive du modèle de ressources.
- Copier l’interface d’un moteur généraliste : Vertex Loom reste un moteur 2D
  spécialisé et doit reprendre les principes, pas la densité fonctionnelle.

## Critères d’acceptation

- Les fichiers `main.cpp` démarrent l’application et délèguent les workspaces ;
  ils ne contiennent plus les éditeurs de ressources complets.
- Un seul composant implémente les pickers typés, diagnostics de champ et
  raisons de désactivation utilisés par Asset Studio et Map Studio.
- Ouvrir Entity → Animation → Map puis revenir restaure document, sélection,
  playhead, zoom et panneau actif.
- La sélection d'une instance ou d'un nœud désigne le même objet dans toutes
  les surfaces visibles et ne repose pas sur un index statique partagé.
- Une sélection Entity multiple survit à la réorganisation des nœuds et au
  retour historique ; arbre, canvas et inspecteur résolvent le même nœud
  primaire par identifiant.
- `Animate selected node…` est découvrable dans la palette, partage son
  invocation avec le bouton et expose une raison de blocage sans Entity ou
  nœud sélectionné.
- `Create Entity from visual(s)` conserve la sélection multiple du Resource
  Browser dans son état de workspace et partage la même action entre bouton,
  menu contextuel et palette.
- Les parcours graphiques actuels restent verts pendant chaque tranche ; un
  nouveau E2E transversal prouve navigation, modification, reload, package et
  runtime publié avec affichage réel.
- Aucun schéma JSON ni identifiant existant n’est migré pour satisfaire ce
  refactoring.

## Conséquences

Les prochaines fonctions UX ne doivent plus être ajoutées directement aux deux
grands `main.cpp`, sauf correctif local indispensable avant extraction. Le
premier bénéfice attendu est la réduction des incohérences et non une réduction
arbitraire du nombre de lignes. Les anciennes surfaces peuvent coexister
temporairement avec le shell commun, mais une commande métier ne doit avoir
qu’un seul propriétaire. Le séquencement, les preuves et l'ordre des commits
sont décrits dans le
[plan de refactoring UX](../plans/editor-ux-modularization-2026-09-04.md).

La navigation contextuelle Map → Mechanics résout la mécanique depuis
l'instance sélectionnée, ouvre sa preview paramétrée par la commande de session
existante et active le document logique dans `EditorContext`. Elle ne crée pas
un second propriétaire de l'ouverture ou des overrides.

Le harnais du parcours Entity→Animation délègue sa preuve publiée à un module
sans UI. Après les gestes et le reload, ce module crée uniquement la map hôte
du test, ferme un paquet déterministe, charge ce paquet avec `PreviewRuntime`
et vérifie l'évaluation du nœud animé et de son événement. Il ne crée ni ne
modifie l'Entity ou l'Animation auditées.

Un outil canvas répétitif conserve son contexte tant que l'auteur ne le quitte
pas explicitement : ressource, couche et snapping restent stables, tandis que
les identifiants persistants sont régénérés à chaque création. Cette règle
s'applique d'abord au placement Map et évite que la production d'une série
d'instances repasse par le formulaire entre chaque clic.
