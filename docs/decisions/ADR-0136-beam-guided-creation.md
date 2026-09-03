# ADR-0136 — Beam comme parcours guidé du stroke

## Statut

Accepté

## Contexte

Le projet persistait déjà les chemins texturés sous le contrat `texturedPath`.
Le parcours Asset Studio exposait toutefois ce contrat technique sous le nom
`Seam`, ce qui mélangeait le modèle moteur et l’intention visuelle de l’auteur.

## Décision

`Beam` est le nom public et le type de demande guidée correspondant au chemin
texturé. `VisualPresetKind::beam` est distinct du preset historique
`VisualPresetKind::seam`; le contrat JSON, les identifiants de ressources et le
format `texturedPath` restent compatibles. Une création Beam reçoit la
`defaultStrokeTexture` du manifeste
quand aucune variante locale n’est choisie et persiste son profil Thread,
classification Beam, couleurs, répétition, brillance et holographie dans le
chemin texturé produit.

Un projet créé depuis Asset Studio installe immédiatement la ressource PNG
`beam-thread` dans son dossier `assets/textures` et la définit comme
`defaultStrokeTexture`. Le bouton Beam ne peut donc pas ouvrir un projet neuf
avec une référence de texture absente.

Les nouvelles compositions Beam utilisent aussi l'identifiant de calque
`beam` pour leurs bindings publics. `seam` reste accepté uniquement dans les
ressources historiques ; il ne doit plus apparaître dans l'inspecteur d'un
Beam nouvellement créé.

Le rendu reste partagé par Asset Studio, Preview Runtime et runtime publié via
la géométrie de ruban par longueur d’arc. La tangente du chemin détermine
l’orientation ; le choix de texture manuel ne modifie que la ressource
sélectionnée.

Les `textureMetrics` persistées décrivent la bande source : U couvre les bords
gauche/droit de la frise et V couvre uniquement son épaisseur. U avance par
longueur d’arc ; V ne se répète jamais. Les caps et raccords réutilisent ces
mêmes bornes afin de ne pas inverser, recouper ou boucler la texture dans un
autre axe. Le PNG original reste stocké dans sa résolution complète ; ces
métriques remplacent tout recadrage destructif du fichier importé. Le draw
packet du chemin marque son image en `stretch` technique afin que le renderer
ne réapplique pas ensuite un second cadrage `cover` sur les UV déjà calculés.

La texture Beam fournie par défaut peut déjà contenir une frise complète. Le
preset guidé commence donc avec une répétition de `1` afin de préserver cette
frise ; la répétition reste un paramètre local et avancé pour les textures qui
contiennent un motif unique. Cette valeur est normalisée sur la longueur totale
du chemin : `1` couvre une fois le trajet du début à la fin, indépendamment de
sa longueur en unités monde.

Pour une frise complète dont les bords gauche et droit ne sont pas conçus pour
un raccord direct, le mapping avancé `Mirror tile` répète uniquement l'axe U
du chemin en miroir. L'axe V reste toujours clampé sur l'épaisseur ; le chemin
et sa tangente ne sont jamais inversés.

Après création, l'inspecteur Beam reprend dans un seul groupe la texture, le
profil, les deux couleurs, l'épaisseur, la répétition, la brillance,
l'holographie et l'opacité. Les réglages de contrat UV restent repliés dans la
section avancée. La couleur du ruban demeure blanche : la coloration Beam est
appliquée une seule fois par le profil Thread afin de conserver les détails du
PNG source. Les terminaisons d'un Beam guidé restent droites et l'offset U
n'est pas exposé : les bords gauche et droit du PNG ne sont ni arrondis, ni
décalés, ni recadrés par l'outil.

Un nouveau Beam guidé commence en `Source intacte` : couleurs, alpha et détails
du PNG sont conservés, et les intensités de teinte, holographie et brillance
valent zéro. Aucune palette produit n'est implicite. L'unique action rapide
`Réinitialiser depuis la source` restaure cet état ; la pile ordonnée reste
disponible dans `Advanced effect stack`.

Le réglage `Traitement des couleurs` distingue `Source intacte`, sélectionné par
défaut, de `Recoloration`, qui utilise la luminance et l'alpha du PNG avec
uniquement la couleur choisie. `VisualPresetRequest::BeamColorMode` mappe ce
choix vers les profils shaders existants. Le JSON persistant ne change pas : le
paramètre texte historique du composant et les profils `plastic`/`thread`
restent le pont de compatibilité. Les ressources existantes conservent leurs
valeurs enregistrées ; seule une nouvelle création ou une réinitialisation
explicite reçoit le nouveau défaut.

Le renderer OpenGL applique aussi le shader aux lots de remplissage et sépare
les lots quand leurs paramètres shader diffèrent ; une composition ne perd
donc pas son coloring en passant par le chemin batché.
L’opacité du profil et les canaux alpha des couleurs sont appliqués au fragment
final dans les deux variantes GLSL.

Les ressources techniques restent disponibles dans un menu `Advanced` et ne
sont pas supprimées des projets existants.

Le parcours Button ne synthétise pas une nouvelle illustration : il référence
une texture originale déjà fournie par le projet. Les anciens assets nommés
`head` sont des images de boutons du jeu et sont sélectionnés comme drawable
texture. Les motifs de bouton paramétriques historiques sont retirés par
ADR-0137 et ne font plus partie du produit.

## Conséquences

- Les tests et fixtures peuvent continuer à employer `VisualPresetKind::seam`
  et les identifiants historiques.
- Les libellés normaux de l’interface ne présentent plus `Seam` comme concept
  de création.
- Toute évolution du calcul UV doit être faite dans le builder partagé et
  couverte par les tests de géométrie avant d’être exposée dans l’interface.
- Le test isolé du Beam vérifie la texture par défaut et les paramètres shader
  avant toute vérification visuelle OpenGL.
- L'E2E UI Beam ouvre l'assistant, clique réellement sur `Create Beam`, capture
  le résultat texturé puis recharge épaisseur, opacité, répétition et texture.
- La session vérifie que la texture sélectionnée, y compris la texture par
  défaut, existe dans l’index du projet avant publication ; une référence
  invalide est affichée comme erreur actionnable.
