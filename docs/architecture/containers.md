# C4 Container

```mermaid
C4Container
    title Vertex Loom — conteneurs
    Person(creator, "Créateur", "Développe et compose le jeu")
    System_Boundary(fabric, "Vertex Loom") {
        Container(runtime, "Game Runtime", "C++20 / SDL2 / OpenGL", "Charge projet ou paquet, normalise actions physiques, décisions IA et événements en signaux sémantiques, évalue les BehaviorGraph attachés sans distinction joueur/monstre, applique leurs actions et transformations atomiques, puis exécute animation, physique, triggers, audio, caméra et rendu")
        Container(asset, "Asset Studio", "C++20 / SDL2 / OpenGL / Dear ImGui", "Crée et personnalise assets, entités, InputDocument physiques, BehaviorGraph génériques et politiques de transformation ; prévisualise signaux et actions pas à pas")
        Container(map, "Map Studio", "C++20 / SDL2 / OpenGL / Dear ImGui", "Compose les maps, leurs prefabs et graphes mécaniques ; édite les paramètres de mécanique par overrides typés et inspecte leur simulation avant publication portable")
        Container(physics, "fabric_physics", "C++20 / Box2D v3.1.1", "Possède le monde physique, compile les graphes mécaniques validés, matérialise leurs capteurs, transporte le personnage de preview et expose un journal de debug borné")
        Container(core, "fabric_core", "C++20 static library", "Vec2, Color, Rect, Transform, identifiants de ressources et journaux structurés locaux")
        Container(projectlib, "fabric_project", "C++20 / nlohmann-json", "Contrats JSON stricts, dont BehaviorGraph v1 et EntityTransformation v1, registre et fermeture transitive des paquets")
        Container(editorlib, "fabric_editor", "C++20 static library", "Sessions et commandes partagées, dont édition BehaviorGraph avec historique, autosave, récupération et journal de preview borné")
        Container(renderlib, "fabric_render", "C++20 / SDL2_image / OpenGL", "Décodage PNG/SVG, constructeur partagé des draw packets RasterView, compositions, maps, géométrie, chemins texturés et batching stable")
        Container(projectcli, "fabric_project_validate / fabric_map_package_export", "C++20 CLI", "Valide un dossier projet et publie un paquet déterministe de map ou de campagne de scènes sans interface graphique")
        Container(renderbench, "fabric_render_benchmark", "C++20 / SDL2 / OpenGL", "Mesure le rendu d’une scène synthétique dense : packets, draw calls, triangles et p95")
        Container(runtimebench, "fabric_runtime_benchmark", "C++20 / Preview Runtime", "Crée un projet temporaire valide, charge une map dense et mesure culling, draw calls et p95 du runtime")
        ContainerDb(project, "Project Files", "JSON + assets", "Projet versionné et ressources sur disque")
        ContainerDb(mapbundle, "Portable Map Package", "map-package.json + MapDocument + dépendances", "Unité versionnée de publication ; déclare map racine, runtime minimal et chemins relatifs ordonnés")
    }
    Rel(creator, asset, "Crée et prévisualise")
    Rel(creator, map, "Compose et teste")
    Rel(creator, runtime, "Lance et joue")
    Rel(asset, core, "Utilise")
    Rel(map, core, "Utilise")
    Rel(runtime, core, "Utilise")
    Rel(asset, projectlib, "Lit et écrit")
    Rel(asset, editorlib, "Pilote une session")
    Rel(asset, renderlib, "Prévisualise les draw packets raster et vectoriels partagés")
    Rel(map, projectlib, "Lit et écrit")
    Rel(map, physics, "Construit et inspecte la physique")
    Rel(map, mapbundle, "Publie après validation")
    Rel(physics, runtime, "Fournit le monde physique")
    Rel(runtime, projectlib, "Charge")
    Rel(runtime, renderlib, "Rend les draw packets")
    Rel(runtime, mapbundle, "Charge sans conversion manuelle")
    Rel(projectcli, projectlib, "Utilise")
    Rel(renderbench, renderlib, "Mesure")
    Rel(runtimebench, runtime, "Charge et mesure")
    Rel(projectlib, core, "Utilise les types communs")
    Rel(editorlib, projectlib, "Valide et charge")
    Rel(editorlib, renderlib, "Valide les sources raster et vectorielles")
    Rel(renderlib, projectlib, "Construit les draw packets RasterView v1, VectorAsset v2 et TexturedPath v1")
    Rel(projectlib, project, "Valide, lit et écrit")
```

## Scope and assumptions

Les trois applications sont des exécutables desktop. Shared Core est une
bibliothèque sans état distant. Le premier squelette CMake expose les cibles
`fabric_core`, `asset_studio`, `map_studio`, `game_runtime` et
`fabric_core_smoke`. Le composant `fabric_project` et son validateur headless
constituent le premier contrat de données partagé. Asset Studio utilise
`fabric_editor` pour la session, une coquille SDL2/OpenGL/Dear ImGui et le
premier composant `fabric_render` pour décoder les aperçus PNG et SVG en RGBA8.
La tranche actuellement compilée décode les aperçus PNG/SVG. Le renderer cible
consomme `VectorAsset v2` et le modèle forme/fill/contour/clip d’ADR-0023. Le
pipeline sprite a été retiré par ADR-0025.

