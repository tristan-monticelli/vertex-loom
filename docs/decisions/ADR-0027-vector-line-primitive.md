# ADR-0027 — Primitive vectorielle ligne

- Status: accepted
- Date: 2026-08-25

## Context

`VectorAsset v2` sait déjà conserver les rectangles et ellipses, mais une
forme filaire simple ne peut pas être représentée sans la détourner en
rectangle. Le personnalisateur et le futur renderer doivent partager un
contrat explicite et déterministe.

## Decision

Ajouter `line` à `VectorShapeKind`. Une ligne stocke exactement deux points
dans `shape.points`, dans l’espace local du nœud. `bounds` reste obligatoire et
décrit l’enveloppe calculée des points afin de conserver l’indexation et le
cadrage communs.

Le parseur refuse une ligne qui ne possède pas exactement deux points, des
coordonnées non finies ou deux extrémités identiques. Les rectangles et
ellipses conservent leur représentation actuelle et ne stockent pas de points.

Le renderer natif dessine la ligne avec son contour de preview. Le remplissage
reste accepté dans le document pour préparer le modèle commun, mais ne produit
aucune surface pour cette primitive.

## Consequences

Le format reste JSON v2 sans rasterisation persistante. Les points sont
ordonnés et sérialisés dans l’ordre de leurs extrémités, ce qui rend le
round-trip déterministe. Les chemins Bézier et la tessellation générale
restent une étape ultérieure.
