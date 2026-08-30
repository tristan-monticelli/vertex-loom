# ADR-0070 — Snapping configurable des placements de map

## Décision

`MapSession` applique optionnellement un `MapSnapSettings` aux positions lors
du placement et de la transformation d’une instance. Le pas, l’origine et
l’activation sont explicites ; la rotation, l’échelle et le pivot ne sont pas
modifiés. Les positions négatives utilisent le même arrondi déterministe que
les positions positives.

Un pas nul, négatif ou non fini désactive le snapping pour l’opération. Le
calcul du chunk est effectué après le snapping et reste dans la commande
réversible de la session.

## Conséquences

Les futurs outils de placement peuvent partager exactement le même calcul
headless que l’interface. Les overrides, gizmos et règles de snapping avancées
restent des étapes distinctes.
