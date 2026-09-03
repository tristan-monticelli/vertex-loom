# C4 Component — parcours Studio et propriété des paramètres

```mermaid
C4Component
    title Vertex Loom — boucle UX d'authoring
    Container_Boundary(studio, "Studio") {
        Component(explorer, "Resource Explorer", "Asset Studio / Map Studio", "Recherche, filtre, sélection, duplication et actions contextuelles")
        Component(create, "Creation Prompt", "Dear ImGui", "Valeurs initiales et références typées")
        Component(inspector, "Inspector", "Dear ImGui", "Édition complète des paramètres persistés")
        Component(preview, "Preview Surface", "fabric_render / Preview Runtime", "Effet immédiat, erreurs et comparaison")
        Component(transition, "Document Transition", "SessionTransitionGuard", "Save, Discard, Cancel, Retry et atomicité")
        Component(validate, "Field Validation", "fabric_project", "Erreurs attachées au champ et diagnostics structurés")
    }
    ContainerDb(files, "Project Files", "JSON + assets", "Documents versionnés")
    Rel(explorer, transition, "Sélectionne ou administre")
    Rel(create, transition, "Crée sans perdre le document actif")
    Rel(inspector, transition, "Mutations undo/autosave")
    Rel(create, validate, "Valide les valeurs initiales")
    Rel(inspector, validate, "Valide les paramètres")
    Rel(validate, preview, "Autorise ou bloque la preview")
    Rel(preview, inspector, "Ramène vers le champ fautif")
    Rel(transition, files, "Publie atomiquement")
```

## Règle de propriété des paramètres

| Surface | Responsabilité obligatoire |
| --- | --- |
| Creation Prompt | Valeur initiale, référence typée, défaut documenté |
| Resource Explorer | Découverte, contexte, chemin, type, dépendances, actions |
| Inspector | Toutes les propriétés persistées, y compris celles ajoutées après création |
| Preview | Résultat visuel/runtime et état invalide explicite |
| Validator | Erreur par champ, chemin et suggestion de correction |
| CommandStack | Undo/redo, dirty, autosave et récupération |

## Parcours nominal

1. Ouvrir/créer le projet ; afficher le rail de ressources et l'état dirty.
2. Rechercher une ressource par nom, ID ou type ; sélectionner sans saisie libre.
3. Créer ou ouvrir ; sauvegarder automatiquement le document précédent s'il est valide.
4. Régler les paramètres dans l'inspecteur ; chaque changement passe par une commande.
5. Prévisualiser immédiatement dans la même surface et corriger les erreurs au champ.
6. Composer la map/scène avec les références existantes et publier après validation.
7. Lancer Preview Runtime ; toute divergence renvoie à la ressource et au paramètre concernés.

## Workspaces orientés tâche

Le triptyque explorateur/canvas/inspecteur reste stable ; un dock inférieur
change selon la tâche. La création ne doit pas ouvrir un formulaire contenant
la totalité du futur document.

| Workspace | Sélection conservée | Canvas | Inspecteur | Dock inférieur |
| --- | --- | --- | --- | --- |
| Entity | ressource et nœud | composition, sélection directe et gizmos | propriétés du nœud ; commandes de clé | diagnostics/dépendances |
| Animation | Entity, nœud, clip et playhead | pose évaluée et gizmos auto-key | propriété ou clé sélectionnée | transport, pistes, clés, marqueurs et courbes |

Le Resource Explorer fournit `Créer une Entity depuis la sélection`. Le
workspace Entity fournit `Animer…`. Une propriété animable fournit une icône de
clé qui crée la piste typée si nécessaire. Les paramètres de binding brut,
tangentes, composition additive et segment A→B sont avancés, jamais requis pour
la première animation. Voir ADR-0147.

