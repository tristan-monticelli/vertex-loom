# ADR-0018 — Manifeste v2 et contrats communs

- Status: accepted
- Date: 2026-08-24

## Context

Les éditeurs, la physique et le runtime doivent partager les mêmes unités,
couleurs, rectangles, transformations et références. Le manifeste v1 ne fixe
pas la conversion entre pixels d’authoring et unités monde. Les documents
d’assets ont déjà un en-tête commun mais sous un nom limité aux assets.

## Decision

Définir dans `fabric_core` les valeurs triviales `Vec2`, `Color`, `Rect` et
`Transform`. Les valeurs par défaut sont finies : position et rotation nulles,
échelle unitaire, pivot nul et couleur blanche opaque. La rotation est exprimée
en degrés.

Définir dans `fabric_project` `ResourceReference`, composé d’un `ResourceId` et
d’un type attendu, ainsi que `DocumentHeader`, composé de `schemaVersion`,
`type`, `id` et `name`. `AssetDocument` reste un alias de compatibilité vers
`DocumentHeader`.

Faire évoluer `ProjectManifest` en version 2 avec `pixelsPerUnit`, réel fini
strictement positif, égal à `100` par défaut. La migration v1 vers v2 ajoute
cette valeur sans modifier les autres champs. Les formats v0 et v1 restent
acceptés uniquement en lecture et migrent séquentiellement vers v2.

## Alternatives

Des types locaux par module provoqueraient des conversions et conventions
divergentes. Une constante de pixels codée dans les éditeurs ne serait ni
versionnée ni disponible au validateur headless. Modifier rétroactivement v1
empêcherait de distinguer les projets anciens.

## Consequences

Tous les futurs documents partagent des primitives et un en-tête stable. Un
projet v1 se charge comme v2 avec `pixelsPerUnit = 100`; toute sauvegarde écrit
le contrat courant v2.
