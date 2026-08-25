# ADR-0034 — Draw packets vectoriels en espace monde

- Status: accepted
- Date: 2026-08-25

## Context

Les documents natifs stockent la géométrie dans l’espace local de chaque nœud,
mais les aperçus et le runtime doivent obtenir le même résultat lorsqu’un
nœud possède une translation, une rotation, une échelle, un pivot ou un
parent. Laisser cette composition à chaque consommateur créerait des
divergences entre Asset Studio et le runtime.

## Decision

`build_native_draw_packets` applique le transform local à chaque sommet puis
remonte les transforms des parents jusqu’à la racine. Les `outline`,
`fill_vertices` et les indices de remplissage du packet sont donc en espace
monde. La composition utilise la même convention que le canvas Asset Studio :
pivot, échelle, rotation en degrés puis translation.

Les références de parent manquantes ou cycliques produisent un diagnostic
headless et aucun packet ambigu pour le nœud concerné.

## Consequences

Le futur backend OpenGL n’a pas à recomposer la hiérarchie pour dessiner les
silhouettes. Les transforms restent éditables et persistés localement ; le
cache de géométrie s’invalide via la sérialisation du document. Le transform
UV d’un fill image reste séparé et n’est pas appliqué aux sommets de la forme.
