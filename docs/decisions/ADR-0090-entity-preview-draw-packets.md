# ADR-0090 — Prévisualisation des drawables d’entité

- Statut : accepté
- Date : 2026-08-25

## Décision

Asset Studio résout l’entité sélectionnée en `VectorDrawPacket` dans le
viewport existant. Les drawables vectoriels utilisent la géométrie native ou
la conversion mémoire d’un SVG lié ; les drawables texturés utilisent les
dimensions du `TextureAsset` et un quad UV déterministe.

Les transforms du nœud et de ses parents sont composés avant le rendu. Le
matériau optionnel modifie la couleur, l’opacité ou remplace le fill par sa
texture. Les références d’image sont résolues par le cache OpenGL déjà utilisé
par les artworks natifs.

## Conséquences

- Une entité nouvellement créée devient inspectable visuellement sans
  conversion persistante.
- Les erreurs de résolution n’écrasent pas le document sélectionné et ne
  bloquent pas le reste de l’éditeur.
- Le batching global et le culling d’entités de Map Studio restent du ressort
  du Preview Runtime.
