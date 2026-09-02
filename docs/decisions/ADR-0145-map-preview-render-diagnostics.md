# ADR-0145 — Diagnostics conservés de la preview Map Studio

- Statut : accepté
- Date : 2026-09-02

## Contexte

La preview de Map Studio ignorait le résultat de
`OpenGLVectorRenderer::draw()` et transformait les échecs de texture en
référence absente sans explication. Un canvas vide ne permettait donc pas de
distinguer une map vide, un PNG invalide ou un échec GPU.

## Décision

La passerelle interne `MapPreviewRenderer` conserve les erreurs de résolution
du document, chargement et décodage de texture, upload OpenGL et draw. Le
canvas les affiche. Save, édition et Validate restent disponibles, tandis que
Preview et Publish sont désactivés avec leur raison lorsque le renderer n’est
pas opérationnel.

Les captures de preuve lisent le back buffer avant le swap. Leur sonde exige
les dimensions minimales attendues et plusieurs valeurs de canal distinctes.

## Conséquences

- Une panne graphique n’empêche pas la récupération ou la sauvegarde du map.
- Une publication ne peut plus être présentée comme prévisualisée lorsque le
  renderer a échoué.
- `OpenGLVectorRenderer::draw()` reste la source des statistiques et erreurs.
