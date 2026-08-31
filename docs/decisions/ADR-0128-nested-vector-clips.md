# ADR-0128 — Clips vectoriels imbriqués dans le renderer OpenGL

## Statut

Accepté — 2026-08-26

## Décision

Lorsqu’un `VectorDrawPacket` référence un clip, le renderer reconstruit la
chaîne complète de ses ancêtres. Chaque forme de clip est rasterisée dans un
niveau stencil successif ; le contenu est ensuite testé contre le niveau final,
ce qui produit l’intersection de tous les clips.

La chaîne est reconstruite par packet afin de ne pas laisser l’état stencil
d’un packet contaminer le suivant. Une référence absente ou un cycle est
rapporté dans `RenderStats::errors` et le packet concerné est ignoré. Lorsqu’un
contexte ne fournit pas de tampon stencil, le renderer applique la chaîne par
clipping CPU pour les silhouettes convexes, en interpolant les UV ; un clip
non convexe est diagnostiqué plutôt que rendu sans masque.

## Conséquences

- Les clips peuvent être imbriqués à plusieurs niveaux sans modifier le
  format `VectorDrawPacket`.
- Le smoke test OpenGL vérifie l’intersection d’un clip parent et d’un clip
  enfant ainsi que les pixels intérieur/extérieur.
- Le coût de rasterisation augmente avec la profondeur de la chaîne ; les
  packets sans clip continuent d’utiliser le batching existant.
