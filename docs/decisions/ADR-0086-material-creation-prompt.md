# ADR-0086 — Prompt de création des matériaux

- Statut : accepté
- Date : 2026-08-25

## Décision

Asset Studio expose `Create material / fill` comme une opération distincte de
l’import et de la création d’artwork. Le prompt valide le nom, l’identifiant
calculé, la couleur, l’opacité, le blend, les références locales optionnelles
de texture ou de motif vectoriel et la transform UV.

La confirmation construit un `MaterialDefinition v1`, le valide puis le publie
atomiquement dans `assets/materials/<id>.material.json`. L’identifiant est
résolu par suffixe en cas de conflit. Le matériau est ensuite indexé et devient
sélectionnable dans Asset Studio ; aucune rasterisation ni spritesheet n’est
créée.

## Conséquences

- Les références manuelles de texture et de vecteur sont refusées si elles ne
  correspondent pas à une ressource locale enregistrée.
- Les matériaux ont un propriétaire de document et un chemin stable comme les
  textures et artworks.
- L’aperçu spécialisé et l’édition continue du matériau restent des étapes
  ultérieures ; la création et la publication sont déjà headless et testées.
