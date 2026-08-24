# C4 Component — format projet

```mermaid
C4Component
    title Vertex Loom — composants du format projet
    Container_Boundary(project_library, "fabric_project") {
        Component(contracts, "Project contracts", "C++20", "ProjectManifest v2, DocumentHeader, ResourceReference, TextureAsset, VectorAsset et erreurs structurées")
        Component(migrations, "Migration registry", "C++20", "Applique chaque conversion de schéma dans l'ordre")
        Component(serializer, "JSON serializer", "C++20 / nlohmann-json", "Convertit les contrats sans exposer la bibliothèque JSON")
        Component(registry, "ResourceRegistry", "C++20", "Indexe les documents et détecte doublons, absences, types incompatibles et cycles")
        Component(storage, "Atomic JSON storage", "C++20 / filesystem", "Écrit un fichier adjacent puis publie manifestes et documents d'asset")
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
- Un vecteur est déclaré par `assets/vectors/<id>.vector.json` et sa source
  normalisée est `assets/vectors/<id>.svg`. Le document JSON est publié en
  dernier et constitue également le marqueur de validité de la ressource.
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
- Le validateur headless applique les mêmes contrôles aux documents
  `*.vector.json` et à leur source SVG.
- Après chargement, le validateur headless refuse les identifiants dupliqués,
  références manquantes, types incompatibles et cycles du registre.
- `ResourceRegistry` est instancié par chargement de projet, sans singleton ;
  `register_resource`, `resolve` et `validate` restent utilisables dans les
  tests et futurs chargeurs sans interface graphique.
