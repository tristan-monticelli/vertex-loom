# ADR-0143 — Pile modulaire d'effets de surface

- Status: accepted
- Date: 2026-09-01

## Contexte

Le contrat shader expose un profil, deux couleurs, une brillance et une
holographie fixes. Cette forme empêche un Button, un Beam ou un PNG d'employer
plusieurs teintes ou plusieurs occurrences d'un même effet et oblige
l'inspecteur à présenter un formulaire différent pour chaque type d'asset.

## Décision

`ShaderSurfaceSettings` conserve ses champs historiques pour relire les assets
existants et ajoute une liste ordonnée `effects`. Chaque bloc possède un type
(`Tint`, `Holography` ou `Shine`), un état actif, une couleur, une intensité et
une échelle de motif. Une liste absente ou vide utilise strictement le calcul
historique ; une liste présente devient la source de vérité du rendu.

Asset Studio utilise un seul éditeur de pile pour les materials, les Buttons
et les Beams. Il permet d'ajouter, dupliquer, monter, descendre et retirer des
blocs sans maximum applicatif. L'ordre affiché est l'ordre d'évaluation.

Pour les parcours guidés Beam et Button, ce même éditeur présente d'abord une
façade compacte : mode source/recoloration, couleur choisie, forces de
recoloration et de lueur, et reflet. Une seule action neutre réinitialise depuis
la source ; aucune palette colorée n'est proposée. Chaque modification
synchronise aussi les champs historiques afin
que les anciens bindings et la pile modulaire ne présentent pas deux valeurs
contradictoires. Les opérations d'ordre et de duplication restent dans la
section avancée repliée.

Le renderer transmet les blocs au fragment shader dans une texture de
paramètres allouée selon la taille de la pile. Il ne déclare donc aucun tableau
uniforme de taille fixe. Si une pile dépasse la taille de texture supportée par
le GPU, le draw packet est refusé avec un diagnostic ; aucun bloc n'est ignoré.

## Conséquences

- Les JSON historiques restent lisibles et conservent leur apparence.
- Les nouveaux effets sont sauvegardés et rechargés dans leur ordre exact.
- Studio, Preview Runtime et runtime publié consomment le même draw packet et
  le même calcul shader.
- Les overrides historiques continuent de cibler les anciens champs ; un
  futur contrat d'animation pourra adresser un bloc par identifiant stable.
- Pour le profil Thread, le PNG source fournit la luminance et le détail ; la
  couleur visible vient des blocs configurés dans l'inspecteur. Aucune palette
  rose, bleue ou dorée n'est imposée par le shader.
