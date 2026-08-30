# ADR-0015 — Contrat et publication des textures PNG

- Status: accepted
- Date: 2026-08-24

## Context

L'aperçu PNG existant ne crée aucune ressource réutilisable. Asset Studio et le
runtime doivent partager un document versionné, des chemins portables et la
même règle de validité sans risquer d'écraser un asset existant.

## Decision

Définir `AssetDocument` et `TextureAsset` dans `fabric_project`. Une texture
version 1 est stockée sous `assets/textures/<id>.texture.json`, référence
`assets/textures/<id>.png`, déclare ses dimensions et le format `rgba8`.

`fabric_editor` orchestre l'import : `fabric_render` décode et borne le PNG,
puis `fabric_project` copie la source et publie atomiquement le JSON. Les deux
destinations doivent être absentes. Le document JSON est publié en dernier et
constitue le marqueur d'existence de l'asset pour les outils et le runtime.
Tous les chemins résolus doivent rester sous la racine canonique du projet.

## Alternatives

Référencer directement le fichier externe rendrait le projet non portable.
Stocker les pixels décodés dans JSON alourdirait le projet et détruirait la
source originale. Publier le document avant la source exposerait brièvement un
asset invalide aux lecteurs concurrents.

## Consequences

Un échec avant publication ne crée aucun asset chargeable. Un incident entre
la publication de la source et celle du document peut laisser un fichier PNG
orphelin, mais jamais un asset déclaré invalide ; une future commande de
maintenance pourra signaler ces fichiers. Les imports existants sont refusés
au lieu d'être remplacés.
