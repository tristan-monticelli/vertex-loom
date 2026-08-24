# Shared Core et format projet

## Responsabilités

`fabric_core` fournit les identifiants de ressources, la version du moteur et
la journalisation JSON Lines locale. `fabric_project` fournit le manifeste,
la sérialisation JSON, les migrations, la création sans écrasement, les
contrats `AssetDocument`, `TextureAsset` et `VectorAsset`, le chargement validé du dossier
projet et la publication atomique des documents.

## Entrées et sorties

- Entrées : texte JSON, chemin d'un dossier projet, `ProjectManifest` et
  `TextureAsset` ou `VectorAsset` C++.
- Sorties : contrats typés, rapport d'erreurs structurées, JSON versionné ou
  événements JSON Lines.
- Outil : `fabric_project_validate [--json] <project-directory>`.

## Invariants

- Seul le schéma courant ou une version disposant d'une migration explicite est
  chargé.
- Le manifeste renvoyé par `load_project` est l'instance exacte dont les
  répertoires ont été validés ; il n'est pas relu après le contrôle.
- Les identifiants sont stables, minuscules et indépendants des chemins.
- Tous les chemins du manifeste sont portables, relatifs et restent dans le
  dossier projet après résolution.
- Un manifeste invalide n'est jamais écrit.
- Une création accepte uniquement une destination absente ou vide et produit
  tous les répertoires déclarés ainsi que les quatre dossiers d'assets standard.
- Une texture publiée possède un document `<id>.texture.json` et une source
  `<id>.png` sous le dossier `assets/textures` déclaré par le manifeste.
- Un vecteur publié possède un document `<id>.vector.json` et une source
  `<id>.svg` sous le dossier `assets/vectors` déclaré par le manifeste.
- Le document d'une texture ou d'un vecteur est publié en dernier et aucun
  import ne remplace une ressource existante.
- Le remplacement de `project.json` se fait depuis un fichier temporaire
  adjacent et complet.
- Aucun journal n'est envoyé hors de la machine.

## Dépendances

`fabric_project` dépend publiquement de `fabric_core` et utilise en privé
`nlohmann/json` v3.11.3. Les contrats publics n'exposent aucun type de cette
bibliothèque JSON.

## Vérification

```sh
npm run validate:cpp
```

CTest couvre le round-trip JSON, la migration `v0` vers `v1`, les versions
futures, les chemins invalides, les documents de texture et de vecteur, la création sans
écrasement, la sauvegarde atomique, les dossiers complets, le logger et les
sorties du validateur headless.
