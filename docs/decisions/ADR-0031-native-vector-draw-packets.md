# ADR-0031 — Draw packets vectoriels déterministes

- Status: accepted
- Date: 2026-08-25

## Decision

`fabric_render` expose `build_native_draw_packets`. La fonction aplatit la
géométrie native selon une tolérance explicite, conserve un packet par nœud et
triangule les contours simples par ear clipping déterministe. Les packets
transportent l’identifiant du nœud, les vertices, les indices, le fill, le
stroke, le parent et le clip.

La géométrie est dérivée en mémoire : aucune triangulation ni rasterisation
n’est ajoutée au document. Les formes invalides ou non triangulables produisent
un diagnostic headless.
