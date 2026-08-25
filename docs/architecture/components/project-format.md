# C4 Component — format projet

```mermaid
C4Component
    title Vertex Loom — composants du format projet
    Container_Boundary(project_library, "fabric_project") {
        Component(contracts, "Project contracts", "C++20", "ProjectManifest v2, DocumentHeader, ResourceReference, TextureAsset, MaterialDefinition v1, EntityDefinition v1, AnimationClip v1, SceneDocument v1, ReplayDocument v1, ProgressSave v1, PropertyDescriptorRegistry, AnimationStateMachine, AnimationConstraint, FABRIK IK, XPBD, mesh deformation, MapDocument v1, map events, fabric_physics shapes et VectorAsset v1/v2")
        Component(migrations, "Migration registry", "C++20", "Applique chaque conversion de schéma dans l'ordre")
        Component(serializer, "JSON serializer", "C++20 / nlohmann-json", "Convertit les contrats sans exposer la bibliothèque JSON")
        Component(registry, "ResourceRegistry", "C++20", "Indexe les documents et détecte doublons, absences, types incompatibles et cycles")
        Component(storage, "Atomic document storage", "C++20 / filesystem", "Remplace les documents éditables validés et publie les imports sans remplacement")
        Component(autosave, "Autosave storage", "C++20 / filesystem", "Écrit un miroir validé sous .vertex-loom/autosave et sélectionne les récupérations récentes")
        Component(validator, "Project validator", "C++20", "Valide versions, chemins, documents puis le graphe complet des ressources")
        Component(creator, "Project creator", "C++20 / filesystem", "Crée l'arborescence standard uniquement dans un emplacement absent ou vide")
    }
    Container(cli, "fabric_project_validate", "C++20 CLI", "Valide un projet sans ouvrir de fenêtre")
    ContainerDb(files, "Project Files", "JSON + assets", "project.json et ressources locales")
    Rel(cli, validator, "Demande la validation")
    Rel(validator, migrations, "Demande la mise à niveau")
    Rel(migrations, serializer, "Fournit le JSON courant")
    Rel(serializer, contracts, "Construit")
    Rel(validator, registry, "Enregistre et valide")
    Rel(registry, contracts, "Résout les références typées")
    Rel(storage, serializer, "Sérialise")
    Rel(storage, validator, "Valide avant écriture")
    Rel(validator, files, "Inspecte")
    Rel(storage, files, "Remplace atomiquement project.json")
    Rel(autosave, storage, "Réutilise le remplacement atomique")
    Rel(autosave, files, "Lit le principal et écrit son miroir")
    Rel(creator, validator, "Valide avant création")
    Rel(creator, storage, "Sauvegarde le manifeste")
    Rel(creator, files, "Crée les répertoires")
```

## Contracts

- `ResourceId` contient 1 à 128 caractères parmi les lettres ASCII minuscules,
  chiffres, tirets, points ou underscores, avec un caractère alphanumérique aux
  deux extrémités.
- `ProjectManifest` version 2 contient le nom du projet, `pixelsPerUnit` et ses
  répertoires relatifs d'assets, d'entités, de maps, de scènes et de schémas.
- `DocumentHeader` porte la version, le type, l'identifiant et le nom communs.
  `AssetDocument` reste son alias de compatibilité.
  `TextureAsset` version 1 ajoute un chemin PNG relatif, ses dimensions et le
  format de pixels `rgba8`.
- Une texture est déclarée par `assets/textures/<id>.texture.json` et sa source
  normalisée est `assets/textures/<id>.png`. Le document JSON est le marqueur
  de publication : une source sans document n'est pas un asset chargeable.
- `VectorAsset v2` est la version écrite. Un document v1 est accepté en lecture,
  conserve sa source `assets/vectors/<id>.svg` et devient
  `sourceKind = linkedSvg` sans modifier le fichier source.
- `sourceKind = linkedSvg` est actuellement chargeable et publiable.
  `sourceKind = native` stocke actuellement taille, origine, nœuds stables,
  visibilité, verrouillage, transform, rectangle ou ellipse et fill couleur ou
  transparent. Un fill image référence un `TextureAsset`, conserve son mode de
  cadrage, son transform indépendant, son opacité et son choix de suivre la
  déformation de la forme. Sa publication est atomique et ne crée aucun SVG.
- `MaterialDefinition v1` est stocké sous
  `assets/materials/<id>.material.json` et porte couleur, opacité, blend,
  transform UV, texture optionnelle et motif vectoriel optionnel.
- `EntityDefinition v1` est stocké sous `entities/<id>.entity.json` et porte
  des nœuds stables, parentage, transform, ordre Z, drawable vectoriel ou
  texture et matériau optionnel.
- `AnimationClip v1` est stocké sous
  `assets/animations/<id>.animation.json`. Il porte une durée, une boucle,
  des markers et des pistes liées par `nodeId + componentId + propertyId`.
  Les valeurs v1 sont scalaire, `Vec2`, couleur, booléen ou référence de
  ressource ; les interpolations disponibles sont step, linear et cubic.
