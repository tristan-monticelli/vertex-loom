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

Le renderer OpenGL applique aussi le shader aux lots de remplissage et sépare
les lots quand leurs paramètres shader diffèrent ; une composition ne perd
donc pas son coloring en passant par le chemin batché.

Les ressources techniques restent disponibles dans un menu `Advanced` et ne
sont pas supprimées des projets existants.

Le parcours Button ne synthétise pas une nouvelle illustration : il référence
un composant Button déjà fourni par le projet. Les presets de formes historiques
restent uniquement des outils de compatibilité/avancé.

## Conséquences

- Les tests et fixtures peuvent continuer à employer `VisualPresetKind::seam`
  et les identifiants historiques.
- Les libellés normaux de l’interface ne présentent plus `Seam` comme concept
  de création.
- Toute évolution du calcul UV doit être faite dans le builder partagé et
  couverte par les tests de géométrie avant d’être exposée dans l’interface.
- Le test isolé du Beam vérifie la texture par défaut et les paramètres shader
  avant toute vérification visuelle OpenGL.
