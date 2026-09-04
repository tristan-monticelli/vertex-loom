# Plan de refactoring UX des Studios — 2026-09-04

Statut : approuvé par ADR-0151 ; aucune modification produit dans cette passe.

## Résultat attendu

Un créateur doit pouvoir traverser Visual → Entity → Animation/Logic → Map →
Scene → Publish sans perdre sa sélection ni apprendre les identifiants du
schéma. Le refactoring conserve les sessions, commandes, validateurs, formats
JSON, previews et runtimes. Il remplace progressivement l'orchestration UI.

## Baseline à préserver

- 45 tests Node et 91 tests C++ sont verts avant cette passe.
- Les E2E graphiques Entity/Animation, Animation Graph, Behavior Graph,
  Mechanic Graph, Map et accessibilité utilisent un contexte SDL/OpenGL réel.
- `asset_studio/main.cpp` contient 12 770 lignes et 1 824 appels ImGui.
- `map_studio/main.cpp` contient 4 612 lignes et 786 appels ImGui.
- Les modifications locales existantes de `CMakeLists.txt` et de la fixture
  `valid-project` ne font pas partie de ce plan.

Le nombre de lignes n'est pas un objectif. La réussite se mesure par la
propriété claire de l'état, l'absence de duplication et les parcours E2E.

## Tranche 1 — fondations partagées sans changement visuel

### Portée

- Ajouter un module UI éditeur partagé déclaré dans CMake et dans le C4 avant
  son code.
- Extraire les widgets sans état métier : nom de ressource, picker typé,
  recherche, erreur de champ, raison de désactivation et aide contextuelle.
- Introduire `EditorContext` pour documents ouverts, sélection stable,
  workspace et historique.
- Introduire `EditorActionRegistry` pour Save, Preview, Validate, Publish,
  Undo/Redo, création, ouverture et navigation.
- Remplacer progressivement les variables `static` de sélection par des états
  détenus par le workspace ou `EditorContext`.

### Preuves de sortie

- Tests unitaires sur identité de sélection, retour/avant et disponibilité des
  actions.
- Même rendu et mêmes artefacts E2E avant/après extraction.
- Aucun accès direct au filesystem depuis les nouveaux widgets.
- Un seul picker typé et une seule fonction de diagnostic utilisés par les
  deux Studios.

## Tranche 2 — shell, documents et sélection

### Portée

- Construire le shell partagé : Resource Browser, onglets, historique,
  barre d'actions, Stage, Inspector, Task Dock et status/diagnostics.
- Router chaque type de ressource vers un workspace enregistré.
- Restaurer par document la sélection, le zoom, le pan, le playhead, l'outil
  actif et l'ouverture des panneaux.
- Ajouter une palette de commandes qui invoque le registre d'actions ; menus,
  raccourcis et boutons utilisent exactement les mêmes commandes.
- Fournir un layout large et un layout compact, puis persister ces préférences
  hors du projet.

### Preuves de sortie

- E2E réel Visual → Entity → Animation → Map → retour Entity.
- Le nœud, le playhead, le zoom et l'outil sont identiques après retour.
- Changer une collection ne redirige jamais une sélection vers un autre index.
- Test 960 × 640 : le Stage garde sa taille minimale et les panneaux se replient.

## Tranche 3 — migration des workspaces de production

### Portée

- Porter Visual/Entity puis Animation et Logic hors de
  `asset_studio/main.cpp`.
- Porter Map et Scene hors de `map_studio/main.cpp`.
- Afficher Animation Graph, Behavior Graph et Mechanic Graph dans le Task Dock
  ou le Stage du document, pas dans des fenêtres sans historique commun.
- Unifier diagnostics et navigation vers une ressource, un objet et un champ.
- Conserver les sessions métier comme seuls propriétaires des mutations,
  undo/redo, dirty, autosave et recovery.

### Preuves de sortie

- Tous les E2E existants restent verts pendant chaque déplacement.
- Un scénario unique crée Entity et animation, place l'Entity dans une map,
  relie une mécanique, publie puis lance le runtime du paquet.
- Fermer un document sale offre toujours Save, Discard et Cancel sans perdre
  les autres documents.
- Les deux `main.cpp` ne contiennent plus d'éditeur de ressource complet.

## Tranche 4 — authoring direct manquant

### Portée

- Map : palette, duplication/multi-placement, collisions directes, bodies,
  pivots, joints, sensors et liens sélectionnables sur le canvas.
- Rig : bones, mesh, poids colorés, IK et contraintes éditables sur le Stage.
- Animation : box-select, déplacement/scale multi-clés, courbes et événements.
- Logic : erreurs sur ports/liens, trace live, pause, step et breakpoints.
- Publish : dépendances, validations cliquables, plateforme et résultat du
  runtime dans un workspace dédié.

Tilemap/navigation, Audio, Replay, VFX et UI jeu ne commencent qu'après les
outils directs P0 dont ils dépendent.

### Preuves de sortie

- Aucun document de test n'est préparé par API pour les gestes audités.
- Chaque outil direct est vérifié par geste UI, sauvegarde, reload et preview.
- Les diagnostics conduisent au handle, nœud ou champ fautif.
- Les résultats Studio, Preview et runtime publié utilisent la même fixture
  asymétrique et sont comparés sur les invariants utiles.

## Risques et parades

| Risque | Parade |
| --- | --- |
| Régression invisible pendant un déplacement de code | Une tranche par commit, artefacts E2E comparés et aucun changement visuel dans la tranche 1 |
| Deux propriétaires pour une mutation | Les workspaces invoquent les sessions ; aucune écriture de fichier ou copie mutable de document dans le shell |
| Contexte périmé après suppression/reparentage | Sélection par IDs résolus à chaque commande et état absent explicite |
| Shell trop général avant les besoins réels | N'extraire qu'un composant utilisé par au moins deux workspaces ou requis par un E2E transversal |
| Refactor interminable | Retirer l'ancien chemin dès que son remplaçant satisfait toutes ses preuves, sans migration de schéma |
| Copie excessive des grands moteurs | Conserver le périmètre 2D et limiter les workspaces aux tâches réellement supportées |

## Ordre des commits

1. `refactor(editor-ui): extract shared widgets and editor context`
2. `refactor(editor-ui): add document shell and action registry`
3. `refactor(asset-studio): move visual entity and logic workspaces`
4. `refactor(map-studio): move map and scene workspaces`
5. `feat(editor-ui): prove authoring to published runtime workflow`

Chaque commit doit passer `npm run validate`; les tests graphiques ignorés faute
d'affichage ne ferment aucun critère UX.