- `PreviewRuntime` énumère les documents `*.animation.json` sous
  `assets/animations` avant l’initialisation SDL. Chaque document est chargé
  avec un chemin relatif au projet, validé puis indexé par `ResourceId` ; un
  document invalide empêche le chargement du runtime. L’évaluation headless
  d’un clip est disponible par identifiant et instant. Une instance de map peut
  le sélectionner via la propriété `animation`, directement ou par override de
  prefab ; le runtime applique alors les pistes de transformation supportées.
- `PropertyDescriptorRegistry` décrit les propriétés exposées par les
  composants, résout les bindings stables et filtre les propriétés animables
  et inscriptibles pour les outils d’édition.
- `AnimationStateMachine` relie des clips par états et transitions. Les
  conditions booléennes/numériques, `exitTime`, priorités et références de
  clips sont validés avant sélection déterministe.
- `AnimationConstraint` porte une dépendance source/cible et un ordre explicite
  pour `copy_transform`, `limits` ou `look_at`. Les cycles et rangs dupliqués
  sont refusés avant résolution.
- Le solveur FABRIK 2D conserve la racine, borne ses itérations, vérifie les
  longueurs de segments et traite explicitement les cibles hors de portée.
- Le système XPBD unifié expose distance, flexion, aire, pin et collision,
  conserve les lambdas, applique la compliance et quantifie l’état après
  chaque sous-pas.
- La déformation maillée applique les poses de nœuds aux sommets de repos par
  influences pondérées et valide les triangles et références de poses.
- `MapDocument v1` est stocké sous `maps/<id>.map.json` et sépare calques,
  prefabs, instances, collisions et triggers. Les instances sont indexées par
  chunks de `64 × 64` unités, événements nommés et les propriétés custom ont un
  ensemble de types fermé.
- Une propriété custom d’instance nommée `animation` est réservée à la
  lecture d’un `ResourceReference` de type `animation`. Elle est unique par
  instance, validée par le parseur MapDocument et résolue par le Preview
  Runtime ; l’absence du clip référencé empêche le chargement runtime.
- Un `PrefabDefinition` est une ressource logique de type `prefab` enregistrée
  par le validateur à partir de son `MapDocument`. Son identifiant, son
  `EntityDefinition` et ses overrides sont donc vérifiés par le graphe global,
  y compris lorsqu’une instance référence le prefab.
- `MapChunkIndex` maintient un ordre déterministe par chunk et identifiant et
  extrait les instances visibles d’un viewport sans accès disque.
- `SceneDocument v1` est stocké sous `scenes/<id>.scene.json`. Il référence les
  maps de la scène, son `entryMap` et des transitions vers d’autres scènes avec
  un point d’entrée. Toutes ces références sont vérifiées par le registre global.
- `ReplayDocument v1` est stocké sous `assets/replays/<id>.replay.json`. Il
  conserve le build, la seed, les entrées et événements ordonnés par frame,
  puis les checkpoints quantifiés à `1/4096` pour les positions et `1/65536`
  de tour pour les rotations.
- `ProgressSave v1` est séparé du projet d’authoring. Il conserve le build, la
  scène active et des propriétés typées (`bool`, entier, réel, texte, `Vec2`
  ou référence de ressource), puis les écrit par remplacement atomique dans
  le chemin utilisateur fourni par le runtime.
- Les chemins absolus, vides, traversants ou extérieurs au dossier projet sont
  refusés avant tout accès aux ressources, y compris après résolution des liens
  symboliques.
- Le chargeur refuse les versions de schéma non prises en charge.
- Le format prototype `v0` (`projectId`, `displayName`, `assetsPath`) migre vers
  `v1`, puis `v1` migre vers `v2` avec `pixelsPerUnit = 100`. Les anciens
  formats sont acceptés en lecture uniquement.
- La sauvegarde valide le manifeste avant de créer un fichier temporaire dans
  le dossier projet, puis remplace `project.json` par renommage atomique.
- La création refuse un emplacement existant non vide et ne remplace aucun
  fichier. Elle crée les répertoires déclarés avant la sauvegarde atomique du
  manifeste.
- Le validateur headless inspecte chaque document `*.texture.json` et refuse
  une source manquante, extérieure au projet ou incohérente avec le contrat.
- Le validateur headless applique les contrôles propres à `linkedSvg` ou
  `native` après migration de chaque `*.vector.json`.
- Le validateur headless charge aussi les documents matériau et entité,
  enregistre leurs références typées et refuse les cycles de parentage ou de
  dépendances.
- Après chargement, le validateur headless refuse les identifiants dupliqués,
  références manquantes, types incompatibles et cycles du registre.
- `ResourceRegistry` est instancié par chargement de projet, sans singleton ;
  `register_resource`, `resolve` et `validate` restent utilisables dans les
  tests et futurs chargeurs sans interface graphique.
- Un document éditable est validé par son parseur avant remplacement atomique.
  Les imports continuent à utiliser une publication séparée sans remplacement.
- `save_document_atomic` reçoit le chemin projet relatif, le document sérialisé
  et son validateur ; il crée uniquement des parents résolus dans le projet.
- Un autosave reproduit le chemin relatif du document sous
  `.vertex-loom/autosave/`. Il est récupérable seulement s’il est valide et
  strictement plus récent que le principal ; sa lecture ne modifie aucun
  fichier.
- Le stockage refuse les documents de plus de 256 Mio et les autosaves résolus
  hors du projet avant de charger leur contenu.
