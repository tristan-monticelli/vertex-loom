# ADR-0023 — Compositing forme, fill, contour et clip

- Status: accepted
- Date: 2026-08-24

## Context

L’esthétique visée repose sur une silhouette vectorielle nette qui peut
contenir une couleur, un motif ou une image. Le contour et le contenu doivent
rester éditables et animables séparément. Assimiler une image remplissante à un
sprite rendrait le cadrage dépendant d’un atlas et confondrait géométrie,
matériau et source raster.

## Decision

Chaque drawable natif compose quatre responsabilités explicites :

- `shape` : géométrie locale qui définit la silhouette ;
- `fill` : couleur, image locale, motif ou matériau référencé ;
- `stroke` : couleur ou matériau, largeur, jointure et extrémité ;
- `clip` : forme optionnelle limitant le fill et les descendants.

Une image de fill est une `ResourceReference` vers une texture locale validée.
Elle possède son propre transform UV, son opacité et un mode de cadrage
`contain`, `cover`, `stretch` ou `free`. Le transform du fill est indépendant du
transform du nœud : déplacer l’image dans le masque ne déplace pas le contour.

Le renderer aplatit les courbes selon le zoom, triangule les contours simples
de façon déterministe, produit le masque puis dessine fill et stroke en ordre
stable. Les auto-intersections, références absentes, dimensions non finies et
clips cycliques sont refusés avant rendu.

Les propriétés exposées à l’animation utilisent des identifiants typés et
stables : transform du nœud, transform du fill, opacité, couleur, largeur de
stroke et références compatibles. Elles ne sont pas découvertes par le nom
d’un champ JSON.

## Alternatives

Précomposer la forme et son image en PNG détruit l’éditabilité et multiplie les
ressources dérivées. Utiliser `<image>` et les règles complètes du SVG comme
runtime élargit le contrat à un standard que le moteur ne peut pas valider ou
animer entièrement. Partager un unique transform entre forme et fill empêche le
cadrage artistique attendu.

## Consequences

Le personnalisateur doit fournir deux gizmos distincts et montrer clairement le
clip. Les draw packets et le validateur headless deviennent le contrat commun
entre aperçu et runtime. La première version reporte gradients complexes,
filtres SVG, trous multiples et opérations booléennes.
