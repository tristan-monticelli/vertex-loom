# ADR-0016 — Import SVG et aperçu borné

- Status: partially superseded by ADR-0022
- Date: 2026-08-24

## Context

Asset Studio doit conserver une source vectorielle portable, refuser les SVG
invalides et fournir un aperçu sans introduire un second décodeur non épinglé.
Un document SVG non fiable peut aussi être volumineux ou demander une surface
de rasterisation excessive.

## Decision

Définir `VectorAsset` version 1 dans `fabric_project`. Une ressource est stockée
sous `assets/vectors/<id>.vector.json` et référence la source originale
`assets/vectors/<id>.svg`. La publication refuse tout remplacement et écrit le
document en dernier.

Activer le backend SVG de SDL2_image `2.8.12`, qui embarque NanoSVG, sans ajouter
de dépendance FetchContent. `fabric_render` refuse les sources de plus de 8 Mio
et demande à SDL2_image un aperçu RGBA8 ajusté dans un carré de 2048 pixels. La
source SVG demeure le contrat de `VectorAsset v1` ; l'aperçu rasterisé n'est pas
persisté. ADR-0022 remplace cette source comme contrat futur par un document
vectoriel natif et migre les imports existants vers `sourceKind = linkedSvg`.

## Alternatives

Un parseur SVG maison serait fragile sur une entrée XML non fiable. Une nouvelle
bibliothèque vectorielle élargirait prématurément la chaîne de build. Convertir
la source en PNG détruirait l'éditabilité et le futur rendu indépendant de la
résolution.

## Consequences

L'import et la validation restent portables dans la pile SDL2 existante. Le
premier aperçu couvre le sous-ensemble SVG pris en charge par NanoSVG ; un futur
renderer vectoriel pourra lire la source inchangée sans migration du contrat.
