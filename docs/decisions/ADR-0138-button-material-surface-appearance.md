# ADR-0138 — Apparence shader des Buttons portée par Material

## Statut

Accepté — 2026-08-31.

## Contexte

Le parcours Button référence correctement un PNG original, mais les drawables
Entity de type `texture` ignorent leur matériau au runtime. Le contrat Material
v1 ne peut par ailleurs stocker ni profil shader, ni couleur d'effet, ni
brillance, ni holographie. L'interface ne peut donc pas proposer les réglages
demandés sans produire un aperçu trompeur ou inventer une nouvelle image.

## Décision

`MaterialDefinition` passe en v2 et porte un `ShaderSurfaceSettings` optionnel.
Les documents Material v1 restent lisibles et migrent avec un shader désactivé.

Le même matériau est appliqué aux draw packets vectoriels et raster des
Entities dans Asset Studio, Preview Runtime et le runtime publié. Le parcours
guidé Button crée un matériau d'apparence lié au PNG explicitement choisi, puis
attache ce matériau à l'Entity Button. Il expose couleur principale, couleur
d'effet, brillance, holographie et opacité. Il ne crée, ne remplace et ne
transforme aucune texture source.

## Conséquences

- Les deux PNG originaux restent les seules sources visuelles des Buttons.
- Un Button peut être recoloré sans dupliquer ni altérer son image.
- Les anciens matériaux conservent exactement leur rendu sans shader.
- Le renderer commun reste l'unique implémentation de la formule shader.
- La création Material puis Entity doit vérifier les deux destinations avant
  publication et être couverte par un reload et une capture UI.
