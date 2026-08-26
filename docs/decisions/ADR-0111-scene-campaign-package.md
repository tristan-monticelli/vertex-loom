# ADR-0111 — Paquet portable de campagne de scènes

## Statut

Accepté — 2026-08-26.

## Contexte

`MapPackageManifest v1` possède obligatoirement une map racine. Ajouter une
scène comme ressource transitive ne suffit pas : le runtime ne connaît ni la
scène de départ ni les transitions à poursuivre depuis `--package`.

## Décision

Le paquet de campagne utilise `ScenePackageManifest v1`, sérialisé dans
`scene-package.json`. Il possède une `rootScene`, une version minimale du
runtime et la même liste triée de documents/payloads portables que le paquet de
map. Les deux formats restent distincts et rétrocompatibles ; un dossier de
paquet contient exactement l'un des deux manifestes.

La planification part de la scène racine, traverse toutes les maps déclarées,
les scènes cibles et leurs ressources transitives. Les cycles composés
uniquement de transitions de scènes sont autorisés : une campagne peut revenir
au menu ou boucler un niveau. Les cycles de ressources visuelles restent
refusés. Les prefabs inline sont résolus dans la map qui les déclare et ne
deviennent jamais des fichiers fictifs du paquet.

Preview Runtime détecte le type de paquet avant SDL. Pour un paquet de scène,
il charge `rootScene` par défaut ou la scène active demandée par la boucle du
jeu. `SceneRuntimeSession` possède une ouverture de paquet équivalente à son
ouverture projet ; `game_runtime --package` réutilise alors les événements,
transitions atomiques et points d'entrée sans accès au projet source.

## Conséquences

- `map-package.json` conserve strictement son schéma v1.
- `scene-package.json` est validé avant toute copie ou fenêtre.
- Une campagne publiée est autonome et ses transitions restent exécutables.
- Le même dossier ne peut pas être interprété à la fois comme paquet map et
  paquet scène.
