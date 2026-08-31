# ADR-0136 — Beam comme parcours guidé du stroke

## Statut

Accepté

## Contexte

Le projet persistait déjà les chemins texturés sous le contrat `texturedPath`.
Le parcours Asset Studio exposait toutefois ce contrat technique sous le nom
`Seam`, ce qui mélangeait le modèle moteur et l’intention visuelle de l’auteur.

## Décision

`Beam` est le nom public de la création guidée correspondant au chemin texturé.
Le contrat JSON, l’énumération interne et les identifiants historiques restent
compatibles. Une création Beam reçoit la `defaultStrokeTexture` du manifeste
quand aucune variante locale n’est choisie et persiste son profil Thread,
classification Beam, couleurs, répétition, brillance et holographie dans le
chemin texturé produit.

Le rendu reste partagé par Asset Studio, Preview Runtime et runtime publié via
la géométrie de ruban par longueur d’arc. La tangente du chemin détermine
l’orientation ; le choix de texture manuel ne modifie que la ressource
sélectionnée.

Les ressources techniques restent disponibles dans un menu `Advanced` et ne
sont pas supprimées des projets existants.

## Conséquences

- Les tests et fixtures peuvent continuer à employer `VisualPresetKind::seam`
  et les identifiants historiques.
- Les libellés normaux de l’interface ne présentent plus `Seam` comme concept
  de création.
- Toute évolution du calcul UV doit être faite dans le builder partagé et
  couverte par les tests de géométrie avant d’être exposée dans l’interface.
