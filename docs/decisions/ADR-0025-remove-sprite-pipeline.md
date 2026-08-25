# ADR-0025 — Retrait du pipeline sprite

- Status: accepted
- Date: 2026-08-25
- Supersedes: ADR-0021 et la politique de compatibilité d’ADR-0022

## Context

Vertex Loom est désormais fondé sur `VectorAsset v2` : géométrie native,
formes, fills et transformations animables. Le lecteur Aseprite, le découpage
de planches, le packer d’atlas et `SpriteSheetDefinition v1` ne sont référencés
par aucun document cible. L’utilisateur a confirmé explicitement leur retrait.

## Decision

Retirer le pipeline sprite de bout en bout : contrats, validation, session,
interface, rendu, tests, documentation de composant et dépendance zlib.

Les fichiers `.sprite.json`, `.aseprite`, `.source.png` et `.atlas.png` ne sont
plus reconnus ni produits. Aucune migration automatique n’est créée puisqu’il
n’existe aucun projet utilisateur placé dans le périmètre de migration.

Les PNG restent des textures locales utilisables comme fills. Les SVG restent
des sources vectorielles liées. Les animations futures ciblent des propriétés
et des keyframes, jamais des frames d’atlas.

## Alternatives

Conserver le code masqué maintiendrait une dépendance, une surface de test et
deux modèles graphiques concurrents. Convertir automatiquement d’anciens
documents serait spéculatif sans projet réel à migrer.

## Consequences

Le build ne télécharge plus zlib directement. Asset Studio ne propose plus
d’import Aseprite ou de planche PNG. Le validateur ignore les anciens documents
sprite, qui devront être recréés comme textures ou artworks vectoriels si un
cas réel apparaît ultérieurement.
