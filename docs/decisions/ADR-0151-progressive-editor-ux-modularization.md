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

La migration suit quatre tranches vérifiables :

1. extraire l’état d’application, les actions et les widgets partagés sans
   changement visuel ;
2. porter Resource Browser, navigation, sélection, Inspector et diagnostics
   sur le shell commun ;
3. porter Entity/Animation/Logic puis Map/Scene en conservant chaque E2E ;
4. ajouter les workspaces directs Rig/Physics et Publish seulement sur cette
   base, puis retirer les anciens chemins lorsqu’ils ne sont plus appelés.

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
- La sélection d’une instance ou d’un nœud désigne le même objet dans toutes
  les surfaces visibles et ne repose pas sur un index statique partagé.
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
