# ADR-0022 — Authoring vectoriel natif et fin de la fondation sprite

- Status: accepted
- Date: 2026-08-24
- Supersedes: la phrase d’ADR-0016 faisant du SVG le contrat vectoriel futur
- Reclassifies: ADR-0021 comme compatibilité héritée uniquement

## Context

Le premier Asset Studio sait importer un SVG opaque et produire des
spritesheets déterministes. Ce flux valide le stockage et l’import, mais ne
correspond pas au produit cible : Vertex Loom doit créer, personnaliser et
animer des artworks vectoriels dont les formes portent aussi des images en
remplissage. Le SVG est un format d’échange, pas le modèle d’édition ni la
définition du rendu du jeu.

Le code Aseprite et atlas est déjà testé et publié. Aucun document d’entité,
de map, de scène ou de runtime ne dépend encore de `SpriteSheetDefinition`.

## Decision

Faire de `VectorAsset v2` le contrat graphique principal. Il possède un
`sourceKind` strict :

- `linkedSvg` conserve un SVG importé comme source opaque et prévisualisable ;
- `native` conserve directement les nœuds, formes, chemins, groupes, fills,
  contours, clips et transforms éditables.

La migration `v1 -> v2` est automatique et sans perte :

1. conserver le `DocumentHeader`, l’identifiant, le nom et le chemin `.svg` ;
2. remplacer `format = svg` par `sourceKind = linkedSvg` ;
3. ne produire aucun nœud natif et ne réécrire aucun octet SVG ;
4. conserver la publication et la résolution de ressource existantes ;
5. exiger une action explicite et un rapport de compatibilité pour convertir
   ultérieurement un SVG lié en document natif.

Le renderer cible consomme la géométrie native et ses draw packets. NanoSVG
reste le lecteur borné des SVG liés. Aucun atlas, frame ou spritesheet n’est
requis par Asset Studio, Map Studio, les entités, les animations ou le runtime.

`SpriteSheetDefinition v1`, le lecteur Aseprite et le packer restent compilés,
validés et chargeables comme compatibilité héritée. Ils ne reçoivent plus de
fonctionnalités et ne sont pas exposés comme voie de création cible. Leur
suppression nécessitera une confirmation distincte après inventaire des projets
réels et proposition de migration.

## Alternatives

Utiliser le DOM SVG comme document natif lie l’éditeur à un format trop large,
rend la validation et l’animation de propriétés ambiguës et expose des fonctions
non prises en charge. Continuer par spritesheets perd l’indépendance de
résolution et empêche l’édition du contour et du fill. Convertir automatiquement
tous les SVG serait destructif pour les éléments non compris.

## Consequences

Les prochaines étapes commencent par la migration du contrat vectoriel et le
renderer natif. Le flux sprite livré reste une dette explicite mais isolée. Les
tests de migration doivent prouver l’identité de la source SVG et le validateur
headless doit accepter `linkedSvg` et `native` sans fenêtre.
