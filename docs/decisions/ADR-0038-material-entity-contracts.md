# ADR-0038 — Contrats matériaux et entités hybrides

- Status: accepted
- Date: 2026-08-25

## Decision

`MaterialDefinition v1` porte couleur RGBA, opacité, mode de blend, transform
UV et références optionnelles vers une texture ou un motif vectoriel.
`EntityDefinition v1` porte des nœuds stables, parentage, transform, ordre Z
et drawable `vector` ou `texture`, avec matériau optionnel. Les deux documents
utilisent un parseur strict, une validation finie, des chemins canoniques et la
sauvegarde atomique.

Les références déclarent leur type attendu. Les cycles de parentage, références
invalides, doublons d’identifiants et transforms non finies sont refusés avant
publication.

## Consequences

Les entités peuvent réutiliser les artworks natifs sans sprite ni atlas. Les
matériaux restent des documents indépendants, ce qui permettra au registre de
ressources et au runtime de résoudre les mêmes références.
