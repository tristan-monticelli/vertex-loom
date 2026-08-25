# ADR-0108 — Manifeste versionné des paquets de map portables

## Statut

Accepté — 2026-08-26.

## Contexte

La map est l'unité de publication de Vertex Loom. Le runtime et le futur
catalogue doivent pouvoir identifier le contenu d'un paquet avant de charger
ses documents, refuser une version incompatible et résoudre tous les fichiers
sans dépendre du chemin du projet d'authoring.

## Décision

Chaque paquet contient `map-package.json` à sa racine. `MapPackageManifest v1`
porte `schemaVersion = 1`, `type = "map-package"`, un identifiant et un nom, une
référence typée `rootMap`, ainsi qu'une `minimumRuntimeVersion` SemVer.

La liste `resources` est ordonnée par type puis identifiant. Chaque entrée
associe une référence typée à un `documentPath` relatif au paquet et à une
liste ordonnée de `payloadPaths` relatifs. La map racine doit avoir une entrée
de ressource. Aucun chemin absolu, traversant ou écrit avec des séparateurs
Windows n'est accepté.

Le parseur est strict : il refuse les champs inconnus, doublons de ressources,
collisions de chemins, listes non ordonnées et versions SemVer invalides. Un
runtime accepte le paquet lorsque sa propre version SemVer est supérieure ou
égale à `minimumRuntimeVersion` et que `schemaVersion` est supportée.

## Alternatives

Déduire le contenu en parcourant librement un dossier rendrait les paquets
ambigus et difficiles à vérifier. Copier `project.json` conserverait des
répertoires d'authoring inutiles et ne déclarerait pas la compatibilité runtime.
Une archive binaire propriétaire compliquerait le diagnostic et les tests
interplateformes sans bénéfice pour cette première version.

## Conséquences

- Le manifeste JSON est sérialisé de façon déterministe et testable headless.
- Le paquet reste un dossier portable ; son format d'archive éventuel est hors
  du contrat v1.
- La fermeture transitive produira les entrées et payloads du manifeste dans
  un incrément séparé.
- Preview Runtime et le futur catalogue partageront le même contrôle de
  compatibilité avant chargement.
