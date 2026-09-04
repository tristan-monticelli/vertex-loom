# Plan de refactoring UX des Studios — 2026-09-04

Statut : tranches 1 et 2 réalisées le 4 septembre 2026 ; tranche 3 en cours.

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

Preuves réalisées : `EditorContext` et `EditorActionRegistry` sont couverts
par `fabric_editor_foundations_tests`; `fabric_editor_ui` fournit les champs,
tooltips, raisons de blocage et le picker recherché commun. Les builds des deux
Studios et 13 E2E graphiques réels, dont Entity → Animation, Behavior,
Transformation, Scene et Mechanic, passent sans test ignoré.

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

Preuves réalisées : onglets et historique restaurent l'état de vue par
document ; menus, raccourcis, boutons et palette partagent le registre
d'actions ; les layouts auto/compact/large sont bornés et persistés hors du
projet. Les E2E minimum, accessibilité, Entity→Animation, Map et Scene passent
avec affichage réel.

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

Progression : Resource Picker, Scene, Mechanic et le canvas Map sont sortis de
`map_studio/main.cpp`. Les diagnostics de champ et pickers simples sont
partagés. Scene publie désormais sa campagne et la recharge avec le runtime du
paquet. Map, Scene et Mechanics partagent désormais la surface principale et
les onglets/historique d'`EditorContext` au lieu de trois fenêtres concurrentes.
Dans Asset Studio, Animation Graph est accessible directement depuis
l'Entity et remplace la timeline dans le Task Workspace contextuel au lieu de
s'ouvrir dans une fenêtre sans historique commun. Behavior Graph utilise le
Stage complet du document sélectionné et sa création est une action nominale
aux côtés d'Animation et Input. Entity Transformation suit le même modèle de
Stage et d'action nominale. L'extraction du code de ces workspaces, des autres
workspaces Asset Studio et du reste de l'inspecteur Map demeure ouverte.
Mechanics affiche désormais côte à côte le graphe logique et un canevas spatial
en unités monde. La sélection y est partagée et les corps, pivots et capteurs
se déplacent par glisser via `MechanicSession`; l'E2E prouve le glisser d'un
capteur, sa sauvegarde, son rechargement et la simulation du graphe reconnecté.
Behavior Graph remonte maintenant le signal de test au-dessus du canevas,
surligne les cartes visitées et fournit breakpoints éphémères, pause,
pas-à-pas de trace, reprise et reset. Son E2E effectue ajout, connexion,
breakpoint et évaluation uniquement par clics avant reload.
Sa sélection n'est plus un index statique : elle suit l'identifiant du nœud et
reste cohérente après duplication, suppression ou reconstruction du graphe.
Publish est désormais un module Map Studio dédié : racine Map/Scene,
fermeture et runtime minimal visibles, destination neuve obligatoire, puis
chargement et smoke d'une frame sur le paquet exact. L'E2E Mechanics enchaîne
ces clics et le runtime embarqué préserve les sous-systèmes SDL du Studio.
Entity propose maintenant `Create IK from selection` dans la barre du Viewer :
une commande atomique crée chaîne+cible, l'overlay montre os et liaison cible,
et les gizmos Entity déplacent cette cible. Le test réel couvre clic, capture,
sauvegarde/reload et le test de session couvre undo/redo.

## Tranche 4 — authoring direct manquant

### Portée

- Map : picker recherché, sélection multiple/rectangle, déplacement groupé,
  duplication, points de collision et placement continu avec ID automatique
  sont réalisés et prouvés par clics→reload ; joints et liens doivent ensuite
  devenir sélectionnables sur le canvas Map.
- Rig : création et cible IK de base réalisées ; création de bones, mesh,
  poids colorés et contraintes directes restent à faire sur le Stage.
- Animation : courbes avancées et événements ; box-select, déplacement groupé
  et `Alt`+glisser pour scaler une sélection autour du playhead sont réalisés.
- Logic : erreurs sur ports/liens, trace live, pause, step et breakpoints.
- Publish : workspace dédié Map/Scene réalisé ; le lancement du binaire release
  séparé et le gate de distribution restent à ajouter.

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
