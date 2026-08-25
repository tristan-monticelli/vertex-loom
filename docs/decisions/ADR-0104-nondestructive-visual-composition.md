# ADR-0104 — Composition visuelle raster non destructive

## Statut

Accepté — 2026-08-25.

## Contexte

Les fills image actuels cadrent une texture par une géométrie vectorielle. Ce
modèle reste utile lorsqu'un masque est voulu, mais il ne doit pas devenir une
conversion imposée à toute image fournie. Les yeux, boutons, coutures et autres
détails doivent aussi rester éditables au lieu d'être aplatis dans la source.

## Décision

Un `TextureAsset` conserve les octets importés. `RasterView v1` référence cette
texture et porte uniquement un rectangle de crop en pixels source, un pivot, un
transform et le filtrage. Le crop est borné aux dimensions connues et reste
réversible ; sauvegarde, autosave et publication ne réécrivent jamais le PNG.

`VisualComposition v1` ordonne des calques raster, vectoriels, composants
paramétriques et chemins texturés. Chaque calque porte son ancrage, transform,
visibilité, opacité et ordre Z. Les anciennes références directes à une texture
gardent leur rendu en devenant une composition implicite à un seul calque sans
crop.

Une vectorisation ou un masque est une action explicite. La géométrie de rendu,
la source raster et la collision restent trois responsabilités séparées.

## Alternatives

Réécrire le PNG après crop détruirait l'information hors cadre et rendrait
l'undo dépendant de copies de pixels. Vectoriser automatiquement l'image
confondrait sa perception avec une géométrie approximative. Aplatir les
overlays reste autorisé comme cache d'export, jamais comme source éditable.

## Conséquences

- Asset Studio doit afficher la source complète et le crop actif.
- Undo/redo porte les paramètres de vue et de composition, pas les pixels.
- Les studios et le runtime utilisent les mêmes draw packets composés.
- L'export peut produire un cache aplati optionnel, mais le projet conserve
  toujours la composition éditable et ses sources.
