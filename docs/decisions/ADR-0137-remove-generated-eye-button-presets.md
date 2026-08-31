# ADR-0137 — Retrait des presets générés Eye et Button

## Statut

Accepté — 2026-08-31.

## Contexte

La factory historique exposait `VisualPresetKind::eye` et
`VisualPresetKind::button`. Elle inventait des formes vectorielles paramétriques
sans rapport avec les images originales du jeu. Le terme `Eye` était en plus
une mauvaise interprétation : ces originaux sont des boutons.

## Décision

Les types `eye` et `button` sont retirés de `VisualPresetKind`, ainsi que leurs
générateurs, leurs variantes et leur galerie. Asset Studio ne crée un Button
qu'à partir d'une texture PNG existante explicitement choisie dans le projet.
Il n'existe plus de type de création Eye.

Les contrats génériques `TextureAsset`, `EntityDefinition`,
`VisualComposition` et `VisualComponent` ne changent pas. Un ancien projet qui
contient déjà une composition ou un composant nommé `eye` reste donc lisible
comme ressource générique, mais le moteur ne possède plus de contrat ni de
factory spécialisés Eye/Button.

Les fixtures produit ne doivent plus générer d'œil ou de bouton vectoriel de
substitution. Les tests de Button doivent utiliser une texture importée.

## Conséquences

- Le parcours public contient Beam, Button, Artwork et Entity composée.
- Button référence le PNG original et ne synthétise aucun motif.
- La factory de presets reste limitée aux assemblages de chemins techniques
  encore couverts : Beam, Seam interne et Zipper.
- Les tests et captures qui validaient les formes Eye/Button inventées sont
  supprimés ou remplacés par une preuve de texture importée.