Pour `TexturedPath v1`, `fabric_render` aplatit les commandes Bézier avec une
tolérance explicite puis dérive un ruban, ses raccords, terminaisons, largeurs
et UV. Seuls le document d'auteur et sa texture appartiennent à
`fabric_project`; les sommets et indices restent un résultat de rendu
reproductible et non persisté.

La preview d'authoring d'une map résout ses instances, entités, composants et
animations en draw packets headless dans `fabric_render`. Map Studio dessine
ce résultat avec le backend OpenGL partagé ; Preview Runtime peut comparer le
même instant sans introduire un format de preview distinct.

Le Preview Runtime expose au code de jeu les événements de trigger et payloads
produits au dernier pas fixe, en complément de ses métriques de culling et de
performance.

Les triggers ne sont plus liés au personnage CLI : Preview Runtime construit
une liste d'acteurs à bounds monde pour chaque instance et pour le personnage,
puis `TriggerRuntime` suit l'occupation par acteur. Seules les collisions sensor
fermées sont des zones ; le payload d'événement est spécialisé par les
propriétés du trigger et chaque émission identifie l'acteur concerné.

Une scène monte toutes ses maps dans un `MapDocument` runtime éphémère. Le
`layer_id` de chaque référence sert de namespace aux identifiants locaux ; les
ressources projet et les événements compatibles restent partagés. Les points
d'entrée sont des instances marquées par `sceneEntryPoint`, résolues
atomiquement avec la scène cible avant qu'un nouveau Preview Runtime place le
personnage.

`fabric_project` publie séparément les paquets map (`map-package.json`) et les
paquets de campagne (`scene-package.json`). Le second ferme transitivement les
scènes cibles, leurs maps et assets tout en autorisant les boucles entre
scènes. Preview Runtime et `SceneRuntimeSession` détectent ce manifeste avant
SDL et exécutent la campagne sans projet source.

`fabric_physics` compile les sept nœuds intégrés de `MechanicGraph` en un plan
headless ordonné de corps, pivots, joints, moteurs, capteurs, contraintes et
liaisons vers les événements déclarés par la map. Ce plan ne contient aucun
identifiant Box2D persistant ; ces handles restent la propriété du monde
physique éphémère.

Map Studio ouvre un `MechanicGraph` dans une session distincte du document map.
Les mutations de nœuds, propriétés et connexions passent par son propre
`CommandStack`, puis la preview recompile et reconstruit entièrement le monde
Box2D. Lecture, pause, pas fixe et reset n'écrivent jamais d'état de simulation
dans le document.

La factory de plateforme tournante vit dans `fabric_editor` et assemble un
graphe générique validé ; `fabric_physics` ne connaît pas ce preset. La preview
évalue les signaux capteur ou événement, applique direction et accélération au
moteur, puis laisse Box2D imposer les limites du joint.

La preview peut ajouter un personnage dynamique générique au même monde. Les
formes sensor détectent physiquement ses entrées et sorties ; la friction des
contacts transporte le personnage. `fabric_physics` publie l'état courant de
chaque activation et un journal borné `begin/end`, consommés par les overlays
de Map Studio sans persistance de handles ou d'état simulé.

Un prefab inline de `MapDocument` peut référencer séparément une entité et un
`MechanicGraph`. Ses overrides ciblent les paramètres déclarés par le graphe ;
le validateur de projet vérifie identifiants et types, puis Map Studio compile
une copie paramétrée pour la preview sans modifier la ressource mécanique. Le
transform uniforme de l'instance déplace corps, pivots et capteurs dans le même
repère monde que son entité visuelle.

Preview Runtime possède une simulation Box2D mécanique par instance de prefab.
Il charge et compile ces graphes avant SDL, les avance avec le pas fixe du
runtime et projette la pose relative de l'unique corps lié à l'entité du prefab
sur les draw packets de l'instance. Les graphes invalides et les liaisons
visuelles ambiguës empêchent le chargement au lieu de produire une map
partiellement active.

Une tranche fonctionnelle suit la même direction de données dans les outils et
le runtime : contrat partagé, commande d'authoring, preview du studio,
sauvegarde dans le projet, composition dans Map Studio, puis chargement du
paquet de map. Une capacité disponible uniquement dans le runtime ne ferme pas
son gate produit.

`InputDocument` reste un contrat de périphériques : il convertit clavier,
gamepad et axes en identifiants d'actions libres. `BehaviorGraph` consomme ces
identifiants comme n'importe quel autre signal. Le graphe persistant, ses ports
et paramètres sont validés dans `fabric_project`; l'état éphémère des délais,
cooldowns et états appartient à un évaluateur par instance dans
`fabric_runtime`. Asset Studio pilote le même évaluateur pour sa preview.