La première tranche conserve le layout desktop existant et matérialise le dock
Timeline sous le canvas lorsqu'un clip est actif. L'action contextuelle de
l'explorateur prépare l'Entity depuis le visuel courant ; elle ne duplique ni ne
convertit la ressource source. Le dock rend les pistes et clés sur un axe de
temps manipulable et réutilise les commandes undoables de `ProjectSession`.
La lecture, le scrub et le déplacement des clés restent des états d'interface ;
seule une commande de clé validée modifie le document.
La preuve E2E Animation doit activer une commande de clé rapide dans l'interface,
sauvegarder, recharger et vérifier la piste typée correspondante avant de
capturer le workspace avec un contexte OpenGL réel.

`Animation Graph` est un panneau dédié ouvert depuis l'Entity. Il édite des
états toujours associés à un clip existant, leurs transitions, conditions,
priorités et temps de sortie, puis simule le choix déterministe avec des
paramètres éphémères. Il ne mélange pas ces paramètres de preview au document.
L'ancien formulaire de state machine dans `Advanced Entity systems` est retiré
du parcours : il pouvait tenter de sauvegarder un état sans clip valide.
L'E2E du panneau et celui du gizmo Entity s'exécutent successivement afin qu'une
fenêtre au premier plan ne masque pas artificiellement l'interaction canvas.

Le volet `Entity simulation` résume le système XPBD avec les erreurs max/RMS et
l'énergie compliante définies par ADR-0139. Le canvas superpose particules et
liaisons sans les confondre avec les contraintes d'animation de nœuds ; ces
diagnostics ne sont jamais sauvegardés dans l'Entity.

Le panneau d'une ressource Audio maintient un brouillon stable du document :
bus nommés, volume, boucle et spatialisation sont édités ensemble puis validés
atomiquement. Le choix `master` reste toujours disponible et un bus supprimé
réaffecte ses événements à `master`, ce qui évite les références cassées.

## Parcours d'échec

Une référence absente, une valeur hors domaine ou une écriture disque échouée ne
doit jamais fermer le document courant. L'interface affiche la ressource, le
champ et la cause, puis propose `Retry`, `Discard` ou `Cancel`.
# Asset Studio guided creation workspace

Asset Studio presents user-facing visual creations first: Beam, Button,
Artwork and composed Entity. Button always references an imported project PNG;
there is no generated Eye or generated Button preset. Internal resource types remain available through
an explicit advanced workspace so existing project contracts stay readable and
editable without making engine concepts part of the normal creation path.

Beam is the guided authoring type for the persisted textured-path contract.
The internal `beam` request is distinct from the legacy `seam` preset while
existing JSON and resource identifiers remain compatible. Its preview and
published rendering use the shared arc-length ribbon geometry builder; the
manifest `defaultStrokeTexture` initializes newly created Beam and thread-based
presets, while a manually selected texture remains local to the selected
resource.

```mermaid
flowchart LR
    Hub[Guided creation hub] --> Beam[Beam]
    Hub --> Button[Button]
    Hub --> Artwork[Artwork]
    Hub --> Entity[Composed Entity]
    Hub --> Advanced[Advanced]
    Advanced --> Technical[Technical resources]
    Beam --> BeamContract[Beam request<br/>preserve_source par défaut<br/>ou recolor_from_detail]
    BeamContract --> Legacy[Compatible texturedPath contract]
    Beam --> Shared[Shared textured-path geometry]
    Beam --> BeamE2E[UI click, screenshot and reload proof]
    BeamE2E --> Shared
    Button --> ButtonTexture[Imported Button PNG]
    ButtonTexture --> ButtonMaterial[Material v2 appearance<br/>source intacte par défaut<br/>ou recoloration explicite]
    ButtonMaterial --> Entity
    ButtonMaterial --> Shared
    Button --> ButtonE2E[UI click, screenshot and reload proof]
    ButtonE2E --> Shared
    Shared --> Studio[Asset Studio preview]
    Shared --> Runtime[Preview and published runtime]
    Entity --> Blocks[Explicit visual blocks]
    Blocks --> EntityPreview[Composed Entity preview]
```
